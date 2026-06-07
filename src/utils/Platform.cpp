#include "utils/Platform.hpp"
#include "utils/PlatformC.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <io.h>
#else
#include <termios.h>
#endif

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <windows.h>
#include <pthread.h>
#else
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

// Test hook for lock path override
static std::string g_lock_name_override;
static bool g_lock_name_override_set = false;

// Instance lock state tracking
#ifdef _WIN32
static int g_lock_fd = -1;
static HANDLE g_lock_handle = nullptr;
#else
static int g_lock_fd = -1;
#endif

static std::string get_lock_path() {
    if (g_lock_name_override_set) {
        return g_lock_name_override;
    }
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

#ifdef _WIN32

// Windows signal handling via Console Control Handler
static volatile sig_atomic_t* g_running_flag_ptr = nullptr;
static BOOL WINAPI console_control_handler(DWORD dwCtrlType) {
    switch (dwCtrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if (g_running_flag_ptr) {
                *g_running_flag_ptr = 0;
            }
            return TRUE;
        default:
            return FALSE;
    }
}

void install_signal_handlers(volatile sig_atomic_t* running_flag) {
    g_running_flag_ptr = running_flag;
    if (running_flag) {
        *running_flag = 1;
    }
    if (!SetConsoleCtrlHandler(console_control_handler, TRUE)) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to install console control handler");
    }
}

bool acquire_instance_lock(bool force) {
#ifdef _WIN32
    std::string lock_path = get_lock_path();
    // Convert path to Windows format (replace /tmp with a valid Windows temp path)
    std::string win_lock_path = lock_path;
    if (lock_path.find("/tmp/") == 0) {
        char temp_path[MAX_PATH];
        GetTempPathA(MAX_PATH, temp_path);
        win_lock_path = std::string(temp_path) + "windmi-controller.lock";
    }

    // Windows doesn't have flock() - use file mapping for exclusive lock
    // Open the file, then create a named mutex for exclusive access
    HANDLE hFile = CreateFileA(win_lock_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to open lock file %s", win_lock_path.c_str());
        return false;
    }

    // Create a named mutex for instance locking
    // The mutex name should be unique per application
    HANDLE hMutex = CreateMutexA(nullptr, FALSE, "Global\\windmi_controller_lock");
    if (hMutex == nullptr) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to create mutex: %lu", GetLastError());
        CloseHandle(hFile);
        return false;
    }

    // Check if we got exclusive access
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!force) {
            WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Another instance is already running");
            CloseHandle(hMutex);
            CloseHandle(hFile);
            return false;
        }
        WINDMI_LOG_WARN(LOG_TAG_PLATFORM, "--force: overriding existing instance lock");
    }

    // Store both handles for later release
    g_lock_fd = _open_osfhandle((intptr_t)hFile, 0);
    g_lock_handle = hMutex;
    return true;
#else
    std::string lock_path = get_lock_path();

    int fd = open(lock_path.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to open lock file %s: %s", lock_path.c_str(), strerror(errno));
        return false;
    }

    // Set FD_CLOEXEC
    int flags = fcntl(fd, F_GETFD);
    if (flags >= 0) {
        fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }

    // Try to acquire exclusive lock
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        if (!force) {
            WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to acquire lock on %s: %s", lock_path.c_str(), strerror(errno));
            close(fd);
            return false;
        }
        WINDMI_LOG_WARN(LOG_TAG_PLATFORM, "--force: overriding existing instance lock");

        // Force override: truncate and rewrite PID
        lseek(fd, 0, SEEK_SET);
        if (ftruncate(fd, 0) < 0) {
            WINDMI_LOG_WARN(LOG_TAG_PLATFORM, "Failed to truncate lock file: %s", strerror(errno));
        }
    }

    // Write our PID to the lock file
    pid_t pid = getpid();
    char pid_buf[32];
    ssize_t len = snprintf(pid_buf, sizeof(pid_buf), "%d\n", pid);
    if (len > 0) {
        lseek(fd, 0, SEEK_SET);
        if (write(fd, pid_buf, static_cast<size_t>(len)) != len) {
            WINDMI_LOG_WARN(LOG_TAG_PLATFORM, "Failed to write PID to lock file: %s", strerror(errno));
        }
        ftruncate(fd, len);
    }

    g_lock_fd = fd;
    return true;
