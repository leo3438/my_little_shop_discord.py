// PKForgeNX - point d'entrée.
//
// Étape 1 d'architecture : initialisation Horizon/SDL2/GLES3/ImGui, détection du
// compte utilisateur, montage (lecture seule) des sauvegardes Écarlate/Violet,
// lecture de la taille du fichier `main` et backup vérifié (SHA-256) vers la SD.
//
// Rappels mémoire Horizon OS utiles pour lire ce fichier :
//  * Le tas (heap) newlib est un bloc unique réservé par libnx au démarrage via
//    svcSetHeapSize. Sa taille dépend du mode de lancement :
//      - mode applet (hbmenu via l'Album)  : quelques centaines de Mo, partagés
//        avec Mesa/nouveau (buffers GPU, command lists, textures) ;
//      - mode application (title override) : ~3 Go.
//    Tout ce qui est gros (buffers de copie, fichier de sauvegarde, sprites) doit
//    donc aller sur le tas, jamais sur la pile, et être dimensionné avec prudence.
//  * La pile du thread principal est petite (définie par le NRO/HBL, ordre du Mo) :
//    aucun tableau de plusieurs centaines de Ko en variable locale.
//  * Chaque device fsdev monté détient une session IPC `fsp-srv`, ressource
//    système limitée : on monte à la demande et on démonte dès que possible (RAII).
//  * Une sauvegarde montée est verrouillée pour le jeu : si le jeu est suspendu en
//    arrière-plan, le montage échoue. Il faut fermer le jeu avant.

#include <switch.h>

#include <SDL.h>
#include <GLES3/gl3.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <sys/stat.h>
#ifdef PKF_DEBUG
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>
#include <string>
#include <vector>

#ifndef PKF_VERSION
#define PKF_VERSION "dev"
#endif

namespace {

//------------------------------------------------------------------------------
// Constantes
//------------------------------------------------------------------------------

// Nom du device fsdev (max 31 caractères). Préfixé pour ne jamais entrer en
// collision avec "sdmc" ou "romfs".
constexpr const char* kSaveDevice   = "pkfsave";
constexpr const char* kSaveMainPath = "pkfsave:/main";
constexpr const char* kBackupRoot   = "sdmc:/switch/PKForgeNX/backups";

// Taille des blocs de copie : 1 Mio alloué sur le tas. Au-delà de la taille du
// buffer interne de newlib, fread/fwrite transmettent directement le bloc au
// service fs, ce qui limite le nombre d'allers-retours IPC.
constexpr size_t kCopyChunkSize = 1u << 20;

constexpr int kWindowWidth  = 1280;
constexpr int kWindowHeight = 720;

struct GameTarget {
    const char* name;
    u64         titleId;
};

// Title IDs officiels (base game). NB : 0100A3D0*1*8C5C000 / 01008F60*1*8C5E000
// ne correspondent pas aux jeux ; les valeurs correctes ont un 0 à cette position.
constexpr std::array<GameTarget, 2> kTargets{{
    {"Pokémon Écarlate", 0x0100A3D008C5C000ULL},
    {"Pokémon Violet",   0x01008F6008C5E000ULL},
}};

//------------------------------------------------------------------------------
// Utilitaires
//------------------------------------------------------------------------------

// Format officiel des codes d'erreur Nintendo : 2MMM-DDDD.
std::string formatResult(Result rc) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "0x%08X (%04u-%04u)", rc, 2000u + R_MODULE(rc), R_DESCRIPTION(rc));
    return buf;
}

std::string formatBytes(u64 bytes) {
    char buf[32];
    if (bytes >= (1ULL << 20))
        std::snprintf(buf, sizeof(buf), "%.2f Mio", static_cast<double>(bytes) / (1 << 20));
    else if (bytes >= (1ULL << 10))
        std::snprintf(buf, sizeof(buf), "%.2f Kio", static_cast<double>(bytes) / (1 << 10));
    else
        std::snprintf(buf, sizeof(buf), "%" PRIu64 " o", bytes);
    return buf;
}

std::string toHex(const u8* data, size_t len) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out(len * 2, '0');
    for (size_t i = 0; i < len; ++i) {
        out[2 * i]     = kDigits[data[i] >> 4];
        out[2 * i + 1] = kDigits[data[i] & 0xF];
    }
    return out;
}

