#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <iterator>

#include "MemoryFileSystem.hpp"
#include "retromanager/forwarder/IconMaker.hpp"
#include "retromanager/services/ForwarderBuilder.hpp"

using namespace rm;

namespace {

// Fake test keys: patterns, never real console keys.
const char* kFakeKeys =
    "header_key = 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n"
    "key_area_key_application_00 = a0a1a2a3a4a5a6a7a8a9aaabacadaeaf\n";

nsp::NcaKeys fakeKeys() { return nsp::parseProdKeys(kFakeKeys).value(); }

std::string fakeNpdm() {
    std::string npdm(0x80 + 0x240 + 0x40, '\0');
    auto put32 = [&](std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) npdm[at + i] = static_cast<char>(v >> (8 * i));
    };
    std::memcpy(&npdm[0], "META", 4);
    put32(0x78, 0x80);
    put32(0x7C, 0x240);
    put32(0x70, 0x80 + 0x240);
    put32(0x74, 0x40);
    std::memcpy(&npdm[0x80 + 0x200], "ACID", 4);
    std::memcpy(&npdm[0x80 + 0x240], "ACI0", 4);
    return npdm;
}

std::string fixture(const std::string& relative) {
    std::ifstream in(std::string(RM_FIXTURE_SD_DIR) + "/../ftp_root/" + relative, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string str(const Bytes& b) { return std::string(b.begin(), b.end()); }

GameEntry gbaGame(const std::string& title = "Tom & Jerry") {
    GameEntry g;
    g.id = "gba/Tom _ Jerry (USA).gba";
    g.title = title;
    g.system = "gba";
    g.fileName = "Tom _ Jerry (USA).gba";
    return g;
}

const std::string kRom = "/roms/gba/Tom _ Jerry (USA).gba";
const std::string kBoxArt = "/retroarch/thumbnails/Nintendo - Game Boy Advance/Named_Boxarts/Tom _ Jerry (USA).png";
const std::string kCore = "/retroarch/cores/mgba_libretro_libnx.nro";

// The SD card as seen after writing: what a console could do when the
// write "succeeds" but the file does not land (full card, FAT error...).
class CheckedFileSystem : public test::MemoryFileSystem {
  public:
    enum class Nsp { Normal, Missing, Truncated };
    Nsp nsp = Nsp::Normal;

    Result<FileInfo> stat(std::string_view path) override {
        const bool isNsp = path.size() > 4 && path.substr(path.size() - 4) == ".nsp";
        if (isNsp && nsp == Nsp::Missing) return makeError(ErrorCode::NotFound, std::string(path));
        auto info = test::MemoryFileSystem::stat(path);
        if (isNsp && nsp == Nsp::Truncated && info) info.value().size = 1;
        return info;
    }
};

struct Recorder {
    int calls = 0;
    nsp::ApplicationSpec spec;
    ForwarderBuilder::Packager packager() {
        return [this](const nsp::ApplicationSpec& s, const nsp::NcaKeys&) -> Result<Bytes> {
            ++calls;
            spec = s;
            return Bytes{'N', 'S', 'P'};
        };
    }
};

class ForwarderBuilderTest : public ::testing::Test {
  protected:
    void SetUp() override {
        write("/switch/prod.keys", kFakeKeys);
        write("/switch/RetroManager/stub/main", "STUB-CODE");
        write("/switch/RetroManager/stub/main.npdm", fakeNpdm());
        write(kRom, "ROM");
        write(kCore, "CORE");
        write(kBoxArt, fixture("thumbnails/Nintendo - Game Boy Advance/Named_Boxarts/Tom _ Jerry (USA).png"));
    }

    void write(const std::string& path, const std::string& data) {
        ASSERT_TRUE(fs.createDirectories(path.substr(0, path.rfind('/'))).ok());
        ASSERT_TRUE(fs.writeFile(path, data).ok());
    }

    ForwarderBuilder builder(ForwarderBuilder::Packager packager) {
        return ForwarderBuilder(fs, SdLayout{}, [](const GameEntry&, const std::string&) { return std::optional<std::string>(kBoxArt); },
                                std::move(packager));
    }

    std::string romfsFile(const nsp::ApplicationSpec& spec, const std::string& name) {
        for (const auto& f : spec.romfs) {
            if (f.name == name) return str(f.data);
        }
        return "<missing>";
    }

    CheckedFileSystem fs;
    Recorder recorder;
    CancellationToken cancel;
};

}  // namespace

TEST_F(ForwarderBuilderTest, InjectsTitleIconAndLaunchArguments) {
    ForwarderReport report = builder(recorder.packager()).build(gbaGame(), cancel);
    ASSERT_TRUE(report.ok()) << report.detail;
    ASSERT_EQ(recorder.calls, 1);
    const nsp::ApplicationSpec& spec = recorder.spec;

    EXPECT_EQ(romfsFile(spec, "nextNroPath"), "sdmc:" + kCore);
    EXPECT_EQ(romfsFile(spec, "nextArgv"), "\"sdmc:" + kCore + "\" \"sdmc:" + kRom + "\"");

    EXPECT_EQ(nsp::nacpName(spec.nacp), "Tom & Jerry");
    EXPECT_EQ(nsp::nacpPublisher(spec.nacp), "RetroArch - Game Boy Advance");

    auto icon = forwarder::decodeImage(spec.icon);
    ASSERT_TRUE(icon.ok());
    EXPECT_EQ(icon.value().width, 256);
    EXPECT_EQ(icon.value().height, 256);
    EXPECT_EQ(spec.icon[0], 0xFF);  // JPEG
    EXPECT_FALSE(report.placeholderIcon);

    EXPECT_EQ(spec.titleId, nsp::forwarderTitleId(kRom));
    ASSERT_EQ(spec.exefs.size(), 2u);
    EXPECT_EQ(str(spec.exefs[0].data), "STUB-CODE");
    EXPECT_TRUE(spec.logo.empty());

    EXPECT_EQ(report.nspPath, "/nsp/Tom & Jerry.nsp");
    EXPECT_EQ(fs.readFile("/nsp/Tom & Jerry.nsp").value(), "NSP");
    EXPECT_EQ(report.corePath, kCore);
    EXPECT_EQ(report.romPath, kRom);
    EXPECT_EQ(report.titleId, spec.titleId);
    EXPECT_EQ(report.sizeBytes, 3u);
}

TEST_F(ForwarderBuilderTest, NspThatDoesNotLandOnTheCardIsAnError) {
    fs.nsp = CheckedFileSystem::Nsp::Missing;
    ForwarderReport report = builder(recorder.packager()).build(gbaGame(), cancel);
    EXPECT_EQ(report.issue, ForwarderIssue::WriteFailed);
    EXPECT_NE(report.detail.find("/nsp/Tom & Jerry.nsp"), std::string::npos) << report.detail;

    fs.nsp = CheckedFileSystem::Nsp::Truncated;
    report = builder(recorder.packager()).build(gbaGame(), cancel);
    EXPECT_EQ(report.issue, ForwarderIssue::WriteFailed);
    EXPECT_NE(report.detail.find("1 B"), std::string::npos) << report.detail;
}

TEST_F(ForwarderBuilderTest, MissingKeysSaysWhereToPutThem) {
    ASSERT_TRUE(fs.remove("/switch/prod.keys").ok());
    ForwarderReport report = builder(recorder.packager()).build(gbaGame(), cancel);
    EXPECT_EQ(report.issue, ForwarderIssue::KeysMissing);
    EXPECT_EQ(report.detail, "/switch/prod.keys");
    EXPECT_EQ(recorder.calls, 0);
    EXPECT_FALSE(fs.exists("/nsp"));
    EXPECT_EQ(builder(recorder.packager()).checkPrerequisites().issue, ForwarderIssue::KeysMissing);
}

TEST_F(ForwarderBuilderTest, IncompleteKeysNameTheMissingKey) {
    write("/switch/prod.keys", "header_key = 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n");
    ForwarderReport report = builder(recorder.packager()).build(gbaGame(), cancel);
    EXPECT_EQ(report.issue, ForwarderIssue::KeysInvalid);
    EXPECT_EQ(report.detail, "key_area_key_application_00");
}

TEST_F(ForwarderBuilderTest, MissingStubListsTheFiles) {
    ASSERT_TRUE(fs.removeAll("/switch/RetroManager/stub").ok());
    ForwarderReport report = builder(recorder.packager()).build(gbaGame(), cancel);
    EXPECT_EQ(report.issue, ForwarderIssue::StubMissing);
    EXPECT_EQ(report.detail, "/switch/RetroManager/stub");
    EXPECT_EQ(report.missingFiles, (std::vector<std::string>{"main", "main.npdm"}));

    write("/switch/RetroManager/stub/main", "STUB-CODE");
    report = builder(recorder.packager()).checkPrerequisites();
    EXPECT_EQ(report.missingFiles, (std::vector<std::string>{"main.npdm"}));
}

TEST_F(ForwarderBuilderTest, StubInAnExefsSubfolderAndOptionalLogo) {
    ASSERT_TRUE(fs.removeAll("/switch/RetroManager/stub").ok());
    write("/switch/RetroManager/stub/exefs/main", "STUB-CODE");
    write("/switch/RetroManager/stub/exefs/main.npdm", fakeNpdm());
    write("/switch/RetroManager/stub/logo/NintendoLogo.png", "LOGO");
    ForwarderReport report = builder(recorder.packager()).build(gbaGame(), cancel);
    ASSERT_TRUE(report.ok()) << report.detail;
    ASSERT_EQ(recorder.spec.logo.size(), 1u);
    EXPECT_EQ(recorder.spec.logo[0].name, "NintendoLogo.png");
    EXPECT_TRUE(builder(recorder.packager()).checkPrerequisites().ok());
}

TEST_F(ForwarderBuilderTest, NpdmThatIsNotOneIsReported) {
    write("/switch/RetroManager/stub/main.npdm", "garbage");
    EXPECT_EQ(builder(recorder.packager()).build(gbaGame(), cancel).issue, ForwarderIssue::StubInvalid);
}

TEST_F(ForwarderBuilderTest, GameMustBeInstalled) {
    ASSERT_TRUE(fs.remove(kRom).ok());
    ForwarderReport report = builder(recorder.packager()).build(gbaGame(), cancel);
    EXPECT_EQ(report.issue, ForwarderIssue::RomMissing);
    EXPECT_EQ(report.detail, kRom);
}

TEST_F(ForwarderBuilderTest, CoreMissingListsTheCandidates) {
    ASSERT_TRUE(fs.remove(kCore).ok());
    ForwarderReport report = builder(recorder.packager()).build(gbaGame(), cancel);
    EXPECT_EQ(report.issue, ForwarderIssue::CoreMissing);
    EXPECT_EQ(report.detail, "/retroarch/cores");
    ASSERT_FALSE(report.cores.empty());
    EXPECT_EQ(report.cores[0], "mgba_libretro_libnx.nro");
}

TEST_F(ForwarderBuilderTest, PrefersTheFirstInstalledCandidate) {
    ASSERT_TRUE(fs.remove(kCore).ok());
    write("/retroarch/cores/gpsp_libretro_libnx.nro", "CORE");
    write("/retroarch/cores/vba_next_libretro_libnx.nro", "CORE");
    ForwarderBuilder b = builder(recorder.packager());
    EXPECT_EQ(b.installedCore("gba").value(), "/retroarch/cores/vba_next_libretro_libnx.nro");
    write(kCore, "CORE");
    EXPECT_EQ(b.installedCore("gba").value(), kCore);
    EXPECT_EQ(ForwarderBuilder::coreCandidates("snes")[0], "snes9x");
    EXPECT_TRUE(ForwarderBuilder::coreCandidates("unknown").empty());
}

TEST_F(ForwarderBuilderTest, NoBoxArtMeansAPlaceholderIcon) {
    ASSERT_TRUE(fs.remove(kBoxArt).ok());
    ForwarderReport report = builder(recorder.packager()).build(gbaGame(), cancel);
    ASSERT_TRUE(report.ok());
    EXPECT_TRUE(report.placeholderIcon);
    EXPECT_EQ(forwarder::decodeImage(recorder.spec.icon).value().width, 256);

    // A box art that is not a picture falls back too.
    write(kBoxArt, "<html>404</html>");
    report = builder(recorder.packager()).build(gbaGame(), cancel);
    EXPECT_TRUE(report.ok());
    EXPECT_TRUE(report.placeholderIcon);
}

TEST_F(ForwarderBuilderTest, FileNameIsSanitized) {
    GameEntry g = gbaGame("Zelda: Minish Cap?");
    EXPECT_EQ(builder(recorder.packager()).outputPathFor(g), "/nsp/Zelda_ Minish Cap_.nsp");
}

TEST_F(ForwarderBuilderTest, CancelledBeforeWriting) {
    cancel.cancel();
    ForwarderReport report = builder(recorder.packager()).build(gbaGame(), cancel);
    EXPECT_EQ(report.issue, ForwarderIssue::Cancelled);
    EXPECT_FALSE(fs.exists("/nsp/Tom & Jerry.nsp"));
}

TEST_F(ForwarderBuilderTest, ReadOnlyCardIsAWriteFailure) {
    ForwarderBuilder b = builder(recorder.packager());
    fs.setReadOnly(true);
    EXPECT_EQ(b.build(gbaGame(), cancel).issue, ForwarderIssue::WriteFailed);
}

TEST_F(ForwarderBuilderTest, PackagerFailureIsReported) {
    auto failing = [](const nsp::ApplicationSpec&, const nsp::NcaKeys&) -> Result<Bytes> {
        return Error{ErrorCode::IntegrityError, "self-check failed"};
    };
    ForwarderReport report = builder(failing).build(gbaGame(), cancel);
    EXPECT_EQ(report.issue, ForwarderIssue::Failed);
    EXPECT_NE(report.detail.find("self-check failed"), std::string::npos);
    EXPECT_FALSE(fs.exists("/nsp/Tom & Jerry.nsp"));
}

TEST_F(ForwarderBuilderTest, EndToEndWithTheRealPackager) {
    ForwarderReport report = builder(ForwarderBuilder::defaultPackager()).build(gbaGame(), cancel);
    ASSERT_TRUE(report.ok()) << report.detail;
    std::string file = fs.readFile(report.nspPath).value();
    EXPECT_EQ(report.sizeBytes, file.size());
    auto app = nsp::readApplicationNsp(Bytes(file.begin(), file.end()), fakeKeys());
    ASSERT_TRUE(app.ok()) << app.error().describe();
    EXPECT_EQ(app.value().titleId, nsp::forwarderTitleId(kRom));
    bool argv = false;
    for (const auto& f : app.value().romfs) argv |= f.name == "nextArgv" && str(f.data).find(kRom) != std::string::npos;
    EXPECT_TRUE(argv);
    for (const auto& f : app.value().exefs) {
        if (f.name == "main.npdm") {
            EXPECT_EQ(nsp::npdmTitleId(f.data).value(), report.titleId);
        }
    }
}

// What HOME menu needs to show the shortcut: an Application meta whose
// Control content carries control.nacp and one 256x256 JPEG icon per language.
TEST_F(ForwarderBuilderTest, HomeMenuIconChain) {
    ForwarderReport report = builder(ForwarderBuilder::defaultPackager()).build(gbaGame(), cancel);
    ASSERT_TRUE(report.ok()) << report.detail;
    EXPECT_EQ(nsp::titleIdHex(report.titleId).substr(0, 4), "0100");
    EXPECT_EQ(nsp::titleIdHex(report.titleId).substr(12), "0000");

    std::string file = fs.readFile(report.nspPath).value();
    auto app = nsp::readApplicationNsp(Bytes(file.begin(), file.end()), fakeKeys());
    ASSERT_TRUE(app.ok()) << app.error().describe();
    EXPECT_EQ(app.value().cnmt.type, nsp::kCnmtApplication);
    EXPECT_EQ(app.value().cnmt.titleId, report.titleId);
    bool hasControl = false;
    for (const auto& c : app.value().cnmt.contents) hasControl |= c.type == nsp::CnmtContentType::Control;
    EXPECT_TRUE(hasControl);

    int icons = 0;
    bool nacpOk = false;
    for (const auto& f : app.value().control) {
        if (f.name == "control.nacp") {
            nacpOk = f.data.size() == nsp::kNacpSize && nsp::nacpName(f.data) == "Tom & Jerry";
            continue;
        }
        ASSERT_EQ(f.name.rfind("icon_", 0), 0u) << f.name;
        auto image = forwarder::decodeImage(f.data);
        ASSERT_TRUE(image.ok()) << f.name;
        EXPECT_EQ(image.value().width, 256);
        EXPECT_EQ(image.value().height, 256);
        EXPECT_EQ(f.data[0], 0xFF);
        EXPECT_EQ(f.data[1], 0xD8);
        EXPECT_LE(f.data.size(), forwarder::kMaxIconBytes);
        ++icons;
    }
    EXPECT_TRUE(nacpOk);
    EXPECT_EQ(icons, nsp::kNacpLanguages);
}
