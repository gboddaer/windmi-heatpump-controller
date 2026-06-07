#include "utils/Platform.hpp"
#include "utils/PlatformC.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>

#ifdef _WIN32
#include <io.h>
#else
#include <termios.h>
#endif

// Platform thread detection: native Windows (MSVC) vs POSIX/MinGW
#if defined(_WIN32) && !defined(__MINGW32__)
#define WINDMI_NATIVE_WINDOWS_THREADS 1
#endif

// Threading primitives: native Windows uses Win32 APIs, everything else uses pthreads
#if !defined(WINDMI_NATIVE_WINDOWS_THREADS)
#include <pthread.h>
#else
#include <process.h>
#include <windows.h>
#endif

// Platform functions: Windows (MSVC + MinGW) vs Linux
#ifdef _WIN32
#include <direct.h>
#else
#include <dirent.h>
#include <cerrno>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"

namespace windmi::platform {

static std::string g_lock_name_override;
static bool g_lock_name_override_set = false;

#ifdef _WIN32
static int g_lock_fd = -1;
static HANDLE g_lock_handle = nullptr;
#else
static int g_lock_fd = -1;
#endif

static std::string get_lock_path() {
    if (g_lock_name_override_set) return g_lock_name_override;
    return "/tmp/windmi-controller.lock";
}

void set_instance_lock_name_for_test(const std::string& lock_name) {
    g_lock_name_override = lock_name;
    g_lock_name_override_set = true;
}

void clear_instance_lock_name_for_test() {
    g_lock_name_override.clear();
    g_lock_name_override_set = false;
}

// ── Signal handlers ────────────────────────────────────────────────

#ifdef _WIN32
// Windows (MSVC + MinGW)
static volatile sig_atomic_t* g_running_flag_ptr = nullptr;
static BOOL WINAPI console_control_handler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT ||
        dwCtrlType == CTRL_CLOSE_EVENT || dwCtrlType == CTRL_LOGOFF_EVENT ||
        dwCtrlType == CTRL_SHUTDOWN_EVENT) {
        if (g_running_flag_ptr) *g_running_flag_ptr = 0;
        return TRUE;
    }
    return FALSE;
}
#else
// Linux
static volatile sig_atomic_t* g_running_flag_ptr = nullptr;
static void signal_handler(int) {
    if (g_running_flag_ptr) *g_running_flag_ptr = 0;
}
#endif

void install_signal_handlers(volatile sig_atomic_t* running_flag) {
#ifdef _WIN32
    g_running_flag_ptr = running_flag;
    if (running_flag) *running_flag = 1;
    if (!SetConsoleCtrlHandler(console_control_handler, TRUE)) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to install console control handler");
    }
#else
    g_running_flag_ptr = running_flag;
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
#endif
}

// ── Instance lock ──────────────────────────────────────────────────

#ifdef _WIN32

bool acquire_instance_lock(bool force) {
    std::string lock_path = get_lock_path();
    std::string win_lock_path = lock_path;
    if (lock_path.find("/tmp/") == 0) {
        char temp_path[MAX_PATH];
        GetTempPathA(MAX_PATH, temp_path);
        win_lock_path = std::string(temp_path) + "windmi-controller.lock";
    }

    HANDLE hFile = CreateFileA(win_lock_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to open lock file %s", win_lock_path.c_str());
        return false;
    }

    HANDLE hMutex = CreateMutexA(nullptr, FALSE, "Global\\windmi_controller_lock");
    if (hMutex == nullptr) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to create mutex: %lu", GetLastError());
        CloseHandle(hFile);
        return false;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!force) {
            WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Another instance is already running");
            CloseHandle(hMutex);
            CloseHandle(hFile);
            return false;
        }
        WINDMI_LOG_WARN(LOG_TAG_PLATFORM, "--force: overriding existing instance lock");
    }

    g_lock_fd = _open_osfhandle((intptr_t)hFile, 0);
    g_lock_handle = hMutex;
    return true;
}

#else  // POSIX (Linux)