bool isApplicationMode() {
    const AppletType type = appletGetAppletType();
    return type == AppletType_Application || type == AppletType_SystemApplication;
}

// Mémoire vue par le noyau pour notre processus (inclut le tas et les mappings GPU).
void queryProcessMemory(u64& used, u64& total) {
    used = total = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
}

// mkdir -p pour les chemins "device:/a/b/c".
bool makeDirectories(const std::string& path) {
    const size_t root = path.find(":/");
    if (root == std::string::npos)
        return false;
    for (size_t pos = path.find('/', root + 2); ; pos = path.find('/', pos + 1)) {
        const std::string part = path.substr(0, pos);
        if (mkdir(part.c_str(), 0777) != 0 && errno != EEXIST)
            return false;
        if (pos == std::string::npos)
            return true;
    }
}

//------------------------------------------------------------------------------
// RAII autour des ressources Horizon
//------------------------------------------------------------------------------

// Montage d'une sauvegarde compte (FsSaveDataType_Account) via fsdev.
// ReadOnly : aucune écriture possible, aucun risque pour le journal de la save.
// ReadWrite : réservé aux futures écritures, qui devront appeler commit().
class SaveMount {
public:
    enum class Mode { ReadOnly, ReadWrite };

    SaveMount(u64 titleId, AccountUid uid, Mode mode) {
        m_rc = (mode == Mode::ReadOnly) ? fsdevMountSaveDataReadOnly(kSaveDevice, titleId, uid)
                                        : fsdevMountSaveData(kSaveDevice, titleId, uid);
    }
    ~SaveMount() {
        if (R_SUCCEEDED(m_rc))
            fsdevUnmountDevice(kSaveDevice); // libère la session fsp-srv
    }
    SaveMount(const SaveMount&)            = delete;
    SaveMount& operator=(const SaveMount&) = delete;

    bool   ok() const { return R_SUCCEEDED(m_rc); }
    Result rc() const { return m_rc; }

    // Les écritures dans une save sont journalisées : rien n'est persisté tant que
    // le journal n'est pas commité. Un démontage sans commit annule tout.
    Result commit() const { return fsdevCommitDevice(kSaveDevice); }

private:
    Result m_rc = 0;
};

// Empêche HOME/fermeture de tuer le processus pendant une opération disque.
// Horizon laisse 15 s avant de forcer la terminaison si l'exit reste verrouillé.
class ExitLock {
public:
    ExitLock() : m_locked(R_SUCCEEDED(appletLockExit())) {}
    ~ExitLock() {
        if (m_locked)
            appletUnlockExit();
    }
    ExitLock(const ExitLock&)            = delete;
    ExitLock& operator=(const ExitLock&) = delete;

private:
    bool m_locked;
};

struct FileCloser {
    void operator()(FILE* f) const {
        if (f)
            std::fclose(f);
    }
};
using UniqueFile = std::unique_ptr<FILE, FileCloser>;

//------------------------------------------------------------------------------
// Comptes utilisateurs
//------------------------------------------------------------------------------

struct UserEntry {
    AccountUid  uid{};
    std::string nickname;
};

std::vector<UserEntry> listUsers() {
    std::vector<UserEntry> users;
    AccountUid uids[ACC_USER_LIST_SIZE]{};
    s32 total = 0;
    if (R_FAILED(accountListAllUsers(uids, ACC_USER_LIST_SIZE, &total)))
        return users;

    for (s32 i = 0; i < total; ++i) {
        UserEntry entry;
        entry.uid = uids[i];

        AccountProfile     profile;
        AccountProfileBase base{};
        if (R_SUCCEEDED(accountGetProfile(&profile, uids[i]))) {
            if (R_SUCCEEDED(accountProfileGet(&profile, nullptr, &base)))
                // nickname est un char[0x20] UTF-8 non garanti terminé par '\0'.
                entry.nickname.assign(base.nickname, strnlen(base.nickname, sizeof(base.nickname)));
            accountProfileClose(&profile);
        }
        if (entry.nickname.empty())
            entry.nickname = "Utilisateur " + std::to_string(i + 1);
        users.push_back(std::move(entry));
    }
    return users;
}