#endif
}

void release_instance_lock() {
#ifdef _WIN32
    if (g_lock_handle) {
        CloseHandle(g_lock_handle);
        g_lock_handle = nullptr;
    }
    if (g_lock_fd >= 0) {
        _close(g_lock_fd);
        g_lock_fd = -1;
    }
#else
    if (g_lock_fd >= 0) {
        close(g_lock_fd);
        g_lock_fd = -1;
    }
#endif
}

bool is_pid_alive(int pid) {
    // Windows: Use OpenProcess to check if a process exists
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return false;
    }

    DWORD exit_code;
    if (GetExitCodeProcess(process, &exit_code)) {
        bool alive = (exit_code == STILL_ACTIVE);
        CloseHandle(process);
        return alive;
    }

    CloseHandle(process);
    return false;
}

std::string resolve_static_dir(const std::string& dir) {
    char exe_path[MAX_PATH];
    std::string exe_dir;

    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) > 0) {
        // Find last backslash and truncate
        char* last_backslash = strrchr(exe_path, '\\');
        if (last_backslash != nullptr) {
            *last_backslash = '\0';
            exe_dir = exe_path;
        }
    }

    // Try as-is
    DWORD attrs = GetFileAttributesA(dir.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return dir;
    }

    // Try relative to executable
    if (!exe_dir.empty()) {
        std::string candidate = exe_dir + "\\" + dir;
        attrs = GetFileAttributesA(candidate.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            return candidate;
        }
    }

    // Try one level above executable
    if (!exe_dir.empty()) {
        size_t last_backslash = exe_dir.rfind('\\');
        if (last_backslash != std::string::npos) {
            std::string parent_dir = exe_dir.substr(0, last_backslash);
            std::string candidate = parent_dir + "\\" + dir;
            attrs = GetFileAttributesA(candidate.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                return candidate;
            }
        }
    }

    return "";
}

void sleep_ms(unsigned int ms) {
    Sleep(ms);
}

#else  // POSIX implementation

// Signal handling
static volatile sig_atomic_t* g_running_flag_ptr = nullptr;

static void signal_handler(int sig) {
    (void)sig;
    if (g_running_flag_ptr) {
        *g_running_flag_ptr = 0;
    }
}

void install_signal_handlers(volatile sig_atomic_t* running_flag) {
    g_running_flag_ptr = running_flag;

    // Ignore SIGPIPE - it can occur when writing to a closed socket
    signal(SIGPIPE, SIG_IGN);

    // Set up handlers for shutdown signals
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, nullptr) < 0) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to install SIGINT handler: %s", strerror(errno));
    }
    if (sigaction(SIGTERM, &sa, nullptr) < 0) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to install SIGTERM handler: %s", strerror(errno));
    }
}

bool acquire_instance_lock(bool force) {
    std::string lock_path = get_lock_path();

    int fd = open(lock_path.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to open lock file %s: %s", lock_path.c_str(), strerror(errno));
        return false;
    }

    // Set FD_CLOEXEC to avoid leaking the fd to child processes
    int flags = fcntl(fd, F_GETFD);
    if (flags >= 0) {
        fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }

    // Try to acquire exclusive lock
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        if (!force) {
            WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to acquire lock on %s: %s", lock_path.c_str(), strerror(errno));
            close(fd);
            return false;
        }
        WINDMI_LOG_WARN(LOG_TAG_PLATFORM, "--force: overriding existing instance lock");

        // Force override: truncate and rewrite PID
        lseek(fd, 0, SEEK_SET);
        if (ftruncate(fd, 0) < 0) {
            WINDMI_LOG_WARN(LOG_TAG_PLATFORM, "Failed to truncate lock file: %s", strerror(errno));
        }
    }

    // Write our PID to the lock file
    pid_t pid = getpid();
    char pid_buf[32];
    ssize_t len = snprintf(pid_buf, sizeof(pid_buf), "%d\n", pid);
    if (len > 0) {
        lseek(fd, 0, SEEK_SET);
        if (write(fd, pid_buf, static_cast<size_t>(len)) != len) {
            WINDMI_LOG_WARN(LOG_TAG_PLATFORM, "Failed to write PID to lock file: %s", strerror(errno));
        }
        ftruncate(fd, len);
    }

    // Store the fd globally to hold the lock and release it later
    g_lock_fd = fd;
    return true;
}

