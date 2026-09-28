#include <gtest/gtest.h>

#include "retromanager/services/SyncPlanner.hpp"

using namespace rm;

namespace {

FileState at(std::int64_t t, std::uint64_t size = 8192) { return FileState{size, t}; }

SyncAction only(const std::map<std::string, FileState>& local, const std::map<std::string, FileState>& remote,
                const std::map<std::string, SyncRecord>& previous) {
    auto plan = planSync(local, remote, previous);
    EXPECT_EQ(plan.size(), 1u);
    return plan.empty() ? SyncAction::InSync : plan[0].action;
}

const std::string kFile = "Game.srm";

}  // namespace

TEST(SyncPlanner, OneSidedFilesAreCopiedAcross) {
    EXPECT_EQ(only({{kFile, at(100)}}, {}, {}), SyncAction::Upload);
    EXPECT_EQ(only({}, {{kFile, at(100)}}, {}), SyncAction::Download);
}

TEST(SyncPlanner, DeletionsAreNotPropagated) {
    SyncRecord synced{at(100), at(200)};
    // Deleted on the NAS: re-upload rather than delete the local save.
    EXPECT_EQ(only({{kFile, at(100)}}, {}, {{kFile, synced}}), SyncAction::Upload);
    // Deleted locally: bring it back rather than delete the NAS copy.
    EXPECT_EQ(only({}, {{kFile, at(200)}}, {{kFile, synced}}), SyncAction::Download);
}

TEST(SyncPlanner, UnchangedSinceLastSyncMeansInSyncWhateverTheClocksSay) {
    // After an upload the NAS copy carries the upload time (200), later than
    // the local file (100): a naive "newer wins" would download it back.
    SyncRecord synced{at(100), at(200)};
    EXPECT_EQ(only({{kFile, at(100)}}, {{kFile, at(200)}}, {{kFile, synced}}), SyncAction::InSync);
}

TEST(SyncPlanner, ChangeOnOneSideIsPropagated) {
    SyncRecord synced{at(100), at(200)};
    EXPECT_EQ(only({{kFile, at(150)}}, {{kFile, at(200)}}, {{kFile, synced}}), SyncAction::Upload);
    EXPECT_EQ(only({{kFile, at(100)}}, {{kFile, at(300)}}, {{kFile, synced}}), SyncAction::Download);
    // A change of size alone (same second) counts as a change.
    EXPECT_EQ(only({{kFile, at(100, 9000)}}, {{kFile, at(200)}}, {{kFile, synced}}), SyncAction::Upload);
    // Even an "older" local time is a change (clock set back, file restored).
    EXPECT_EQ(only({{kFile, at(50)}}, {{kFile, at(200)}}, {{kFile, synced}}), SyncAction::Upload);
}

TEST(SyncPlanner, BothChangedIsAConflictWonByTheNewest) {
    SyncRecord synced{at(100), at(200)};
    EXPECT_EQ(only({{kFile, at(500)}}, {{kFile, at(400)}}, {{kFile, synced}}), SyncAction::ConflictKeepLocal);
    EXPECT_EQ(only({{kFile, at(300)}}, {{kFile, at(400)}}, {{kFile, synced}}), SyncAction::ConflictKeepRemote);
    // Same time but different content: tie goes to the console, NAS copy kept.
    EXPECT_EQ(only({{kFile, at(400, 1)}}, {{kFile, at(400, 2)}}, {{kFile, synced}}), SyncAction::ConflictKeepLocal);
    // Both sides changed into the same thing (synced by another device): converged.
    EXPECT_EQ(only({{kFile, at(400)}}, {{kFile, at(400)}}, {{kFile, synced}}), SyncAction::InSync);
}

TEST(SyncPlanner, FirstSyncRecognisesIdenticalFiles) {
    EXPECT_EQ(only({{kFile, at(1000)}}, {{kFile, at(1000)}}, {}), SyncAction::InSync);
    EXPECT_EQ(only({{kFile, at(1001)}}, {{kFile, at(1000)}}, {}), SyncAction::InSync);  // FAT's 2 s steps
    EXPECT_EQ(only({{kFile, at(1002)}}, {{kFile, at(1000)}}, {}), SyncAction::InSync);
}

TEST(SyncPlanner, FirstSyncWithTwoDifferentVersionsKeepsBoth) {
    EXPECT_EQ(only({{kFile, at(2000)}}, {{kFile, at(1000)}}, {}), SyncAction::ConflictKeepLocal);
    EXPECT_EQ(only({{kFile, at(1000)}}, {{kFile, at(2000)}}, {}), SyncAction::ConflictKeepRemote);
    EXPECT_EQ(only({{kFile, at(1000, 1)}}, {{kFile, at(1000, 2)}}, {}), SyncAction::ConflictKeepLocal);  // same time, other content
}

TEST(SyncPlanner, PlansEveryFileInPathOrder) {
    auto plan = planSync({{"b.srm", at(1)}, {"a/x.sav", at(1)}, {"c.srm", at(5)}},
                         {{"b.srm", at(1)}, {"d.dsv", at(1)}, {"c.srm", at(5)}}, {{"c.srm", {at(5), at(5)}}});
    std::vector<PlannedAction> expected = {
        {"a/x.sav", SyncAction::Upload},
        {"b.srm", SyncAction::InSync},
        {"c.srm", SyncAction::InSync},
        {"d.dsv", SyncAction::Download},
    };
    EXPECT_EQ(plan, expected);
}

TEST(SyncPlanner, StaleRecordsOfVanishedFilesAreIgnored) {
    EXPECT_TRUE(planSync({}, {}, {{"gone.srm", {at(1), at(1)}}}).empty());
}

// --- sync-state.json ---------------------------------------------------------

TEST(SyncStateFile, RoundTrips) {
    SyncState state;
    state.remoteUrl = "ftp://nas.local/Saves/";
    state.files["Game.srm"] = {at(100, 8192), at(200, 8192)};
    state.files["mGBA/Pokémon.sav"] = {at(5, 1), at(6, 1)};

    auto parsed = parseSyncState(serializeSyncState(state));
    ASSERT_TRUE(parsed.ok()) << parsed.error().describe();
    EXPECT_EQ(parsed.value().remoteUrl, state.remoteUrl);
    ASSERT_EQ(parsed.value().files.size(), 2u);
    EXPECT_EQ(parsed.value().files.at("Game.srm").local, at(100, 8192));
    EXPECT_EQ(parsed.value().files.at("mGBA/Pokémon.sav").remote, at(6, 1));
}

TEST(SyncStateFile, RejectsCorruptedDocuments) {
    EXPECT_FALSE(parseSyncState("").ok());
    EXPECT_FALSE(parseSyncState("{\"files\": 3}").ok());
    EXPECT_FALSE(parseSyncState(R"({"files": {"a": {"local": {"size": "x"}}}})").ok());
    EXPECT_EQ(parseSyncState(R"({"version": 9, "files": {}})").error().code, ErrorCode::Unsupported);
}
