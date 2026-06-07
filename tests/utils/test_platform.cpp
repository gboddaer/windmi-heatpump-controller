#include <gtest/gtest.h>
#include <csignal>
#include <string>

#include "utils/Platform.hpp"

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

TEST(PlatformTest, InstallSignalHandlers) {
    volatile sig_atomic_t running = 1;
    windmi::platform::install_signal_handlers(&running);
    // On POSIX, this would test that signal handlers are properly installed.
    // On Windows, this would test that SetConsoleCtrlHandler is properly registered.
    // For now, just verify the function exists and can be called.
    EXPECT_EQ(running, 1);
}

TEST(PlatformTest, AcquireAndReleaseLock) {
    // Use a temporary lock file for testing
    std::string test_lock = "/tmp/test_windmi_lock_" + std::to_string(getpid()) + ".lock";
    windmi::platform::set_instance_lock_name_for_test(test_lock);
    
    bool result = windmi::platform::acquire_instance_lock(false);
    EXPECT_TRUE(result);
    
    windmi::platform::release_instance_lock();
    
    windmi::platform::clear_instance_lock_name_for_test();
}

TEST(PlatformTest, IsPidAlive) {
    // Test with current process PID
    int current_pid = getpid();
    bool result = windmi::platform::is_pid_alive(current_pid);
    EXPECT_TRUE(result);
    
    // Test with invalid PID
    result = windmi::platform::is_pid_alive(-1);
    EXPECT_FALSE(result);
}

TEST(PlatformTest, SleepMs) {
    auto start = std::chrono::steady_clock::now();
    windmi::platform::sleep_ms(100);
    auto end = std::chrono::steady_clock::now();
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    // Allow some tolerance for system timing
    EXPECT_GE(elapsed, 90);
    EXPECT_LE(elapsed, 200);
}

TEST(PlatformTest, ResolveStaticDir) {
    // Test with current directory
    std::string result = windmi::platform::resolve_static_dir(".");
    EXPECT_FALSE(result.empty());
    
    // Test with non-existent directory (should return empty)
    result = windmi::platform::resolve_static_dir("/nonexistent/path");
    EXPECT_TRUE(result.empty());
}