void release_instance_lock() {
#ifdef _WIN32
    if (g_lock_handle) {
        CloseHandle(g_lock_handle);
        g_lock_handle = nullptr;
    }
#else
    if (g_lock_fd >= 0) {
        close(g_lock_fd);
        g_lock_fd = -1;
    }
#endif
}

bool is_pid_alive(int pid) {
    if (pid <= 0) {
        return false;
    }

    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/stat", pid);

    FILE* f = fopen(proc_path, "r");
    if (!f) {
        return false;
    }

    // Read and close - if we can open it, the process exists
    fclose(f);
    return true;
}

std::string resolve_static_dir(const std::string& dir) {
    char exe_path[PATH_MAX];
    char resolved[PATH_MAX];
    std::string exe_dir;

    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        if (realpath(exe_path, resolved)) {
            // Find last slash and truncate
            char* last_slash = strrchr(resolved, '/');
            if (last_slash != nullptr) {
                *last_slash = '\0';
                exe_dir = resolved;
            }
        }
    }

    // Try as-is
    struct stat st;
    if (stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        return dir;
    }

    // Try relative to executable
    if (!exe_dir.empty()) {
        std::string candidate = exe_dir + "/" + dir;
        if (stat(candidate.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            return candidate;
        }
    }

    // Try one level above executable
    if (!exe_dir.empty()) {
        size_t last_slash = exe_dir.rfind('/');
        if (last_slash != std::string::npos) {
            std::string parent_dir = exe_dir.substr(0, last_slash);
            std::string candidate = parent_dir + "/" + dir;
            if (stat(candidate.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                return candidate;
            }
        }
    }

    return "";
}

void sleep_ms(unsigned int ms) {
    usleep(ms * 1000);
}

#endif  // _WIN32

}  // namespace windmi::platform

// ─────────────────────────────────────────────────────────────────────
// Windmi Platform Mutex Implementation
// ─────────────────────────────────────────────────────────────────────

namespace windmi {

Mutex::Mutex() {
    pthread_mutex_init(&mutex_, nullptr);
}

Mutex::~Mutex() {
    pthread_mutex_destroy(&mutex_);
}

void Mutex::lock() {
    pthread_mutex_lock(&mutex_);
}

void Mutex::unlock() {
    pthread_mutex_unlock(&mutex_);
}

// ─────────────────────────────────────────────────────────────────────
// Windmi Platform Thread Implementation
// ─────────────────────────────────────────────────────────────────────

Thread::Thread(std::function<void()> callable) {
#ifdef _WIN32
    // Windows MinGW uses pthread_create with a wrapper
    // Store the callable in a shared_ptr on the heap
    auto* func_ptr = new std::function<void()>(std::move(callable));
    int ret = pthread_create(&thread_, nullptr, thread_entry, func_ptr);
    if (ret != 0) {
        delete func_ptr;
        thread_ = {};
        joined_ = true;  // mark as non-joinable
    } else {
        joined_ = false;
        detached_ = false;
    }
#else
    // Store the callable in a shared_ptr on the heap
    auto* func_ptr = new std::function<void()>(std::move(callable));
    int ret = pthread_create(&thread_, nullptr, thread_entry, func_ptr);
    if (ret != 0) {
        delete func_ptr;
        thread_ = {};
        joined_ = true;  // mark as non-joinable
    } else {
        joined_ = false;
        detached_ = false;
    }
#endif
}

Thread::~Thread() {
    if (joinable()) {
        join();
    }
}

Thread::Thread(Thread&& other) noexcept
    : thread_(other.thread_), joined_(other.joined_), detached_(other.detached_)
{
    other.thread_ = {};
    other.joined_ = true;
    other.detached_ = false;
}

Thread& Thread::operator=(Thread&& other) noexcept {
    if (this != &other) {
        if (joinable()) {
            join();
        }
        thread_ = other.thread_;
        joined_ = other.joined_;
        detached_ = other.detached_;
        other.thread_ = {};
        other.joined_ = true;
        other.detached_ = false;
    }
    return *this;
}

bool Thread::joinable() const {
    return !joined_ && !detached_;
}

void Thread::join() {
    if (!joined_ && !detached_ && thread_ != 0) {
        pthread_join(thread_, nullptr);
        joined_ = true;
    }
}

void Thread::detach() {
    if (!joined_ && !detached_ && thread_ != 0) {
        pthread_detach(thread_);
        detached_ = true;
    }
}

void* Thread::thread_entry(void* arg) {
    auto* func_ptr = static_cast<std::function<void()>*>(arg);
    try {
        (*func_ptr)();
    } catch (...) {
        // Catch all exceptions to prevent thread crash
    }
    delete func_ptr;
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────
// Windmi Platform UniqueLock Implementation
// ─────────────────────────────────────────────────────────────────────

UniqueLock::UniqueLock(Mutex& mutex) : mutex_(&mutex), owns_(true) {
    mutex_->lock();
}

UniqueLock::~UniqueLock() {
    if (owns_) {
        mutex_->unlock();
    }
}

void UniqueLock::lock() {
    if (!owns_) {
        mutex_->lock();
        owns_ = true;
    }
}

void UniqueLock::unlock() {
    if (owns_) {
        mutex_->unlock();
        owns_ = false;
    }
}

Mutex* UniqueLock::mutex() const noexcept {
    return mutex_;
}

bool UniqueLock::owns_lock() const noexcept {
    return owns_;
}

// ─────────────────────────────────────────────────────────────────────
// ConditionVariable Implementation
// ─────────────────────────────────────────────────────────────────────

ConditionVariable::ConditionVariable() {
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&cond_, &attr);
    pthread_condattr_destroy(&attr);
}

ConditionVariable::~ConditionVariable() {
    pthread_cond_destroy(&cond_);
}

}  // namespace windmi