// Priorité : utilisateur présélectionné (title override avec sélection de
// profil) > dernier utilisateur ouvert > premier compte.
int pickDefaultUser(const std::vector<UserEntry>& users) {
    AccountUid preferred{};
    if (R_FAILED(accountGetPreselectedUser(&preferred)) || !accountUidIsValid(&preferred))
        if (R_FAILED(accountGetLastOpenedUser(&preferred)))
            preferred = {};

    for (size_t i = 0; i < users.size(); ++i)
        if (std::memcmp(&users[i].uid, &preferred, sizeof(AccountUid)) == 0)
            return static_cast<int>(i);
    return users.empty() ? -1 : 0;
}

//------------------------------------------------------------------------------
// Sonde de sauvegarde
//------------------------------------------------------------------------------

struct ProbeResult {
    bool   attempted  = false;
    Result mountRc    = 0;
    bool   mounted    = false;
    bool   mainFound  = false;
    s64    mainSize   = -1;
    int    statErrno  = 0;
};

ProbeResult probeSave(const GameTarget& game, AccountUid uid) {
    ProbeResult result;
    result.attempted = true;

    SaveMount mount(game.titleId, uid, SaveMount::Mode::ReadOnly);
    result.mountRc = mount.rc();
    result.mounted = mount.ok();
    if (!mount.ok())
        return result;

    struct stat st{};
    if (stat(kSaveMainPath, &st) == 0 && S_ISREG(st.st_mode)) {
        result.mainFound = true;
        result.mainSize  = st.st_size;
    } else {
        result.statErrno = errno;
    }
    return result; // démontage automatique ici
}

//------------------------------------------------------------------------------
// Backup (obligatoire avant toute écriture future)
//------------------------------------------------------------------------------

struct BackupResult {
    bool        done = false;
    bool        ok   = false;
    std::string path;
    std::string message;
};

// Copie `in` vers `out` (si non nul) par blocs, en calculant le SHA-256 à la
// volée. sha256Context* de libnx utilise les instructions ARMv8 Crypto Extensions.
bool streamWithHash(FILE* in, FILE* out, u8* chunk, u8 (&hash)[SHA256_HASH_SIZE], u64& bytes) {
    Sha256Context ctx;
    sha256ContextCreate(&ctx);
    bytes = 0;
    for (;;) {
        const size_t n = std::fread(chunk, 1, kCopyChunkSize, in);
        if (n > 0) {
            sha256ContextUpdate(&ctx, chunk, n);
            if (out && std::fwrite(chunk, 1, n, out) != n)
                return false;
            bytes += n;
        }
        if (n < kCopyChunkSize) {
            if (std::ferror(in))
                return false;
            break;
        }
    }
    sha256ContextGetHash(&ctx, hash);
    return true;
}