bool acquire_instance_lock(bool force) {
    std::string lock_path = get_lock_path();
    int fd = open(lock_path.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to open lock file %s: %s", lock_path.c_str(), strerror(errno));
        return false;
    }
    int flags = fcntl(fd, F_GETFD);
    if (flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);

    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        if (!force) {
            WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to acquire lock on %s: %s", lock_path.c_str(), strerror(errno));
            close(fd);
            return false;
        }
        WINDMI_LOG_WARN(LOG_TAG_PLATFORM, "--force: overriding existing instance lock");
        lseek(fd, 0, SEEK_SET);
        ftruncate(fd, 0);
    }

    pid_t pid = getpid();
    char pid_buf[32];
    ssize_t len = snprintf(pid_buf, sizeof(pid_buf), "%lld\n", static_cast<long long>(pid));
    if (len > 0) {
        lseek(fd, 0, SEEK_SET);
        if (write(fd, pid_buf, static_cast<size_t>(len)) != len) {
            WINDMI_LOG_WARN(LOG_TAG_PLATFORM, "Failed to write PID to lock file");
        }
        ftruncate(fd, len);
    }
    g_lock_fd = fd;
    return true;
}

#endif

void release_instance_lock() {
#ifdef _WIN32
    if (g_lock_handle) { CloseHandle(g_lock_handle); g_lock_handle = nullptr; }
    if (g_lock_fd >= 0) { _close(g_lock_fd); g_lock_fd = -1; }
#else
    if (g_lock_fd >= 0) close(g_lock_fd);
    g_lock_fd = -1;
#endif
}

// ── PID alive ──────────────────────────────────────────────────────

bool is_pid_alive(int pid) {
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    DWORD exit_code;
    bool alive = (GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE);
    CloseHandle(process);
    return alive;
#else
    if (pid <= 0) return false;
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/stat", pid);
    FILE* f = fopen(proc_path, "r");
    bool alive = (f != nullptr);
    if (f) fclose(f);
    return alive;
#endif
}

// ── Static dir ─────────────────────────────────────────────────────

std::string resolve_static_dir(const std::string& dir) {
#ifdef _WIN32
    char exe_path[MAX_PATH];
    std::string exe_dir;
    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) > 0) {
        char* p = strrchr(exe_path, '\\');
        if (p) { *p = '\0'; exe_dir = exe_path; }
    }
    auto try_dir = [](const std::string& d) -> std::string {
        DWORD a = GetFileAttributesA(d.c_str());
        return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) ? d : std::string();
    };
    std::string r = try_dir(dir);
    if (!r.empty()) return r;
    if (!exe_dir.empty()) {
        r = try_dir(exe_dir + "\\" + dir);
        if (!r.empty()) return r;
        size_t bs = exe_dir.rfind('\\');
        if (bs != std::string::npos) {
            r = try_dir(exe_dir.substr(0, bs) + "\\" + dir);
            if (!r.empty()) return r;
        }
    }
    return "";
#else
    char exe_path[PATH_MAX], resolved[PATH_MAX];
    std::string exe_dir;
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        if (realpath(exe_path, resolved)) {
            char* p = strrchr(resolved, '/');
            if (p) { *p = '\0'; exe_dir = resolved; }
        }
    }
    struct stat st;
    auto try_dir = [&st](const std::string& d) -> std::string {
        return (stat(d.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) ? d : std::string();
    };
    std::string r = try_dir(dir);
    if (!r.empty()) return r;
    if (!exe_dir.empty()) {
        r = try_dir(exe_dir + "/" + dir);
        if (!r.empty()) return r;
        size_t sl = exe_dir.rfind('/');
        if (sl != std::string::npos) {
            r = try_dir(exe_dir.substr(0, sl) + "/" + dir);
            if (!r.empty()) return r;
        }
    }
    return "";
#endif
}

// ── Sleep ──────────────────────────────────────────────────────────

void sleep_ms(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

}  // namespace windmi::platform

// ─────────────────────────────────────────────────────────────────────
// Windmi Platform Mutex PIMPL
// ─────────────────────────────────────────────────────────────────────