// ─────────────────────────────────────────────────────────────────────
// Windmi Platform SerialPort Implementation
// ─────────────────────────────────────────────────────────────────────

namespace windmi::platform {

// ─────────────────────────────────────────────────────────────────────
// SerialPort (POSIX implementation using termios)
// ─────────────────────────────────────────────────────────────────────

#ifdef _WIN32
// Windows SerialPort implementation

SerialPort::SerialPort() : handle_(nullptr), rs485_enabled_(false), open_(false) {}

SerialPort::~SerialPort() {
    close();
}

SerialPort::SerialPort(SerialPort&& other) noexcept
    : handle_(other.handle_), rs485_enabled_(other.rs485_enabled_), open_(other.open_) {
    other.handle_ = nullptr;
    other.open_ = false;
}

SerialPort& SerialPort::operator=(SerialPort&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        rs485_enabled_ = other.rs485_enabled_;
        open_ = other.open_;
        other.handle_ = nullptr;
        other.open_ = false;
    }
    return *this;
}

bool SerialPort::open(const std::string& device, int baud, char parity, int stop_bits, bool rs485_enabled) {
    close();

    // Convert device path to wide string for Windows
    std::wstring wdevice;
    if (!device.empty()) {
        int size = MultiByteToWideChar(CP_UTF8, 0, device.c_str(), -1, nullptr, 0);
        if (size > 0) {
            wdevice.resize(size - 1);
            MultiByteToWideChar(CP_UTF8, 0, device.c_str(), -1, &wdevice[0], size);
        }
    }

    // Open the serial port
    handle_ = CreateFileW(wdevice.c_str(), GENERIC_READ | GENERIC_WRITE,
                          0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to open serial port %s", device.c_str());
        handle_ = nullptr;
        return false;
    }

    // Set timeout parameters
    COMMTIMEOUTS timeouts;
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = 100;  // 100ms between characters
    timeouts.ReadTotalTimeoutConstant = 1000;  // 1 second total timeout
    timeouts.ReadTotalTimeoutMultiplier = 10;  // 10ms per character
    timeouts.WriteTotalTimeoutConstant = 1000;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(handle_, &timeouts);

    // Configure port
    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(DCB);

    if (!GetCommState(handle_, &dcb)) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "GetCommState failed");
        close();
        return false;
    }

    // Set baud rate
    switch (baud) {
        case 9600:   dcb.BaudRate = CBR_9600;   break;
        case 19200:  dcb.BaudRate = CBR_19200;  break;
        case 38400:  dcb.BaudRate = CBR_38400;  break;
        case 57600:  dcb.BaudRate = CBR_57600;  break;
        case 115200: dcb.BaudRate = CBR_115200; break;
        default:
            WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Unsupported baud rate: %d", baud);
            close();
            return false;
    }

    // 8 data bits
    dcb.ByteSize = 8;

    // Stop bits
    dcb.StopBits = (stop_bits == 2) ? TWOSTOPBITS : ONESTOPBIT;

    // Parity
    switch (parity) {
        case 'N':
            dcb.Parity = NOPARITY;
            dcb.fParity = FALSE;
            break;
        case 'E':
            dcb.Parity = EVENPARITY;
            dcb.fParity = TRUE;
            break;
        case 'O':
            dcb.Parity = ODDPARITY;
            dcb.fParity = TRUE;
            break;
        default:
            WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Unsupported parity: %c", parity);
            close();
            return false;
    }

    // Flow control (none)
    dcb.fOutxCtsFlow = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;

    if (!SetCommState(handle_, &dcb)) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "SetCommState failed");
        close();
        return false;
    }

    rs485_enabled_ = rs485_enabled;
    open_ = true;

    WINDMI_LOG_INFO(LOG_TAG_PLATFORM, "Serial port opened: %s @ %d %d%c%d",
                    device.c_str(), baud, 8, parity, stop_bits);
    return true;
}