BackupResult backupMainFile(const GameTarget& game, const UserEntry& user) {
    BackupResult result;
    result.done = true;

    // Buffer de copie sur le tas : new(std::nothrow) pour gérer proprement un OOM
    // en mode applet plutôt que d'aboutir à std::terminate.
    std::unique_ptr<u8[]> chunk(new (std::nothrow) u8[kCopyChunkSize]);
    if (!chunk) {
        result.message = "Mémoire insuffisante pour le buffer de copie.";
        return result;
    }

    ExitLock  exitLock;
    SaveMount mount(game.titleId, user.uid, SaveMount::Mode::ReadOnly);
    if (!mount.ok()) {
        result.message = "Montage impossible : " + formatResult(mount.rc());
        return result;
    }

    // sdmc:/switch/PKForgeNX/backups/<TitleID>/<AAAAMMJJ-HHMMSS>_<uid>/main
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::tm tmNow{};
    localtime_r(&now, &tmNow);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmNow);

    char dir[160];
    std::snprintf(dir, sizeof(dir), "%s/%016" PRIX64 "/%s_%016" PRIX64, kBackupRoot, game.titleId, stamp,
                  user.uid.uid[0]);
    if (!makeDirectories(dir)) {
        result.message = std::string("Création du dossier impossible : ") + std::strerror(errno);
        return result;
    }
    result.path = std::string(dir) + "/main";

    u8  srcHash[SHA256_HASH_SIZE]{};
    u64 copied = 0;
    {
        UniqueFile src(std::fopen(kSaveMainPath, "rb"));
        UniqueFile dst(std::fopen(result.path.c_str(), "wb"));
        if (!src || !dst) {
            result.message = std::string("Ouverture impossible : ") + std::strerror(errno);
            return result;
        }
        if (!streamWithHash(src.get(), dst.get(), chunk.get(), srcHash, copied)) {
            result.message = "Erreur d'E/S pendant la copie.";
            dst.reset();
            std::remove(result.path.c_str());
            return result;
        }
        // fclose explicite pour détecter un échec de flush (SD pleine...).
        if (std::fclose(dst.release()) != 0) {
            result.message = "Échec de la finalisation du fichier (SD pleine ?).";
            std::remove(result.path.c_str());
            return result;
        }
    }

    // Vérification : relecture complète de la copie depuis la SD.
    u8  dstHash[SHA256_HASH_SIZE]{};
    u64 verified = 0;
    {
        UniqueFile check(std::fopen(result.path.c_str(), "rb"));
        if (!check || !streamWithHash(check.get(), nullptr, chunk.get(), dstHash, verified) ||
            verified != copied || std::memcmp(srcHash, dstHash, SHA256_HASH_SIZE) != 0) {
            result.message = "Vérification SHA-256 échouée : backup invalide.";
            check.reset();
            std::remove(result.path.c_str());
            return result;
        }
    }

    // Empreinte à côté du backup (format compatible `sha256sum -c`).
    const std::string hashHex = toHex(srcHash, SHA256_HASH_SIZE);
    if (UniqueFile sum(std::fopen((std::string(dir) + "/main.sha256").c_str(), "w")); sum)
        std::fprintf(sum.get(), "%s  main\n", hashHex.c_str());

    result.ok      = true;
    result.message = formatBytes(copied) + " copiés, SHA-256 " + hashHex.substr(0, 16) + "...";
    return result;
}

//------------------------------------------------------------------------------
// État applicatif + UI
//------------------------------------------------------------------------------

struct AppState {
    bool                                        applicationMode = false;
    bool                                        romfsOk         = false;
    std::vector<UserEntry>                      users;
    int                                         selectedUser = -1;
    std::array<ProbeResult, kTargets.size()>    probes{};
    std::array<BackupResult, kTargets.size()>   backups{};
    bool                                        quitRequested = false;
};

void probeAll(AppState& state) {
    if (state.selectedUser < 0)
        return;
    const AccountUid uid = state.users[state.selectedUser].uid;
    for (size_t i = 0; i < kTargets.size(); ++i) {
        state.probes[i]  = probeSave(kTargets[i], uid);
        state.backups[i] = {};
    }
}

