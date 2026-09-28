#include "retromanager/network/MockRemoteSource.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>

#include "retromanager/core/Url.hpp"

namespace rm {

namespace {

struct DemoGame {
    const char* title;
    const char* system;
    const char* region;
    std::uint64_t size;
    const char* path;  // relative to the index, percent-encoded
    int year;
    const char* boxart;
    const char* cheat = nullptr;  // relative .cht URL
};

// Single source of truth for the demo shop: the index and the synthetic
// files are both generated from this table.
const std::vector<DemoGame>& demoGames() {
    static const std::vector<DemoGame> games = {
        {"Pokémon Platine", "nds", "EUR", 134217728, "roms/nds/Pokemon%20Platine%20(France).nds", 2009,
         "boxart/nds/pokemon-platine.png", "cheats/nds/Pokemon%20Platine%20(France).cht"},
        {"Mario Kart DS", "nds", "EUR", 33554432, "roms/nds/Mario%20Kart%20DS%20(Europe).nds", 2005, nullptr,
         "cheats/nds/Mario%20Kart%20DS%20(Europe).cht"},
        {"The Legend of Zelda: A Link to the Past", "snes", "USA", 1048576,
         "roms/snes/Legend%20of%20Zelda,%20The%20-%20A%20Link%20to%20the%20Past%20(USA).sfc", 1991, nullptr},
        {"Super Mario World", "snes", "USA", 524288, "roms/snes/Super%20Mario%20World%20(USA).sfc", 1990, nullptr,
         "cheats/snes/Super%20Mario%20World%20(USA).cht"},
        {"Super Metroid", "snes", "EUR", 3145728, "roms/snes/Super%20Metroid%20(Europe).sfc", 1994, nullptr},
        {"Chrono Trigger", "snes", "USA", 4194304, "roms/snes/Chrono%20Trigger%20(USA).sfc", 1995, nullptr},
        {"Advance Wars", "gba", "USA", 8388608, "roms/gba/Advance%20Wars%20(USA).gba", 2001, nullptr},
        {"Metroid Fusion", "gba", "EUR", 8388608, "roms/gba/Metroid%20Fusion%20(Europe).gba", 2002, nullptr},
        {"Pokémon Émeraude", "gba", "EUR", 16777216, "roms/gba/Pokemon%20Emeraude%20(France).gba", 2005, nullptr},
        {"Tetris", "gb", "WLD", 32768, "roms/gb/Tetris%20(World).gb", 1989, nullptr},
        {"The Legend of Zelda: Link's Awakening DX", "gbc", "EUR", 1048576,
         "roms/gbc/Legend%20of%20Zelda,%20The%20-%20Link's%20Awakening%20DX%20(Europe).gbc", 1998, nullptr},
        {"Sonic the Hedgehog", "megadrive", "EUR", 524288, "roms/megadrive/Sonic%20The%20Hedgehog%20(USA,%20Europe).md",
         1991, nullptr},
        {"Streets of Rage 2", "megadrive", "EUR", 2097152, "roms/megadrive/Streets%20of%20Rage%202%20(Europe).md", 1992,
         nullptr},
        {"Super Mario Bros. 3", "nes", "USA", 393232, "roms/nes/Super%20Mario%20Bros.%203%20(USA).nes", 1990, nullptr},
        {"Castlevania", "nes", "EUR", 131088, "roms/nes/Castlevania%20(Europe).nes", 1988, nullptr},
    };
    return games;
}

}  // namespace

std::string MockRemoteSource::demoIndex() {
    nlohmann::ordered_json index;
    index["version"] = 1;
    index["name"] = "Boutique de démonstration";
    index["success"] = "Bienvenue sur la boutique de démonstration RetroManager !";
    index["games"] = nlohmann::ordered_json::array();
    for (const DemoGame& game : demoGames()) {
        nlohmann::ordered_json entry;
        entry["title"] = game.title;
        entry["system"] = game.system;
        entry["region"] = game.region;
        entry["size"] = game.size;
        entry["url"] = game.path;
        if (game.boxart != nullptr) entry["boxart"] = game.boxart;
        if (game.cheat != nullptr) entry["cheat_url"] = game.cheat;
        entry["year"] = game.year;
        index["games"].push_back(entry);
    }
    return index.dump(2);
}

char MockRemoteSource::syntheticByte(std::uint64_t offset) {
    // Cheap, deterministic, not trivially compressible.
    std::uint64_t x = offset * 0x9E3779B97F4A7C15ull;
    return static_cast<char>((x >> 29) ^ (x >> 47));
}

MockRemoteSource::MockRemoteSource() : document_(demoIndex()), indexUrl_(kDemoIndexUrl) {
    for (const DemoGame& game : demoGames()) {
        auto url = url::resolve(indexUrl_, game.path);
        if (url) addSyntheticFile(url.value(), game.size);
        if (game.cheat != nullptr) {
            auto cheatUrl = url::resolve(indexUrl_, game.cheat);
            if (cheatUrl) {
                addFile(cheatUrl.value(), std::string("cheats = 2\n\n") + "cheat0_desc = \"" + game.title +
                                              " - Demo cheat 1\"\ncheat0_code = \"00000000+00000000\"\ncheat0_enable = false\n\n" +
                                              "cheat1_desc = \"" + game.title +
                                              " - Demo cheat 2\"\ncheat1_code = \"00000000+00000001\"\ncheat1_enable = false\n");
            }
        }
    }
}

MockRemoteSource::MockRemoteSource(std::string document, std::string indexUrl)
    : document_(std::move(document)), indexUrl_(std::move(indexUrl)) {}

std::string MockRemoteSource::describe() const { return "mock: " + indexUrl_; }

Result<std::string> MockRemoteSource::fetchIndex() {
    ++fetchCount_;
    if (latency_.count() > 0) std::this_thread::sleep_for(latency_);
    if (failure_) return *failure_;
    return document_;
}

void MockRemoteSource::addFile(const std::string& url, std::string content) {
    File file;
    file.size = content.size();
    file.content = std::move(content);
    files_[url] = std::move(file);
}

void MockRemoteSource::addSyntheticFile(const std::string& url, std::uint64_t size) {
    File file;
    file.size = size;
    file.synthetic = true;
    files_[url] = std::move(file);
}

Status MockRemoteSource::downloadFile(const std::string& url, const ChunkSink& sink, const ProgressCallback& progress,
                                      const CancellationToken& cancel) {
    ++downloadCount_;
    if (latency_.count() > 0) std::this_thread::sleep_for(latency_);
    if (failure_) return *failure_;

    auto it = files_.find(url);
    if (it == files_.end()) return makeError(ErrorCode::NotFound, "mock: no such file " + url);
    const File& file = it->second;

    std::vector<char> chunk(chunkSize_);
    std::uint64_t sent = 0;
    auto begin = std::chrono::steady_clock::now();
    while (sent < file.size) {
        if (cancel.isCancelled()) return makeError(ErrorCode::Cancelled, "download cancelled");
        if (dropAfter_ && sent >= *dropAfter_) return dropError_;

        std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(chunkSize_, file.size - sent));
        if (file.synthetic) {
            for (std::size_t i = 0; i < n; ++i) chunk[i] = syntheticByte(sent + i);
        } else {
            std::copy_n(file.content.data() + sent, n, chunk.data());
        }
        if (Status accepted = sink(chunk.data(), n); !accepted) return accepted;
        sent += n;
        if (progress) progress(TransferProgress{sent, file.size});

        if (throughput_ > 0) {  // sleep until the simulated link would have carried `sent` bytes
            auto due = begin + std::chrono::microseconds(sent * 1000000 / throughput_);
            std::this_thread::sleep_until(due);
        }
    }
    return success();
}

}  // namespace rm