namespace windmi {

struct Mutex::Impl {
#if defined(WINDMI_NATIVE_WINDOWS_THREADS)
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t mutex;
#endif
};

Mutex::Mutex() : impl_(new Mutex::Impl) {
#if defined(WINDMI_NATIVE_WINDOWS_THREADS)
    InitializeCriticalSection(&impl_->cs);
#else
    pthread_mutex_init(&impl_->mutex, nullptr);
#endif
}

Mutex::~Mutex() {
#if defined(WINDMI_NATIVE_WINDOWS_THREADS)
    DeleteCriticalSection(&impl_->cs);
#else
    pthread_mutex_destroy(&impl_->mutex);
#endif
    delete impl_;
}

void Mutex::lock() {
#if defined(WINDMI_NATIVE_WINDOWS_THREADS)
    EnterCriticalSection(&impl_->cs);
#else
    pthread_mutex_lock(&impl_->mutex);
#endif
}

void Mutex::unlock() {
#if defined(WINDMI_NATIVE_WINDOWS_THREADS)
    LeaveCriticalSection(&impl_->cs);
#else
    pthread_mutex_unlock(&impl_->mutex);
#endif
}

// ── ConditionVariable ──────────────────────────────────────────────

struct ConditionVariable::Impl {
#if defined(WINDMI_NATIVE_WINDOWS_THREADS)
    CONDITION_VARIABLE cv;
#else
    pthread_cond_t cond;
#endif
};

ConditionVariable::ConditionVariable() : impl_(new ConditionVariable::Impl) {
#if defined(WINDMI_NATIVE_WINDOWS_THREADS)
    InitializeConditionVariable(&impl_->cv);
#else
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&impl_->cond, &attr);
    pthread_condattr_destroy(&attr);
#endif
}

ConditionVariable::~ConditionVariable() {
#if !defined(WINDMI_NATIVE_WINDOWS_THREADS)
    pthread_cond_destroy(&impl_->cond);
#endif
    delete impl_;
}

void ConditionVariable::notify_one() {
#if defined(WINDMI_NATIVE_WINDOWS_THREADS)
    WakeConditionVariable(&impl_->cv);
#else
    pthread_cond_signal(&impl_->cond);
#endif
}

void ConditionVariable::notify_all() {
#if defined(WINDMI_NATIVE_WINDOWS_THREADS)
    WakeAllConditionVariable(&impl_->cv);
#else
    pthread_cond_broadcast(&impl_->cond);
#endif
}

void ConditionVariable::wait(UniqueLock& lock) {
#if defined(WINDMI_NATIVE_WINDOWS_THREADS)
    SleepConditionVariableCS(&impl_->cv,
                             &static_cast<Mutex*>(lock.mutex())->impl_->cs, INFINITE);
#else
    pthread_cond_wait(&impl_->cond, &static_cast<Mutex*>(lock.mutex())->impl_->mutex);
#endif
}

bool ConditionVariable::wait_for(UniqueLock& lock, unsigned int ms) {
#if defined(WINDMI_NATIVE_WINDOWS_THREADS)
    BOOL r = SleepConditionVariableCS(&impl_->cv,
                                      &static_cast<Mutex*>(lock.mutex())->impl_->cs, ms);
    if (r) return true;
    DWORD err = GetLastError();
    if (err == ERROR_TIMEOUT) return false;
    WINDMI_LOG_WARN(LOG_TAG_PLATFORM, "ConditionVariable::wait_for unexpected error: %lu", err);
    return false;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_nsec += (ms % 1000) * 1000000;
    ts.tv_sec += ms / 1000 + (ts.tv_nsec >= 1000000000 ? 1 : 0);
    if (ts.tv_nsec >= 1000000000) ts.tv_nsec -= 1000000000;
    return pthread_cond_timedwait(&impl_->cond,
                                  &static_cast<Mutex*>(lock.mutex())->impl_->mutex, &ts) == 0;
#endif
}

// ── Thread ─────────────────────────────────────────────────────────

#if defined(WINDMI_NATIVE_WINDOWS_THREADS)

struct Thread::Impl {
    HANDLE handle = nullptr;
    unsigned int thread_id = 0;
    bool joined = false;
    bool detached = false;
};

Thread::Thread(std::function<void()> callable) : impl_(new Thread::Impl) {
    auto* fp = new std::function<void()>(std::move(callable));
    impl_->handle = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0, +[](void* a) -> unsigned int {
            auto* p = static_cast<std::function<void()>*>(a);
            try { (*p)(); } catch (...) {}
            delete p;
            return 0;
        }, fp, 0, &impl_->thread_id));
    if (impl_->handle == nullptr) { delete fp; impl_->joined = true; }
    else { impl_->joined = false; impl_->detached = false; }
}

#else  // POSIX Thread

struct Thread::Impl {
    pthread_t thread{};
    bool joined = false;
    bool detached = false;
};

