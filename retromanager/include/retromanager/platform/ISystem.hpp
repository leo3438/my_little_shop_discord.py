#pragma once

#include <memory>
#include <mutex>

namespace rm {

// Console services that are not about files: power management today,
// sys-clk and title management later.
//
// Implementations: SwitchSystem (libnx) and DesktopSystem (no-op), both in
// src/platform/<platform>/; tests use a recording fake.
class ISystem {
  public:
    virtual ~ISystem() = default;

    // While true, the console neither dims the screen nor goes to sleep
    // (libnx: appletSetMediaPlaybackState). Called by AwakeLock only.
    virtual void setKeepAwake(bool keepAwake) = 0;
};

// Does nothing (tests, platforms without power management).
class NullSystem : public ISystem {
  public:
    void setKeepAwake(bool) override {}
};

// Reference-counted keep-awake request: the console stays awake while at
// least one lock is alive (several downloads, a sync...), and is released
// when the last one goes, whatever the exit path (success, error, cancel).
class AwakeLock {
  public:
    class Holder {
      public:
        explicit Holder(ISystem& system);
        ~Holder();
        Holder(const Holder&) = delete;
        Holder& operator=(const Holder&) = delete;

        std::unique_ptr<AwakeLock> acquire();
        int activeLocks() const;

      private:
        friend class AwakeLock;
        void release();

        ISystem& system_;
        mutable std::mutex mutex_;
        int count_ = 0;
    };

    ~AwakeLock() { holder_.release(); }
    AwakeLock(const AwakeLock&) = delete;
    AwakeLock& operator=(const AwakeLock&) = delete;

  private:
    explicit AwakeLock(Holder& holder) : holder_(holder) {}
    Holder& holder_;
};

}  // namespace rm
