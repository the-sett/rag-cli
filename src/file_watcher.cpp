#include "file_watcher.hpp"
#include "vector_store.hpp"
#include "file_resolver.hpp"
#include "console.hpp"
#include <iostream>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace rag {

FileWatcher::FileWatcher(
    Settings& settings,
    providers::IAIProvider& provider,
    int poll_interval_seconds
)
    : settings_(settings)
    , provider_(provider)
    , poll_interval_seconds_(poll_interval_seconds)
{
#ifdef __linux__
    // Create inotify watcher with 500ms debounce
    inotify_watcher_ = std::make_unique<InotifyWatcher>(500);
    inotify_watcher_->on_change([this]() {
        check_and_reindex();
    });
#endif
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

#ifdef __linux__
    if (use_inotify_ && inotify_watcher_) {
        setup_watches();
        inotify_watcher_->start();
        std::cerr << "[FileWatcher] Started (inotify mode - passive)" << std::endl;
        return;
    }
#endif

    // Fallback to polling
    watch_thread_ = std::thread([this]() {
        watch_loop();
    });
    std::cerr << "[FileWatcher] Started (polling mode - " << poll_interval_seconds_ << "s interval)" << std::endl;
}

void FileWatcher::stop() {
    if (!running_.load()) {
        return;  // Not running
    }

    stop_requested_.store(true);

#ifdef __linux__
    if (inotify_watcher_ && inotify_watcher_->is_running()) {
        inotify_watcher_->stop();
    }
#endif

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
        // Apply the changes to the knowledge store
        update_vector_store(
            settings_.vector_store_id,
            diff,
            provider_,
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

void FileWatcher::setup_watches() {
#ifdef __linux__
    if (!inotify_watcher_) {
        return;
    }

    // Extract directories to watch from file patterns
    std::unordered_set<std::string> dirs_to_watch;

    for (const auto& pattern : settings_.file_patterns) {
        fs::path p(pattern);

        // Check if pattern is a glob pattern
        bool is_glob = pattern.find('*') != std::string::npos ||
                       pattern.find('?') != std::string::npos ||
                       pattern.find('[') != std::string::npos;

        if (!is_glob) {
            // Literal path
            if (fs::exists(p)) {
                if (fs::is_directory(p)) {
                    dirs_to_watch.insert(fs::absolute(p).string());
                } else {
                    dirs_to_watch.insert(fs::absolute(p.parent_path()).string());
                }
            }
        } else {
            // Glob pattern - find base directory
            fs::path base_dir = ".";
            for (const auto& component : p) {
                std::string comp_str = component.string();
                bool comp_is_glob = comp_str.find('*') != std::string::npos ||
                                    comp_str.find('?') != std::string::npos ||
                                    comp_str.find('[') != std::string::npos;
                if (comp_is_glob) {
                    break;
                }
                if (base_dir == ".") {
                    base_dir = component;
                } else {
                    base_dir /= component;
                }
            }

            if (fs::exists(base_dir) && fs::is_directory(base_dir)) {
                dirs_to_watch.insert(fs::absolute(base_dir).string());
            } else {
                // Watch current directory as fallback
                dirs_to_watch.insert(fs::absolute(".").string());
            }
        }
    }

    // Add watches for all directories
    for (const auto& dir : dirs_to_watch) {
        inotify_watcher_->add_watch(dir);
    }

    std::cerr << "[FileWatcher] Watching " << dirs_to_watch.size() << " directories" << std::endl;
#endif
}

} // namespace rag
