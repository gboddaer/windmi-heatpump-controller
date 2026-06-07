#pragma once

#include <cstddef>
#include <cstdint>
#include <csignal>
#include <functional>
#include <string>
#include <pthread.h>

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

/** Platform abstraction for serial port (Modbus RTU over RS-485). */
class SerialPort {
public:
    SerialPort();
    ~SerialPort();

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    SerialPort(SerialPort&& other) noexcept;
    SerialPort& operator=(SerialPort&& other) noexcept;

    /** Open serial device with specified settings. Returns false on failure. */
    bool open(const std::string& device, int baud, char parity, int stop_bits, bool rs485_enabled);

    /** Close serial device. Safe to call multiple times. */
    void close();

    /** Check if device is open. */
    bool isOpen() const;

    /** Flush input buffer. */
    void flush();

    /** Read bytes from serial port. Returns actual bytes read (0 = timeout, -1 = error). */
    int read(uint8_t* buffer, size_t len, unsigned int timeout_ms);

    /** Write bytes to serial port. Returns actual bytes written (-1 = error). */
    int write(const uint8_t* buffer, size_t len);

private:
#ifdef _WIN32
    void* handle_;  // HANDLE (void*) on Windows
    bool rs485_enabled_;
#else
    int fd_;
#endif
    bool open_;
};

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

private:
    pthread_mutex_t* native_handle() { return &mutex_; }

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
    ConditionVariable();

    /** Destructor - destroys condition variable */
    ~ConditionVariable();

    /** Wake one waiting thread */
    void notify_one();

    /** Wake all waiting threads */
    void notify_all();

    /** Block until notified. Lock must be held on entry. */
    void wait(UniqueLock& lock);

    /** Block until notified or timeout. Returns false on timeout. */
    bool wait_for(UniqueLock& lock, unsigned int ms);

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
