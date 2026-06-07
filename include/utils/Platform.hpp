#pragma once

#include <csignal>
#include <functional>
#include <string>
#include <time.h>  // for clock_gettime / CLOCK_MONOTONIC

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pthread.h>
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

    // Friend for ConditionVariable access to underlying mutex
    friend class ConditionVariable;

    // Public accessor for ConditionVariable
    pthread_mutex_t* native_handle() { return &mutex_; }

private:
    pthread_mutex_t mutex_;
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

// ─────────────────────────────────────────────────────────────────────
// Platform Abstraction for Threading
// ─────────────────────────────────────────────────────────────────────

/**
 * @brief Platform-agnostic thread wrapper
 *
 * Wraps pthread_t (POSIX) or HANDLE/_beginthreadex (Windows).
 * Takes std::function<void()> to avoid template bloat in .cpp file.
 */
class Thread {
public:
    Thread() = default;
    ~Thread();

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    Thread(Thread&& other) noexcept;
    Thread& operator=(Thread&& other) noexcept;

    /** Creates a thread that executes the given callable. */
    explicit Thread(std::function<void()> callable);

    bool joinable() const;
    void join();
    void detach();

private:
    static void* thread_entry(void* arg);

    pthread_t thread_{};
    bool joined_ = false;
    bool detached_ = false;
};

/**
 * @brief RAII unique lock for Mutex (needed for ConditionVariable)
 *
 * Simpler than std::unique_lock - just lock/unlock/owns_lock.
 * Required by ConditionVariable::wait() for RAII semantics.
 */
class UniqueLock {
public:
    explicit UniqueLock(Mutex& mutex);
    ~UniqueLock();

    void lock();
    void unlock();
    Mutex* mutex() const noexcept;
    bool owns_lock() const noexcept;

    UniqueLock(const UniqueLock&) = delete;
    UniqueLock& operator=(const UniqueLock&) = delete;

private:
    Mutex* mutex_;
    bool owns_;
};

/**
 * @brief Platform-agnostic condition variable wrapper
 *
 * Wraps pthread_cond_t (POSIX) or CONDITION_VARIABLE (Windows).
 * wait_for() takes milliseconds (not std::chrono) to avoid <chrono> issues on MinGW.
 */
class ConditionVariable {
public:
    /** Constructor - initializes condition variable with CLOCK_MONOTONIC timeout clock */
    ConditionVariable() {
        pthread_condattr_t attr;
        pthread_condattr_init(&attr);
        pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        pthread_cond_init(&cond_, &attr);
        pthread_condattr_destroy(&attr);
    }

    /** Destructor - destroys condition variable */
    ~ConditionVariable() {
        pthread_cond_destroy(&cond_);
    }

    /** Wake one waiting thread */
    void notify_one() {
        pthread_cond_signal(&cond_);
    }

    /** Wake all waiting threads */
    void notify_all() {
        pthread_cond_broadcast(&cond_);
    }

    /** Block until notified. Lock must be held on entry. */
    inline void wait(UniqueLock& lock) {
        pthread_cond_wait(&cond_, lock.mutex()->native_handle());
    }

    /** Block until notified or timeout. Returns false on timeout. */
    inline bool wait_for(UniqueLock& lock, unsigned int ms) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_nsec += (ms % 1000) * 1000000;
        ts.tv_sec += ms / 1000 + (ts.tv_nsec >= 1000000000 ? 1 : 0);
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_nsec -= 1000000000;
        }
        int rc = pthread_cond_timedwait(&cond_, lock.mutex()->native_handle(), &ts);
        return rc == 0;
    }

    /** Block until predicate returns true. Spurious-wakeup safe. */
    template<typename Predicate>
    void wait(UniqueLock& lock, Predicate pred) {
        while (!pred()) wait(lock);
    }

    /** Block until predicate returns true or timeout. */
    template<typename Predicate>
    bool wait_for(UniqueLock& lock, unsigned int ms, Predicate pred) {
        while (!pred()) {
            if (!wait_for(lock, ms)) return pred();
        }
        return true;
    }

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

private:
    pthread_cond_t cond_;
};

}  // namespace windmi