Thread::Thread(std::function<void()> callable) : impl_(new Thread::Impl) {
    auto* fp = new std::function<void()>(std::move(callable));
    int ret = pthread_create(&impl_->thread, nullptr, +[](void* a) -> void* {
        auto* p = static_cast<std::function<void()>*>(a);
        try { (*p)(); } catch (...) {}
        delete p;
        return nullptr;
    }, fp);
    if (ret != 0) { delete fp; impl_->joined = true; }
    else { impl_->joined = false; impl_->detached = false; }
}

#endif

Thread::~Thread() { if (joinable()) join(); }

Thread::Thread(Thread&& other) noexcept : impl_(other.impl_) { other.impl_ = nullptr; }

Thread& Thread::operator=(Thread&& other) noexcept {
    if (this != &other) {
        if (joinable()) join();
        if (impl_) {
#if defined(WINDMI_NATIVE_WINDOWS_THREADS)
            if (impl_->handle) CloseHandle(impl_->handle);
#endif
            delete impl_;
        }
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

bool Thread::joinable() const { return impl_ && !impl_->joined && !impl_->detached; }

void Thread::join() {
    if (!impl_ || !joinable()) return;
#if defined(WINDMI_NATIVE_WINDOWS_THREADS)
    if (impl_->handle && WaitForSingleObject(impl_->handle, INFINITE) == WAIT_OBJECT_0) {
        CloseHandle(impl_->handle);
        impl_->handle = nullptr;
    }
    impl_->joined = true;
#else
    if (impl_->thread) { pthread_join(impl_->thread, nullptr); impl_->thread = 0; }
    impl_->joined = true;
#endif
}

void Thread::detach() {
    if (!impl_ || !joinable()) return;
#if defined(WINDMI_NATIVE_WINDOWS_THREADS)
    if (impl_->handle) { CloseHandle(impl_->handle); impl_->handle = nullptr; }
    impl_->detached = true;
#else
    if (impl_->thread) { pthread_detach(impl_->thread); impl_->thread = 0; }
    impl_->detached = true;
#endif
}

void* Thread::thread_entry(void* arg) {
    auto* fp = static_cast<std::function<void()>*>(arg);
    try { (*fp)(); } catch (...) {}
    delete fp;
    return nullptr;
}

// ── UniqueLock ─────────────────────────────────────────────────────

UniqueLock::UniqueLock(Mutex& mutex) : mutex_(&mutex), owns_(true) { mutex_->lock(); }
UniqueLock::~UniqueLock() { if (owns_) mutex_->unlock(); }
void UniqueLock::lock() { if (!owns_) { mutex_->lock(); owns_ = true; } }
void UniqueLock::unlock() { if (owns_) { mutex_->unlock(); owns_ = false; } }
Mutex* UniqueLock::mutex() const noexcept { return mutex_; }
bool UniqueLock::owns_lock() const noexcept { return owns_; }

}  // namespace windmi

// ─────────────────────────────────────────────────────────────────────
// SerialPort Implementation
// ─────────────────────────────────────────────────────────────────────

namespace windmi::platform {

#ifdef _WIN32
// Windows SerialPort (MSVC + MinGW)

SerialPort::SerialPort() : handle_(nullptr), rs485_enabled_(false), open_(false) {}
SerialPort::~SerialPort() { close(); }
SerialPort::SerialPort(SerialPort&& o) noexcept
    : handle_(o.handle_), rs485_enabled_(o.rs485_enabled_), open_(o.open_) {
    o.handle_ = nullptr; o.open_ = false;
}
SerialPort& SerialPort::operator=(SerialPort&& o) noexcept {
    if (this != &o) { close(); handle_ = o.handle_; rs485_enabled_ = o.rs485_enabled_; open_ = o.open_; o.handle_ = nullptr; o.open_ = false; }
    return *this;
}

bool SerialPort::open(const std::string& device, int baud, char parity, int stop_bits, bool rs485) {
    close();
    std::wstring wdev;
    if (!device.empty()) {
        int sz = MultiByteToWideChar(CP_UTF8, 0, device.c_str(), -1, nullptr, 0);
        if (sz > 0) { wdev.resize(sz - 1); MultiByteToWideChar(CP_UTF8, 0, device.c_str(), -1, &wdev[0], sz); }
    }
    std::wstring norm = wdev;
    std::string sd = device;
    if (sd.size() >= 4 && ((sd[0]=='C'||sd[0]=='c') && (sd[1]=='O'||sd[1]=='o') && (sd[2]=='M'||sd[2]=='m'))) {
        try { int pn = std::stoi(sd.substr(3)); if (pn >= 10) norm = L"\\\\.\\COM" + std::to_wstring(pn); } catch (...) {}
    }
    handle_ = CreateFileW(norm.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) { WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to open serial port %s", device.c_str()); handle_ = nullptr; return false; }

    COMMTIMEOUTS to; memset(&to, 0, sizeof(to));
    to.ReadIntervalTimeout = 100; to.ReadTotalTimeoutConstant = 1000; to.ReadTotalTimeoutMultiplier = 10;
    to.WriteTotalTimeoutConstant = 1000; to.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(handle_, &to);

    DCB dcb; memset(&dcb, 0, sizeof(dcb)); dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(handle_, &dcb)) { close(); return false; }
    switch (baud) {
        case 9600: dcb.BaudRate = CBR_9600; break; case 19200: dcb.BaudRate = CBR_19200; break;
        case 38400: dcb.BaudRate = CBR_38400; break; case 57600: dcb.BaudRate = CBR_57600; break;
        case 115200: dcb.BaudRate = CBR_115200; break;
        default: WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Unsupported baud rate: %d", baud); close(); return false;
    }
    dcb.ByteSize = 8; dcb.StopBits = (stop_bits == 2) ? TWOSTOPBITS : ONESTOPBIT;
    switch (parity) {
        case 'N': dcb.Parity = NOPARITY; dcb.fParity = FALSE; break;
        case 'E': dcb.Parity = EVENPARITY; dcb.fParity = TRUE; break;
        case 'O': dcb.Parity = ODDPARITY; dcb.fParity = TRUE; break;
        default: WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Unsupported parity: %c", parity); close(); return false;
    }
    dcb.fOutxCtsFlow = FALSE; dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutxDsrFlow = FALSE; dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fOutX = FALSE; dcb.fInX = FALSE;
    if (!SetCommState(handle_, &dcb)) { close(); return false; }
    rs485_enabled_ = rs485; open_ = true;
    WINDMI_LOG_INFO(LOG_TAG_PLATFORM, "Serial port opened: %s @ %d %d%c%d", device.c_str(), baud, 8, parity, stop_bits);
    return true;
}

void SerialPort::close() { if (handle_ && handle_ != INVALID_HANDLE_VALUE) { FlushFileBuffers(handle_); CloseHandle(handle_); } handle_ = nullptr; open_ = false; }
bool SerialPort::isOpen() const { return open_; }
void SerialPort::flush() { if (handle_ && handle_ != INVALID_HANDLE_VALUE) FlushFileBuffers(handle_); }

int SerialPort::read(uint8_t* buffer, size_t len, unsigned int timeout_ms) {
    if (!open_ || !buffer || len == 0) return -1;
    COMMTIMEOUTS to; memset(&to, 0, sizeof(to)); to.ReadIntervalTimeout = 100; to.ReadTotalTimeoutConstant = timeout_ms; to.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(handle_, &to);
    DWORD br = 0;
    if (!ReadFile(handle_, buffer, static_cast<DWORD>(len), &br, nullptr)) return -1;
    return static_cast<int>(br);
}

int SerialPort::write(const uint8_t* buffer, size_t len) {
    if (!open_ || !buffer || len == 0) return -1;
    DWORD bw = 0;
    if (!WriteFile(handle_, buffer, static_cast<DWORD>(len), &bw, nullptr)) return -1;
    return static_cast<int>(bw);
}

#else  // POSIX SerialPort (Linux)

SerialPort::SerialPort() : fd_(-1), open_(false) {}
SerialPort::~SerialPort() { close(); }
SerialPort::SerialPort(SerialPort&& o) noexcept : fd_(o.fd_), open_(o.open_) { o.fd_ = -1; o.open_ = false; }
SerialPort& SerialPort::operator=(SerialPort&& o) noexcept {
    if (this != &o) { close(); fd_ = o.fd_; open_ = o.open_; o.fd_ = -1; o.open_ = false; }
    return *this;
}

bool SerialPort::open(const std::string& device, int baud, char parity, int stop_bits, bool) {
    close();
    fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) { WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to open serial device %s: %s", device.c_str(), strerror(errno)); return false; }
    struct termios tty; memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd_, &tty) != 0) { close(); return false; }
    cfmakeraw(&tty);
    speed_t speed;
    switch (baud) {
        case 9600: speed = B9600; break; case 19200: speed = B19200; break;
        case 38400: speed = B38400; break; case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
        default: WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Unsupported baud rate: %d", baud); close(); return false;
    }
    if (cfsetspeed(&tty, speed) != 0) { close(); return false; }
    tty.c_cflag &= ~CSIZE; tty.c_cflag |= CS8;
    if (stop_bits == 2) tty.c_cflag |= CSTOPB; else tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~PARENB; tty.c_cflag &= ~PARODD;
    switch (parity) {
        case 'N': break;
        case 'E': tty.c_cflag |= PARENB; break;
        case 'O': tty.c_cflag |= PARENB; tty.c_cflag |= PARODD; break;
        default: WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Unsupported parity: %c", parity); close(); return false;
    }
    tty.c_cflag &= ~CRTSCTS; tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cc[VMIN] = 1; tty.c_cc[VTIME] = 0;
    if (tcsetattr(fd_, TCSANOW, &tty) != 0) { close(); return false; }
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) & ~O_NONBLOCK);
    tcflush(fd_, TCIOFLUSH);
    open_ = true;
    WINDMI_LOG_INFO(LOG_TAG_PLATFORM, "Serial port opened: %s @ %d %d%c%d", device.c_str(), baud, 8, parity, stop_bits);
    return true;
}

