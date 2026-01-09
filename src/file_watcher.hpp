#pragma once

/**
 * File watcher for automatic reindexing.
 *
 * Monitors files matching the configured patterns and automatically
 * reindexes when changes are detected. Uses a polling-based approach
 * with debouncing to batch rapid changes.
 */

#include "settings.hpp"
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

namespace rag {

class OpenAIClient;
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
 * it calls update_vector_store() to sync with OpenAI.
 */
class FileWatcher {
public:
    /**
     * Creates a file watcher.
     *
     * @param settings Reference to settings (will be modified when reindexing)
     * @param client Reference to OpenAI client for API calls
     * @param poll_interval_seconds How often to check for changes (default: 5)
     */
    FileWatcher(
        Settings& settings,
        OpenAIClient& client,
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
    // Background thread function
    void watch_loop();

    // Check for changes and reindex if needed
    void check_and_reindex();

    Settings& settings_;
    OpenAIClient& client_;
    int poll_interval_seconds_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::thread watch_thread_;
    std::mutex reindex_mutex_;  // Protects settings during reindex

    ReindexCallback on_reindex_callback_;
};

} // namespace rag
