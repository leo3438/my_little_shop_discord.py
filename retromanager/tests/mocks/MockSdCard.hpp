#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "MemoryFileSystem.hpp"
#include "retromanager/platform/IFileSystem.hpp"

namespace rm::test {

// tests/fixtures/sd_card: the reference fake SD card, shared by unit tests
// (loaded in memory) and by the desktop app (copied by tools/make_mock_sd.py).
std::filesystem::path fixtureSdCardDir();

// Copies a host directory tree into `target` under `virtualDir`.
// ".gitkeep" placeholders are skipped; their directories are kept.
Status copyHostTree(const std::filesystem::path& hostDir, IFileSystem& target, const std::string& virtualDir = "/");

// A MemoryFileSystem pre-populated with the fixture SD card. Each call
// returns an independent copy: tests may mutate it freely.
std::unique_ptr<MemoryFileSystem> makeMockSdCard();

}  // namespace rm::test
