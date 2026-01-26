#pragma once

/**
 * File watcher for automatic reindexing.
 *
 * Monitors files matching the configured patterns and automatically
 * reindexes when changes are detected. On Linux, uses inotify for
 * efficient event-based watching. Falls back to polling on other platforms.
 */

#include "settings.hpp"
#include "providers/provider.hpp"
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <memory>

#ifdef __linux__
#include "inotify_watcher.hpp"
#endif

namespace rag {

class Console;

/**
 * Callback invoked when reindexing occurs.
 *
 * @param added Number of files added
 * @param modified Number of files modified
 * @param removed Number of files removed
 */
using ReindexCallback = std::function<void(size_t added, size_t modified, size_t removed)>;

/**
 * File watcher that automatically reindexes when files change.
 *
 * Runs a background thread that periodically checks for file changes
 * using the file patterns stored in settings. When changes are detected,
 * it calls update_vector_store() to sync with the AI provider.
 */
class FileWatcher {
public:
    /**
     * Creates a file watcher.
     *
     * @param settings Reference to settings (will be modified when reindexing)
     * @param provider Reference to AI provider for API calls
     * @param poll_interval_seconds How often to check for changes (default: 5)
     */
    FileWatcher(
        Settings& settings,
        providers::IAIProvider& provider,
        int poll_interval_seconds = 5
    );

    ~FileWatcher();

    /**
     * Sets a callback to be invoked when reindexing completes.
     * The callback receives counts of added, modified, and removed files.
     */
    void on_reindex(ReindexCallback callback);

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
    // Background thread function (polling fallback)
    void watch_loop();

    // Check for changes and reindex if needed
    void check_and_reindex();

    // Setup directory watches based on file patterns
    void setup_watches();

    Settings& settings_;
    providers::IAIProvider& provider_;
    int poll_interval_seconds_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::thread watch_thread_;
    std::mutex reindex_mutex_;  // Protects settings during reindex

    ReindexCallback on_reindex_callback_;

#ifdef __linux__
    std::unique_ptr<InotifyWatcher> inotify_watcher_;
    bool use_inotify_{true};  // Can be disabled if inotify fails
#endif
};

} // namespace rag
