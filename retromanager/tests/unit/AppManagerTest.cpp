#include <gtest/gtest.h>

#include <optional>

#include "MemoryFileSystem.hpp"
#include "MockSdCard.hpp"
#include "retromanager/core/Crc32.hpp"
#include "retromanager/network/MockRemoteSource.hpp"
#include "retromanager/services/AppManager.hpp"
#include "retromanager/services/DownloadQueueManager.hpp"

using namespace rm;

namespace {

const std::string kNroUrl = "ftp://mock.local/shop/apps/RetroArch/retroarch_switch.nro";
const std::string kIconUrl = "ftp://mock.local/shop/apps/RetroArch/icon.jpg";
const char* kNro = "/switch/RetroArch/RetroArch.nro";
const char* kIcon = "/switch/RetroArch/RetroArch.jpg";  // hbmenu: same name as the .nro
const char* kIconAlias = "/switch/RetroArch/icon.jpg";

// An NRO starts with a 16-byte "start" block, then the "NRO0" header.
std::string nro(const std::string& body = "RetroArch 1.19.1") { return std::string(0x10, '\0') + "NRO0" + body; }
const std::string kJpeg = std::string("\xFF\xD8\xFF\xE0", 4) + std::string(500, 'j');
const std::string kPng = std::string("\x89PNG\r\n\x1a\n", 8) + std::string(100, 'p');

AppEntry retroArch(std::string version = "1.19.1") {
    AppEntry app;
    app.id = "app/RetroArch";
    app.title = "RetroArch";
    app.author = "libretro";
    app.version = std::move(version);
    app.folder = "RetroArch";
    app.nroUrl = kNroUrl;
    app.iconUrl = kIconUrl;
    return app;
}

class NullSystem : public ISystem {
  public:
    void setKeepAwake(bool) override {}
};

struct Fixture {
    std::unique_ptr<test::MemoryFileSystem> sd = test::makeMockSdCard();
    MockRemoteSource shop{"{}", ""};
    ImmediateTaskRunner mainThread;
    EventBus bus{mainThread};
    RomStore store{*sd, SdLayout{}};
    NullSystem system;
    DownloadQueueManager downloads{shop, store, bus, system, std::make_unique<ImmediateTaskRunner>()};
    AppManager apps{*sd, SdLayout{}, shop};
    std::optional<DownloadFinished> finished;
    EventBus::Subscription sub = bus.subscribe<DownloadFinished>([this](const DownloadFinished& e) { finished = e; });

    Fixture() {
        shop.addFile(kNroUrl, nro());
        shop.addFile(kIconUrl, kJpeg);
    }

