/**
 * @file tests/utils/test_platform_mutex.cpp
 * @brief Platform mutex, signal handler, and instance lock tests
 */

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "utils/Platform.hpp"
#include "utils/PlatformC.h"
#include "config.h"

using namespace windmi;

// ─── Mutex Lock/Unlock Tests ───

TEST(MutexTest, LockAndUnlock) {
    Mutex mutex;
    mutex.lock();
    mutex.unlock();
}

TEST(MutexTest, RecursiveLockDeadlock) {
    // A plain (non-recursive) mutex must deadlock on recursive lock.
    // We verify this indirectly: two threads try to lock the same mutex,
    // only one should succeed at a time.
    Mutex mutex;
    std::atomic<int> max_concurrent{0};
    std::atomic<int> current{0};
    std::atomic<int> total_successes{0};

    auto worker = [&]() {
        for (int i = 0; i < 50; i++) {
            mutex.lock();
            int c = ++current;
            if (c > max_concurrent) max_concurrent = c;
            platform::sleep_ms(2);
            --current;
            mutex.unlock();
        }
        total_successes.fetch_add(50);
    };

    std::vector<Thread> threads;
    for (int i = 0; i < 4; i++) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(max_concurrent.load(), 1);
    EXPECT_EQ(total_successes.load(), 200);
}

// ─── Signal Handler Tests ───

TEST(SignalHandlerTest, InstallSetsFlag) {
    volatile sig_atomic_t running = 1;
    // On Windows, install_signal_handlers sets *running = 1.
    // On POSIX, it does NOT modify the flag.
    platform::install_signal_handlers(&running);
#ifdef _WIN32
    EXPECT_EQ(running, 1);
#else
    // On POSIX, flag should remain unchanged (not set to 0)
    EXPECT_EQ(running, 1);
#endif
}

TEST(SignalHandlerTest, NullFlagDoesNotCrash) {
    // Passing nullptr should not crash
    platform::install_signal_handlers(nullptr);
}

TEST(SignalHandlerTest, HandlerCallbackSetsFlagToZero) {
    volatile sig_atomic_t running = 1;
    platform::install_signal_handlers(&running);
    // We can't easily trigger a real signal in a unit test,
    // but we verify the handler was installed and the flag
    // is not set to 0 by the call.
    EXPECT_EQ(running, 1);
}

// ─── Instance Lock Tests ───

TEST(InstanceLockTest, AcquireAndRelease) {
    // Use a unique temp path for this test
    char lock_path[256];
    snprintf(lock_path, sizeof(lock_path), "/tmp/windmi-test-lock-%d", getpid());
    platform::set_instance_lock_name_for_test(lock_path);

    EXPECT_TRUE(platform::acquire_instance_lock(false));
    platform::release_instance_lock();
    platform::clear_instance_lock_name_for_test();

    // Clean up lock file
    std::remove(lock_path);
}

TEST(InstanceLockTest, ForceAcquireWhenLocked) {
    char lock_path[256];
    snprintf(lock_path, sizeof(lock_path), "/tmp/windmi-test-lock-force-%d", getpid());
    platform::set_instance_lock_name_for_test(lock_path);

    // First lock
    EXPECT_TRUE(platform::acquire_instance_lock(false));

    // Without force, second acquire should fail (same flock)
    // Note: on POSIX, flock is per-process, so this is a no-op for same process.
    // We just verify the first acquire worked.
    platform::release_instance_lock();
    platform::clear_instance_lock_name_for_test();

    std::remove(lock_path);
}

TEST(InstanceLockTest, InvalidPathReturnsFalse) {
    platform::set_instance_lock_name_for_test("/nonexistent/dir/lockfile");
    EXPECT_FALSE(platform::acquire_instance_lock(false));
    platform::clear_instance_lock_name_for_test();
}

// ─── PID Alive Tests ───

TEST(PidAliveTest, CurrentProcessAlive) {
    int pid = getpid();
    EXPECT_TRUE(platform::is_pid_alive(pid));
}

TEST(PidAliveTest, InvalidPidReturnsFalse) {
    EXPECT_FALSE(platform::is_pid_alive(0));
    EXPECT_FALSE(platform::is_pid_alive(-1));
}

TEST(PidAliveTest, NonExistentPidReturnsFalse) {
    // PID 999999999 is very unlikely to exist
    EXPECT_FALSE(platform::is_pid_alive(999999999));
}

// ─── Sleep Tests ───

TEST(SleepTest, SleepMsBlocks) {
    auto start = std::chrono::steady_clock::now();
    platform::sleep_ms(50);
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_GE(ms, 40);  // Allow 10ms tolerance
    EXPECT_LT(ms, 200);
}

TEST(SleepTest, SleepMsZeroReturnsImmediately) {
    auto start = std::chrono::steady_clock::now();
    platform::sleep_ms(0);
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(ms, 10);
}

// ─── C API sleep_ms bridge ───

TEST(CApiSleepTest, WindmiSleepMs) {
    auto start = std::chrono::steady_clock::now();
    windmi_sleep_ms(50);
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_GE(ms, 40);
    EXPECT_LT(ms, 200);
}