void SerialPort::close() {
    if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(handle_);
        CloseHandle(handle_);
        handle_ = nullptr;
    }
    open_ = false;
}

bool SerialPort::isOpen() const {
    return open_;
}

void SerialPort::flush() {
    if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(handle_);
    }
}

int SerialPort::read(uint8_t* buffer, size_t len, unsigned int timeout_ms) {
    if (!open_ || !buffer || len == 0) {
        return -1;
    }

    // Set timeout
    COMMTIMEOUTS timeouts;
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = 100;
    timeouts.ReadTotalTimeoutConstant = timeout_ms;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(handle_, &timeouts);

    DWORD bytes_read = 0;
    if (!ReadFile(handle_, buffer, static_cast<DWORD>(len), &bytes_read, nullptr)) {
        return -1;
    }

    return static_cast<int>(bytes_read);
}

int SerialPort::write(const uint8_t* buffer, size_t len) {
    if (!open_ || !buffer || len == 0) {
        return -1;
    }

    DWORD bytes_written = 0;
    if (!WriteFile(handle_, buffer, static_cast<DWORD>(len), &bytes_written, nullptr)) {
        return -1;
    }

    return static_cast<int>(bytes_written);
}

#else
// POSIX SerialPort implementation using termios

SerialPort::SerialPort() : fd_(-1), open_(false) {}

SerialPort::~SerialPort() {
    close();
}

SerialPort::SerialPort(SerialPort&& other) noexcept : fd_(other.fd_), open_(other.open_) {
    other.fd_ = -1;
    other.open_ = false;
}

SerialPort& SerialPort::operator=(SerialPort&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        open_ = other.open_;
        other.fd_ = -1;
        other.open_ = false;
    }
    return *this;
}

bool SerialPort::open(const std::string& device, int baud, char parity, int stop_bits, bool rs485_enabled) {
    (void)rs485_enabled;  // TODO: RS-485 direction control not yet implemented on any platform
    close();

    // Open device in read-write mode (non-blocking to avoid hanging on open)
    fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to open serial device %s: %s", device.c_str(), strerror(errno));
        return false;
    }

    // Configure serial port
    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd_, &tty) != 0) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "tcgetattr failed: %s", strerror(errno));
        close();
        return false;
    }

    // Set raw mode
    cfmakeraw(&tty);

    // Set baud rate
    speed_t speed;
    switch (baud) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        default:
            WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Unsupported baud rate: %d", baud);
            close();
            return false;
    }

    if (cfsetspeed(&tty, speed) != 0) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "cfsetspeed failed: %s", strerror(errno));
        close();
        return false;
    }

    // 8 data bits
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    // Stop bits
    if (stop_bits == 2) {
        tty.c_cflag |= CSTOPB;
    } else {
        tty.c_cflag &= ~CSTOPB;
    }

    // Parity
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~PARODD;

    switch (parity) {
        case 'N':
            tty.c_cflag &= ~PARENB;
            break;
        case 'E':
            tty.c_cflag |= PARENB;
            tty.c_cflag &= ~PARODD;
            break;
        case 'O':
            tty.c_cflag |= PARENB;
            tty.c_cflag |= PARODD;
            break;
        default:
            WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Unsupported parity: %c", parity);
            close();
            return false;
    }

    // Disable flow control
    tty.c_cflag &= ~CRTSCTS;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    // Read behavior: minimum = 1, timeout = 0 (blocking with select)
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "tcsetattr failed: %s", strerror(errno));
        close();
        return false;
    }

    // Clear non-blocking flag for normal operation
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);
    }

    // Flush any stale data
    tcflush(fd_, TCIOFLUSH);

    open_ = true;

    WINDMI_LOG_INFO(LOG_TAG_PLATFORM, "Serial port opened: %s @ %d %d%c%d",
                    device.c_str(), baud, 8, parity, stop_bits);
    return true;
}