    const DownloadFinished& install(const AppEntry& app) {
        finished.reset();
        downloads.start(apps.job(app));
        EXPECT_TRUE(finished.has_value());
        return *finished;
    }
};

const StepOutcome* step(const DownloadFinished& event, const std::string& id) {
    for (const StepOutcome& s : event.steps) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

}  // namespace

TEST(AppManager, InstallsInTheStandardHomebrewLayout) {
    Fixture f;
    EXPECT_EQ(f.apps.folderFor(retroArch()).value(), "/switch/RetroArch");
    EXPECT_EQ(f.apps.nroPathFor(retroArch()).value(), kNro);
    EXPECT_EQ(f.apps.iconPathFor(retroArch()).value(), kIcon);
    EXPECT_EQ(f.apps.iconAliasPathFor(retroArch()).value(), kIconAlias);

    AppEntry evil = retroArch();
    evil.folder = "..";
    EXPECT_FALSE(f.apps.nroPathFor(evil).ok());
    evil.folder = "a/b";
    EXPECT_FALSE(f.apps.nroPathFor(evil).ok());
}

TEST(AppManager, DownloadsTheNroAndItsIcon) {
    Fixture f;
    EXPECT_EQ(f.apps.state(retroArch()), AppState::NotInstalled);
    EXPECT_EQ(f.apps.job(retroArch()).kind, DownloadKind::App);

    const DownloadFinished& done = f.install(retroArch());

    ASSERT_TRUE(done.result.ok()) << done.result.error().describe();
    EXPECT_EQ(done.destination, kNro);
    EXPECT_EQ(done.itemId, "app/RetroArch");
    EXPECT_EQ(f.sd->readFile(kNro).value(), nro());
    EXPECT_EQ(f.sd->readFile(kIcon).value(), kJpeg);
    EXPECT_EQ(f.sd->readFile(kIconAlias).value(), kJpeg);  // both conventions
    ASSERT_NE(step(done, "icon"), nullptr);
    EXPECT_TRUE(step(done, "icon")->result.ok());
    EXPECT_EQ(f.apps.state(retroArch()), AppState::Installed);
    EXPECT_EQ(f.apps.installedVersion(retroArch()), "1.19.1");
}

TEST(AppManager, WithoutIconOnlyTheNroIsInstalled) {
    Fixture f;
    AppEntry app = retroArch();
    app.iconUrl.clear();
    const DownloadFinished& done = f.install(app);
    ASSERT_TRUE(done.result.ok());
    EXPECT_EQ(step(done, "icon"), nullptr);
    EXPECT_FALSE(f.sd->exists(kIcon));
    EXPECT_FALSE(f.sd->exists(kIconAlias));
}

TEST(AppManager, SomethingThatIsNotAnNroIsNeverInstalled) {
    Fixture f;
    f.shop.addFile(kNroUrl, "<html><body>404 Not Found</body></html>");
    const DownloadFinished& done = f.install(retroArch());
    EXPECT_EQ(done.result.error().code, ErrorCode::IntegrityError);
    EXPECT_FALSE(f.sd->exists(kNro));
    EXPECT_FALSE(f.sd->exists("/switch/RetroArch"));  // no empty folder left in hbmenu
    EXPECT_FALSE(f.sd->exists(kIcon));
    EXPECT_EQ(f.apps.state(retroArch()), AppState::NotInstalled);
}

TEST(AppManager, AFailedUpdateKeepsThePreviousVersionAndItsFolder) {
    Fixture f;
    ASSERT_TRUE(f.install(retroArch("1.19.0")).result.ok());
    f.shop.addFile(kNroUrl, "truncated");
    EXPECT_FALSE(f.install(retroArch("1.19.1")).result.ok());
    EXPECT_EQ(f.sd->readFile(kNro).value(), nro());
    EXPECT_EQ(f.apps.installedVersion(retroArch()), "1.19.0");
}

TEST(AppManager, VerifiesTheAnnouncedCrc) {
    Fixture f;
    AppEntry app = retroArch();
    app.crc32 = "00000000";
    EXPECT_EQ(f.install(app).result.error().code, ErrorCode::IntegrityError);
    EXPECT_FALSE(f.sd->exists(kNro));

    Crc32 crc;
    const std::string content = nro();
    crc.update(content.data(), content.size());
    app.crc32 = crc.hex();
    EXPECT_TRUE(f.install(app).result.ok());
}

TEST(AppManager, IconProblemsNeverUndoTheInstall) {
    Fixture f;
    f.shop.addFile(kIconUrl, kPng);  // hbmenu only reads JPEG icons
    const DownloadFinished& png = f.install(retroArch());
    ASSERT_TRUE(png.result.ok());
    EXPECT_EQ(step(png, "icon")->result.error().code, ErrorCode::Unsupported);
    EXPECT_FALSE(f.sd->exists(kIcon));
    EXPECT_TRUE(f.sd->isFile(kNro));

    AppEntry missingIcon = retroArch();
    missingIcon.iconUrl = "ftp://mock.local/shop/apps/RetroArch/nope.jpg";
    const DownloadFinished& missing = f.install(missingIcon);
    ASSERT_TRUE(missing.result.ok());
    EXPECT_EQ(step(missing, "icon")->result.error().code, ErrorCode::NotFound);
}

TEST(AppManager, UnknownSizeIsAskedToTheServerForTheSpaceCheck) {
    Fixture f;
    f.shop.addFile(kNroUrl, nro(std::string(5 * 1024 * 1024, 'x')));
    f.sd->setCapacity(f.sd->usedBytes() + 3 * 1024 * 1024);
    AppEntry app = retroArch();
    ASSERT_EQ(app.sizeBytes, 0u);  // the index did not say

    const DownloadFinished& done = f.install(app);

    EXPECT_EQ(done.result.error().code, ErrorCode::InsufficientSpace);
    EXPECT_GT(done.space.requiredBytes, 5u * 1024 * 1024);
    EXPECT_EQ(f.shop.downloadCount(), 0);  // refused before downloading anything
    EXPECT_FALSE(f.sd->exists("/switch/RetroArch"));
}

TEST(AppManager, DetectsUpdates) {
    Fixture f;
    ASSERT_TRUE(f.install(retroArch("1.19.0")).result.ok());
    EXPECT_EQ(f.apps.state(retroArch("1.19.0")), AppState::Installed);
    EXPECT_EQ(f.apps.state(retroArch("1.19.1")), AppState::UpdateAvailable);
    EXPECT_EQ(f.apps.state(retroArch("")), AppState::Installed);  // index without version: nothing to compare

    ASSERT_TRUE(f.install(retroArch("1.19.1")).result.ok());
    EXPECT_EQ(f.apps.state(retroArch("1.19.1")), AppState::Installed);

    auto states = f.apps.states({retroArch("1.20.0"), [] {
                                     AppEntry other = retroArch();
                                     other.id = "app/pNES";
                                     other.folder = "pNES";
                                     return other;
                                 }()});
    EXPECT_EQ(states.at("app/RetroArch"), AppState::UpdateAvailable);
    EXPECT_EQ(states.at("app/pNES"), AppState::NotInstalled);
}

TEST(AppManager, HomebrewsInstalledByHandCountAsInstalled) {
    Fixture f;
    ASSERT_TRUE(f.sd->createDirectories("/switch/RetroArch").ok());
    ASSERT_TRUE(f.sd->writeFile(kNro, nro()).ok());
    EXPECT_EQ(f.apps.state(retroArch()), AppState::Installed);  // version unknown: no update claimed
    EXPECT_EQ(f.apps.installedVersion(retroArch()), std::nullopt);

    // Deleted by hand after an install: not installed any more.
    ASSERT_TRUE(f.install(retroArch()).result.ok());
    ASSERT_TRUE(f.sd->remove(kNro).ok());
    EXPECT_EQ(f.apps.state(retroArch()), AppState::NotInstalled);
}

TEST(AppManager, CorruptedRecordFileIsIgnoredAndRewritten) {
    Fixture f;
    ASSERT_TRUE(f.sd->writeFile(f.apps.recordPath(), "{ not json").ok());
    EXPECT_EQ(f.apps.state(retroArch()), AppState::NotInstalled);
    const DownloadFinished& done = f.install(retroArch());
    ASSERT_TRUE(done.result.ok());
    EXPECT_EQ(step(done, "record"), nullptr);  // only reported when it fails
    EXPECT_EQ(f.apps.installedVersion(retroArch()), "1.19.1");
}

TEST(AppManager, NroHeaderValidation) {
    EXPECT_TRUE(AppManager::validateNroHeader(nro()).ok());
    EXPECT_FALSE(AppManager::validateNroHeader(std::string(0x10, '\0') + "NSO0").ok());
    EXPECT_TRUE(AppManager::validateIcon(kJpeg).ok());
    EXPECT_EQ(AppManager::validateIcon(kPng).error().code, ErrorCode::Unsupported);
    EXPECT_EQ(AppManager::validateIcon("<html>").error().code, ErrorCode::IntegrityError);
}