void SerialPort::close() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } open_ = false; }
bool SerialPort::isOpen() const { return open_; }
void SerialPort::flush() { if (fd_ >= 0) tcflush(fd_, TCIFLUSH); }

int SerialPort::read(uint8_t* buffer, size_t len, unsigned int timeout_ms) {
    if (!open_ || fd_ < 0 || !buffer || len == 0) return -1;
    fd_set fds; struct timeval tv;
    FD_ZERO(&fds); FD_SET(fd_, &fds);
    tv.tv_sec = timeout_ms / 1000; tv.tv_usec = (timeout_ms % 1000) * 1000;
    int r = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
    if (r <= 0) return (r == 0) ? 0 : -1;
    ssize_t n = ::read(fd_, buffer, len);
    if (n < 0) return (errno == EINTR) ? 0 : -1;
    return static_cast<int>(n);
}

int SerialPort::write(const uint8_t* buffer, size_t len) {
    if (!open_ || fd_ < 0 || !buffer || len == 0) return -1;
    ssize_t n = ::write(fd_, buffer, len);
    return (n < 0) ? -1 : static_cast<int>(n);
}

#endif

}  // namespace windmi::platform

// ──────────────────────────────────────────────────────────────────
// C-linkage bridge
// ──────────────────────────────────────────────────────────────────

