#include "file_watcher.hpp"
#include "vector_store.hpp"
#include "file_resolver.hpp"
#include "openai_client.hpp"
#include "console.hpp"
#include <iostream>

namespace rag {

FileWatcher::FileWatcher(
    Settings& settings,
    OpenAIClient& client,
    int poll_interval_seconds
)
    : settings_(settings)
    , client_(client)
    , poll_interval_seconds_(poll_interval_seconds)
{
}

FileWatcher::~FileWatcher() {
    stop();
}

void FileWatcher::on_reindex(ReindexCallback callback) {
    on_reindex_callback_ = std::move(callback);
}

void FileWatcher::start() {
    if (running_.load()) {
        return;  // Already running
    }

    stop_requested_.store(false);
    running_.store(true);

    watch_thread_ = std::thread([this]() {
        watch_loop();
    });
}

void FileWatcher::stop() {
    if (!running_.load()) {
        return;  // Not running
    }

    stop_requested_.store(true);

    if (watch_thread_.joinable()) {
        watch_thread_.join();
    }

    running_.store(false);
}

void FileWatcher::watch_loop() {
    while (!stop_requested_.load()) {
        // Sleep in small increments to allow quick shutdown
        for (int i = 0; i < poll_interval_seconds_ * 10 && !stop_requested_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (stop_requested_.load()) {
            break;
        }

        check_and_reindex();
    }
}

void FileWatcher::check_and_reindex() {
    std::lock_guard<std::mutex> lock(reindex_mutex_);

    // Skip if no patterns configured
    if (settings_.file_patterns.empty()) {
        return;
    }

    // Create a quiet console for file resolution (suppresses warnings)
    // We use a local Console instance to avoid polluting output
    Console console;

    // Resolve current files
    std::vector<std::string> current_files = resolve_file_patterns(
        settings_.file_patterns,
        console
    );

    // Compute diff against indexed files
    FileDiff diff = compute_file_diff(current_files, settings_.indexed_files);

    // Check if there are any changes
    size_t total_changes = diff.added.size() + diff.modified.size() + diff.removed.size();
    if (total_changes == 0) {
        return;  // No changes
    }

    // Log the changes
    std::cerr << "[FileWatcher] Detected changes: "
              << diff.added.size() << " added, "
              << diff.modified.size() << " modified, "
              << diff.removed.size() << " removed" << std::endl;

    try {
        // Apply the changes to the vector store
        update_vector_store(
            settings_.vector_store_id,
            diff,
            client_,
            console,
            settings_.indexed_files
        );

        // Save updated settings
        save_settings(settings_);

        std::cerr << "[FileWatcher] Reindex complete" << std::endl;

        // Invoke callback if set
        if (on_reindex_callback_) {
            on_reindex_callback_(diff.added.size(), diff.modified.size(), diff.removed.size());
        }
    } catch (const std::exception& e) {
        std::cerr << "[FileWatcher] Reindex error: " << e.what() << std::endl;
    }
}

} // namespace rag
