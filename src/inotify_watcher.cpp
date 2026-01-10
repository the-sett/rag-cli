#ifdef __linux__

#include "inotify_watcher.hpp"
#include <sys/inotify.h>
#include <sys/select.h>
#include <unistd.h>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <cerrno>

namespace fs = std::filesystem;

namespace rag {

// Events we care about for file watching
static constexpr uint32_t WATCH_EVENTS =
    IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE;

InotifyWatcher::InotifyWatcher(int debounce_ms)
    : debounce_ms_(debounce_ms)
{
    inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ < 0) {
        std::cerr << "[InotifyWatcher] Failed to initialize inotify: "
                  << strerror(errno) << std::endl;
    }

    // Create pipe for signaling shutdown
    if (pipe(pipe_fd_) < 0) {
        std::cerr << "[InotifyWatcher] Failed to create pipe: "
                  << strerror(errno) << std::endl;
    }
}

InotifyWatcher::~InotifyWatcher() {
    stop();

    if (inotify_fd_ >= 0) {
        close(inotify_fd_);
    }
    if (pipe_fd_[0] >= 0) {
        close(pipe_fd_[0]);
    }
    if (pipe_fd_[1] >= 0) {
        close(pipe_fd_[1]);
    }
}

void InotifyWatcher::on_change(FileChangeCallback callback) {
    on_change_callback_ = std::move(callback);
}

bool InotifyWatcher::add_single_watch(const std::string& path) {
    if (inotify_fd_ < 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(watch_mutex_);

    // Check if already watching
    if (watched_paths_.count(path) > 0) {
        return true;
    }

    int wd = inotify_add_watch(inotify_fd_, path.c_str(), WATCH_EVENTS);
    if (wd < 0) {
        if (errno != ENOENT && errno != EACCES) {
            std::cerr << "[InotifyWatcher] Failed to watch " << path << ": "
                      << strerror(errno) << std::endl;
        }
        return false;
    }

    wd_to_path_[wd] = path;
    watched_paths_.insert(path);
    return true;
}

void InotifyWatcher::add_watches_recursive(const std::string& path) {
    try {
        // Add watch for the directory itself
        add_single_watch(path);

        // Recursively add watches for subdirectories
        for (const auto& entry : fs::recursive_directory_iterator(
                path, fs::directory_options::skip_permission_denied)) {
            if (entry.is_directory()) {
                add_single_watch(entry.path().string());
            }
        }
    } catch (const fs::filesystem_error& e) {
        // Silently skip directories we can't access
    }
}

bool InotifyWatcher::add_watch(const std::string& path) {
    try {
        fs::path p(path);
        if (!fs::exists(p)) {
            std::cerr << "[InotifyWatcher] Path does not exist: " << path << std::endl;
            return false;
        }

        if (fs::is_directory(p)) {
            add_watches_recursive(path);
        } else {
            // For files, watch the parent directory
            add_single_watch(p.parent_path().string());
        }
        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[InotifyWatcher] Error adding watch: " << e.what() << std::endl;
        return false;
    }
}

void InotifyWatcher::start() {
    if (running_.load() || inotify_fd_ < 0) {
        return;
    }

    stop_requested_.store(false);
    running_.store(true);

    watch_thread_ = std::thread([this]() {
        watch_loop();
    });
}

void InotifyWatcher::stop() {
    if (!running_.load()) {
        return;
    }

    stop_requested_.store(true);

    // Write to pipe to wake up select()
    if (pipe_fd_[1] >= 0) {
        char c = 'x';
        ssize_t written = write(pipe_fd_[1], &c, 1);
        (void)written;  // Ignore result
    }

    if (watch_thread_.joinable()) {
        watch_thread_.join();
    }

    running_.store(false);
}

void InotifyWatcher::watch_loop() {
    constexpr size_t EVENT_BUF_SIZE = 4096;
    alignas(struct inotify_event) char buffer[EVENT_BUF_SIZE];

    auto last_event_time = std::chrono::steady_clock::now();
    bool has_pending_changes = false;

    while (!stop_requested_.load()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(inotify_fd_, &read_fds);
        FD_SET(pipe_fd_[0], &read_fds);

        int max_fd = std::max(inotify_fd_, pipe_fd_[0]);

        // Set timeout for debouncing
        struct timeval timeout;
        if (has_pending_changes) {
            // Short timeout to check debounce
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;  // 100ms
        } else {
            // Long timeout when idle
            timeout.tv_sec = 60;
            timeout.tv_usec = 0;
        }

        int result = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);

        if (stop_requested_.load()) {
            break;
        }

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "[InotifyWatcher] select() error: " << strerror(errno) << std::endl;
            break;
        }

        // Check for shutdown signal
        if (FD_ISSET(pipe_fd_[0], &read_fds)) {
            break;
        }

        // Process inotify events
        if (FD_ISSET(inotify_fd_, &read_fds)) {
            ssize_t len = read(inotify_fd_, buffer, EVENT_BUF_SIZE);
            if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                std::cerr << "[InotifyWatcher] read() error: " << strerror(errno) << std::endl;
                break;
            }

            // Process events
            const char* ptr = buffer;
            while (ptr < buffer + len) {
                const struct inotify_event* event =
                    reinterpret_cast<const struct inotify_event*>(ptr);

                // Handle new directory creation - add watch for it
                if ((event->mask & IN_CREATE) && (event->mask & IN_ISDIR)) {
                    std::lock_guard<std::mutex> lock(watch_mutex_);
                    auto it = wd_to_path_.find(event->wd);
                    if (it != wd_to_path_.end() && event->len > 0) {
                        std::string new_dir = it->second + "/" + event->name;
                        // Add watch outside of lock to avoid deadlock
                        watch_mutex_.unlock();
                        add_watches_recursive(new_dir);
                        watch_mutex_.lock();
                    }
                }

                // Handle directory deletion - remove from our tracking
                if ((event->mask & IN_DELETE_SELF) || (event->mask & IN_IGNORED)) {
                    std::lock_guard<std::mutex> lock(watch_mutex_);
                    auto it = wd_to_path_.find(event->wd);
                    if (it != wd_to_path_.end()) {
                        watched_paths_.erase(it->second);
                        wd_to_path_.erase(it);
                    }
                }

                // Mark that we have changes pending
                has_pending_changes = true;
                last_event_time = std::chrono::steady_clock::now();

                ptr += sizeof(struct inotify_event) + event->len;
            }
        }

        // Check debounce timeout
        if (has_pending_changes) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_event_time).count();

            if (elapsed >= debounce_ms_) {
                has_pending_changes = false;

                // Invoke callback
                if (on_change_callback_) {
                    try {
                        on_change_callback_();
                    } catch (const std::exception& e) {
                        std::cerr << "[InotifyWatcher] Callback error: " << e.what() << std::endl;
                    }
                }
            }
        }
    }
}

} // namespace rag

#endif // __linux__