extern "C" void windmi_sleep_ms(unsigned int ms) { windmi::platform::sleep_ms(ms); }

extern "C" WindmiSerialPort *windmi_serial_open(const char *device, int baud, char parity,
                                                 int stop_bits, bool rs485_enabled) {
    if (!device) return nullptr;
    auto *port = new windmi::platform::SerialPort();
    if (!port->open(device, baud, parity, stop_bits, rs485_enabled)) { delete port; return nullptr; }
    return reinterpret_cast<WindmiSerialPort*>(port);
}

extern "C" void windmi_serial_close(WindmiSerialPort *port) {
    if (!port) return;
    auto *p = reinterpret_cast<windmi::platform::SerialPort*>(port);
    p->close(); delete p;
}

extern "C" int windmi_serial_read(WindmiSerialPort *port, uint8_t *buffer, size_t len,
                                   unsigned int timeout_ms) {
    if (!port) return -1;
    return reinterpret_cast<windmi::platform::SerialPort*>(port)->read(buffer, len, timeout_ms);
}

extern "C" int windmi_serial_write(WindmiSerialPort *port, const uint8_t *buffer, size_t len) {
    if (!port) return -1;
    return reinterpret_cast<windmi::platform::SerialPort*>(port)->write(buffer, len);
}

extern "C" void windmi_serial_flush(WindmiSerialPort *port) {
    if (!port) return;
    reinterpret_cast<windmi::platform::SerialPort*>(port)->flush();
}