void drawUi(AppState& state) {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("PKForgeNX", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);

    u64 memUsed = 0, memTotal = 0;
    queryProcessMemory(memUsed, memTotal);
    ImGui::Text("PKForgeNX v%s  |  Mode : %s  |  Mémoire : %s / %s", PKF_VERSION,
                state.applicationMode ? "application" : "applet", formatBytes(memUsed).c_str(),
                formatBytes(memTotal).c_str());
    if (!state.applicationMode)
        ImGui::TextColored(ImVec4(1.f, .75f, .2f, 1.f),
                           "Mode applet : mémoire limitée. Lancez via un jeu (maintenir R) pour le mode complet.");
    if (!state.romfsOk)
        ImGui::TextColored(ImVec4(1.f, .75f, .2f, 1.f), "RomFS indisponible : assets non chargés.");
    ImGui::Separator();

    // Sélection du compte
    if (state.users.empty()) {
        ImGui::TextColored(ImVec4(1.f, .3f, .3f, 1.f), "Aucun compte utilisateur trouvé.");
    } else {
        const char* preview = state.users[state.selectedUser].nickname.c_str();
        ImGui::SetNextItemWidth(400.f);
        if (ImGui::BeginCombo("Compte", preview)) {
            for (int i = 0; i < static_cast<int>(state.users.size()); ++i) {
                const bool selected = (i == state.selectedUser);
                if (ImGui::Selectable(state.users[i].nickname.c_str(), selected) && !selected) {
                    state.selectedUser = i;
                    probeAll(state);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Réessayer le montage"))
            probeAll(state);
    }
    ImGui::Spacing();

    // Résultats par jeu
    const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("saves", 5, tableFlags)) {
        ImGui::TableSetupColumn("Jeu", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Title ID", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Montage", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Fichier main", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < kTargets.size(); ++i) {
            const GameTarget&  game  = kTargets[i];
            const ProbeResult& probe = state.probes[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(game.name);

            ImGui::TableNextColumn();
            ImGui::Text("%016" PRIX64, game.titleId);

            ImGui::TableNextColumn();
            if (!probe.attempted)
                ImGui::TextDisabled("-");
            else if (probe.mounted)
                ImGui::TextColored(ImVec4(.3f, 1.f, .4f, 1.f), "Succès");
            else
                ImGui::TextColored(ImVec4(1.f, .3f, .3f, 1.f), "Échec %s", formatResult(probe.mountRc).c_str());

            ImGui::TableNextColumn();
            if (probe.mainFound)
                ImGui::Text("%s (%" PRId64 " o)", formatBytes(static_cast<u64>(probe.mainSize)).c_str(), probe.mainSize);
            else if (probe.mounted)
                ImGui::TextColored(ImVec4(1.f, .3f, .3f, 1.f), "Absent (%s)", std::strerror(probe.statErrno));
            else
                ImGui::TextDisabled("-");

            ImGui::TableNextColumn();
            ImGui::BeginDisabled(!probe.mainFound);
            if (ImGui::Button("Backup"))
                state.backups[i] = backupMainFile(game, state.users[state.selectedUser]);
            ImGui::EndDisabled();

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    for (size_t i = 0; i < kTargets.size(); ++i) {
        const BackupResult& b = state.backups[i];
        if (!b.done)
            continue;
        const ImVec4 color = b.ok ? ImVec4(.3f, 1.f, .4f, 1.f) : ImVec4(1.f, .3f, .3f, 1.f);
        ImGui::TextColored(color, "[%s] Backup %s : %s", kTargets[i].name, b.ok ? "OK" : "échoué", b.message.c_str());
        if (b.ok)
            ImGui::TextDisabled("    %s", b.path.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Code 2002-1002 : aucune sauvegarde pour ce jeu/compte.  Jeu suspendu : fermez-le avant.");
    ImGui::TextDisabled("(+) Quitter");
    ImGui::End();
}

//------------------------------------------------------------------------------
// Police : police système partagée (shared font) Nintendo
//------------------------------------------------------------------------------

// La police est mappée en mémoire partagée par le service `pl` : pas de copie sur
// notre tas. FontDataOwnedByAtlas = false est impératif, sinon ImGui appellerait
// IM_FREE() sur une adresse qui n'a jamais été allouée par lui (crash). Le mapping
// doit rester valide jusqu'à la construction de l'atlas : plExit() n'est appelé
// qu'après ImGui::DestroyContext().
void loadFonts(bool plReady) {
    ImGuiIO&   io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH          = 2;
    cfg.OversampleV          = 1;

    PlFontData font{};
    if (plReady && R_SUCCEEDED(plGetSharedFontByType(&font, PlSharedFontType_Standard))) {
        // GetGlyphRangesDefault : Basic Latin + Latin-1 (accents français inclus).
        io.Fonts->AddFontFromMemoryTTF(font.address, static_cast<int>(font.size), 26.f, &cfg,
                                       io.Fonts->GetGlyphRangesDefault());
    } else {
        ImFontConfig fallback;
        fallback.SizePixels = 26.f;
        io.Fonts->AddFontDefault(&fallback);
    }
}

} // namespace

//------------------------------------------------------------------------------
// main
//------------------------------------------------------------------------------

int main(int /*argc*/, char** /*argv*/) {
    // Les services de base (sm, applet, hid, time, fs + montage "sdmc:") sont
    // initialisés par le runtime libnx dans __appInit() avant main() et fermés
    // dans __appExit() après : on ne les réinitialise pas ici.

#ifdef PKF_DEBUG
    // stdout/stderr redirigés vers `nxlink -s` sur le PC.
    const bool netReady  = R_SUCCEEDED(socketInitializeDefault());
    const int  nxlinkSock = netReady ? nxlinkStdio() : -1;
#endif

    AppState state;
    state.applicationMode = isApplicationMode();

    // RomFS embarqué dans le .nro (fonts, sprites, tables de données).
    state.romfsOk = R_SUCCEEDED(romfsInit());

    // acc:u0 (Application) donne accès à l'utilisateur présélectionné, mais n'est
    // pleinement fonctionnel qu'en mode application ; en mode applet on passe par
    // acc:u1 (System), accessible au homebrew via HBL.
    const Result accRc = accountInitialize(state.applicationMode ? AccountServiceType_Application
                                                                 : AccountServiceType_System);
    if (R_SUCCEEDED(accRc)) {
        state.users        = listUsers();
        state.selectedUser = pickDefaultUser(state.users);
    }

    const bool plReady = R_SUCCEEDED(plInitialize(PlServiceType_User));

    // Tentative de montage initiale (lecture seule) des deux jeux.
    probeAll(state);

    //--------------------------------------------------------------------------
    // SDL2 + contexte OpenGL ES 3.0 (Mesa/nouveau via EGL)
    //--------------------------------------------------------------------------
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return EXIT_FAILURE; // __appExit() ferme les services de base
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // Pas de depth/stencil : ImGui n'en a pas besoin, économise la mémoire GPU
    // prise sur le tas du processus.
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    // 1280x720 : résolution native portable ; en dock, la sortie est upscalée par
    // le compositeur système (pas de réallocation de framebuffer à gérer).
    SDL_Window* window = SDL_CreateWindow("PKForgeNX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kWindowWidth,
                                          kWindowHeight, SDL_WINDOW_OPENGL);
    SDL_GLContext glContext = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!glContext) {
        std::fprintf(stderr, "SDL window/GL context: %s\n", SDL_GetError());
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1); // VSync : cadence 60 Hz, pas de busy-loop CPU

    //--------------------------------------------------------------------------
    // ImGui
    //--------------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // pas d'imgui.ini écrit à la racine de la SD
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(1.6f); // lisibilité à distance TV / tactile
    loadFonts(plReady);

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    std::vector<SDL_GameController*> controllers;

    //--------------------------------------------------------------------------
    // Boucle principale
    //--------------------------------------------------------------------------
    // Le portage SDL2 Switch appelle appletMainLoop() dans SDL_PumpEvents et
    // convertit une demande de fermeture (HOME -> fermer, retour hbmenu) en
    // SDL_QUIT : on ne rappelle donc pas appletMainLoop() ici, pour ne pas
    // consommer les messages applet à la place de SDL.
    while (!state.quitRequested) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            switch (event.type) {
            case SDL_QUIT:
                state.quitRequested = true;
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (SDL_GameController* pad = SDL_GameControllerOpen(event.cdevice.which))
                    controllers.push_back(pad);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (SDL_GameController* pad = SDL_GameControllerFromInstanceID(event.cdevice.which)) {
                    controllers.erase(std::remove(controllers.begin(), controllers.end(), pad), controllers.end());
                    SDL_GameControllerClose(pad);
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) // bouton (+)
                    state.quitRequested = true;
                break;
            default:
                break;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        drawUi(state);
        ImGui::Render();

        int fbWidth = 0, fbHeight = 0;
        SDL_GL_GetDrawableSize(window, &fbWidth, &fbHeight);
        glViewport(0, 0, fbWidth, fbHeight);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    //--------------------------------------------------------------------------
    // Arrêt ordonné (ordre inverse de l'initialisation)
    //--------------------------------------------------------------------------
    // Aucune save n'est montée ici : SaveMount démonte en sortie de portée.
    for (SDL_GameController* pad : controllers)
        SDL_GameControllerClose(pad);

    ImGui_ImplOpenGL3_Shutdown(); // libère VBO/shaders/texture d'atlas côté GPU
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();      // libère l'atlas CPU -> la shared font n'est plus référencée

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (plReady)
        plExit();
    if (R_SUCCEEDED(accRc))
        accountExit();
    if (state.romfsOk)
        romfsExit();

#ifdef PKF_DEBUG
    if (nxlinkSock >= 0)
        close(nxlinkSock);
    if (netReady)
        socketExit();
#endif
    // Retour à hbmenu : __appExit() démonte sdmc et ferme fs/applet/hid/sm.
    return EXIT_SUCCESS;
}
