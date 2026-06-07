#pragma once

#include <csignal>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace windmi::platform {

/** Install Ctrl+C / termination handlers and set *running_flag = 0 on shutdown request. */
void install_signal_handlers(volatile sig_atomic_t* running_flag);

/** Acquire exclusive instance lock. Returns false when another instance is running. */
bool acquire_instance_lock(bool force = false);

/** Release instance lock if currently held. Safe to call more than once. */
void release_instance_lock();

/** Check whether a PID appears alive on the current platform. */
bool is_pid_alive(int pid);

/** Resolve static directory as-is, relative to executable, or one level above executable. */
std::string resolve_static_dir(const std::string& dir);

/** Cross-platform millisecond sleep. */
void sleep_ms(unsigned int ms);

/** Test hook: override lock path/name so tests do not use the production lock. */
void set_instance_lock_name_for_test(const std::string& lock_name);

/** Test hook: clear lock override. */
void clear_instance_lock_name_for_test();

}  // namespace windmi::platform

// ─────────────────────────────────────────────────────────────────────
// Platform Abstraction for Threading/Mutex
// ─────────────────────────────────────────────────────────────────────

namespace windmi {

/**
 * @brief Platform-agnostic mutex wrapper
 */
class Mutex {
public:
    Mutex();
    ~Mutex();
    void lock();
    void unlock();
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

private:
#ifdef _WIN32
    CRITICAL_SECTION handle_;
#else
    pthread_mutex_t mutex_;
#endif
};

/**
 * @brief RAII lock guard for Mutex
 */
class LockGuard {
public:
    explicit LockGuard(Mutex& mutex) : mutex_(mutex) { mutex_.lock(); }
    ~LockGuard() { mutex_.unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    Mutex& mutex_;
};

}  // namespace windmi
