#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

namespace rm::test {

// A unique host directory, deleted with its content on destruction.
class TempDir {
  public:
    TempDir() {
        static std::atomic<unsigned> counter{0};
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("retromanager-test-" + std::to_string(stamp) + "-" + std::to_string(counter++));
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

}  // namespace rm::test
