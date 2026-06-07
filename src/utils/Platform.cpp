#include "utils/Platform.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <windows.h>
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
static volatile sig_atomic_t g_running_flag = 1;
static BOOL WINAPI console_control_handler(DWORD dwCtrlType) {
    switch (dwCtrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_running_flag = 0;
            return TRUE;
        default:
            return FALSE;
    }
}

void install_signal_handlers(volatile sig_atomic_t* running_flag) {
    g_running_flag = 1;
    if (SetConsoleCtrlHandler(console_control_handler, TRUE)) {
        *running_flag = 1;
    } else {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to install console control handler");
    }
}

bool acquire_instance_lock(bool force) {
    std::string lock_path = get_lock_path();
    // Convert path to Windows format (replace /tmp with a valid Windows temp path)
    std::string win_lock_path = lock_path;
    if (lock_path.find("/tmp/") == 0) {
        char temp_path[MAX_PATH];
        GetTempPathA(MAX_PATH, temp_path);
        win_lock_path = std::string(temp_path) + "windmi-controller.lock";
    }

    // Create a named mutex for instance locking
    HANDLE mutex = CreateMutexA(nullptr, FALSE, "Global\\windmi_controller_lock");
    if (mutex == nullptr) {
        WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Failed to create mutex: %lu", GetLastError());
        return false;
    }

    // Check if we got exclusive access
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!force) {
            WINDMI_LOG_ERROR(LOG_TAG_PLATFORM, "Another instance is already running");
            CloseHandle(mutex);
            return false;
        }
        WINDMI_LOG_WARN(LOG_TAG_PLATFORM, "--force: overriding existing instance lock");
    }

    // Store the mutex handle somewhere - we need a global for this
    // For simplicity, we'll just return success for now
    // In a full implementation, you'd store the handle in a thread-safe manner
    return true;
}

void release_instance_lock() {
    // On Windows, closing the handle releases the mutex
    // We need to track the mutex handle globally
    // For now, this is a no-op placeholder
}

bool is_pid_alive(int pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
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

    // Keep fd open to hold the lock
    // In a full implementation, this would be stored globally
    return true;
}

void release_instance_lock() {
    // This would need to track the fd globally
    // For now, this is a no-op placeholder
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
#ifdef _WIN32
    InitializeCriticalSection(&handle_);
#else
    pthread_mutex_init(&mutex_, nullptr);
#endif
}

Mutex::~Mutex() {
#ifdef _WIN32
    DeleteCriticalSection(&handle_);
#else
    pthread_mutex_destroy(&mutex_);
#endif
}

void Mutex::lock() {
#ifdef _WIN32
    EnterCriticalSection(&handle_);
#else
    pthread_mutex_lock(&mutex_);
#endif
}

void Mutex::unlock() {
#ifdef _WIN32
    LeaveCriticalSection(&handle_);
#else
    pthread_mutex_unlock(&mutex_);
#endif
}

}  // namespace windmi
