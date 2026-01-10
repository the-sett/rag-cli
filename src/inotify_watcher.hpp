#pragma once

/**
 * Linux inotify-based file watcher.
 *
 * Uses the kernel's inotify subsystem for efficient, passive file change
 * detection. The process blocks on a file descriptor until the kernel
 * notifies of changes, eliminating CPU-intensive polling.
 */

#ifdef __linux__

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace rag {

/**
 * Callback invoked when file changes are detected.
 * Called after debouncing, so rapid changes are batched.
 */
using FileChangeCallback = std::function<void()>;

/**
 * Efficient file watcher using Linux inotify.
 *
 * Watches directories for file changes and invokes a callback when
 * modifications are detected. Uses the kernel's event notification
 * system instead of polling, resulting in near-zero CPU usage when idle.
 *
 * Features:
 * - Passive waiting (no polling)
 * - Automatic recursive directory watching
 * - Debouncing to batch rapid changes
 * - Handles new subdirectory creation
 */
class InotifyWatcher {
public:
    /**
     * Creates an inotify watcher.
     *
     * @param debounce_ms Milliseconds to wait after last event before callback
     */
    explicit InotifyWatcher(int debounce_ms = 500);

    ~InotifyWatcher();

    // Non-copyable
    InotifyWatcher(const InotifyWatcher&) = delete;
    InotifyWatcher& operator=(const InotifyWatcher&) = delete;

    /**
     * Sets the callback to invoke when files change.
     */
    void on_change(FileChangeCallback callback);

    /**
     * Adds a directory to watch (recursively includes subdirectories).
     *
     * @param path Directory path to watch
     * @return true if watch was added successfully
     */
    bool add_watch(const std::string& path);

    /**
     * Starts watching for file changes in a background thread.
     * Does nothing if already running.
     */
    void start();

    /**
     * Stops watching for file changes.
     * Blocks until the background thread has stopped.
     */
    void stop();

    /**
     * Returns true if the watcher is currently running.
     */
    bool is_running() const { return running_.load(); }

private:
    // Background thread function
    void watch_loop();

    // Add watch for a single directory (non-recursive)
    bool add_single_watch(const std::string& path);

    // Recursively add watches for all subdirectories
    void add_watches_recursive(const std::string& path);

    int inotify_fd_{-1};
    int pipe_fd_[2]{-1, -1};  // For signaling shutdown

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::thread watch_thread_;

    // Map from watch descriptor to directory path
    std::unordered_map<int, std::string> wd_to_path_;
    // Set of watched paths to avoid duplicates
    std::unordered_set<std::string> watched_paths_;
    std::mutex watch_mutex_;

    FileChangeCallback on_change_callback_;
    int debounce_ms_;

    // Debouncing state
    std::atomic<bool> change_pending_{false};
};

} // namespace rag

#endif // __linux__