void SerialPort::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    open_ = false;
}

bool SerialPort::isOpen() const {
    return open_;
}

void SerialPort::flush() {
    if (fd_ >= 0) {
        tcflush(fd_, TCIFLUSH);
    }
}

int SerialPort::read(uint8_t* buffer, size_t len, unsigned int timeout_ms) {
    if (!open_ || fd_ < 0 || !buffer || len == 0) {
        return -1;
    }

    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ready = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
    if (ready <= 0) {
        return (ready == 0) ? 0 : -1;  // 0 = timeout, -1 = error
    }

    ssize_t received = ::read(fd_, buffer, len);
    if (received < 0) {
        if (errno == EINTR) {
            return 0;  // Interrupted, return 0 to indicate timeout behavior
        }
        return -1;
    }

    return static_cast<int>(received);
}

int SerialPort::write(const uint8_t* buffer, size_t len) {
#ifdef _WIN32
    if (!open_ || !buffer || len == 0) {
        return -1;
    }

    DWORD bytes_written = 0;
    if (!WriteFile(handle_, buffer, static_cast<DWORD>(len), &bytes_written, nullptr)) {
        return -1;
    }

    return static_cast<int>(bytes_written);
#else
    if (!open_ || fd_ < 0 || !buffer || len == 0) {
        return -1;
    }

    ssize_t sent = ::write(fd_, buffer, len);
    if (sent < 0) {
        return -1;
    }

    return static_cast<int>(sent);
#endif
}

#endif  // _WIN32 (outer from line 601)

}  // namespace windmi::platform

// ──────────────────────────────────────────────────────────────────
// C-linkage bridge functions (callable from .c files)
// ──────────────────────────────────────────────────────────────────

extern "C" void windmi_sleep_ms(unsigned int ms) {
    windmi::platform::sleep_ms(ms);
}

extern "C" WindmiSerialPort *windmi_serial_open(const char *device, int baud, char parity,
                                                 int stop_bits, bool rs485_enabled) {
    if (!device) return nullptr;
    auto *port = new windmi::platform::SerialPort();
    if (!port->open(device, baud, parity, stop_bits, rs485_enabled)) {
        delete port;
        return nullptr;
    }
    return reinterpret_cast<WindmiSerialPort*>(port);
}

extern "C" void windmi_serial_close(WindmiSerialPort *port) {
    if (!port) return;
    auto *p = reinterpret_cast<windmi::platform::SerialPort*>(port);
    p->close();
    delete p;
}

extern "C" int windmi_serial_read(WindmiSerialPort *port, uint8_t *buffer, size_t len,
                                   unsigned int timeout_ms) {
    if (!port) return -1;
    auto *p = reinterpret_cast<windmi::platform::SerialPort*>(port);
    return p->read(buffer, len, timeout_ms);
}

extern "C" int windmi_serial_write(WindmiSerialPort *port, const uint8_t *buffer, size_t len) {
    if (!port) return -1;
    auto *p = reinterpret_cast<windmi::platform::SerialPort*>(port);
    return p->write(buffer, len);
}

extern "C" void windmi_serial_flush(WindmiSerialPort *port) {
    if (!port) return;
    auto *p = reinterpret_cast<windmi::platform::SerialPort*>(port);
    p->flush();
}
