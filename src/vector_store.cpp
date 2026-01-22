#include "vector_store.hpp"
#include "file_resolver.hpp"
#include "openai_client.hpp"
#include "console.hpp"
#include <filesystem>
#include <thread>
#include <chrono>
#include <set>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

namespace rag {

int64_t get_file_mtime(const std::string& filepath) {
    try {
        auto ftime = fs::last_write_time(filepath);
        // Convert file_time_type to a duration since epoch
        // In C++17, we need to use the file_time_type's duration directly
        auto duration = ftime.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
        return seconds.count();
    } catch (const fs::filesystem_error&) {
        return 0;
    }
}

std::string compute_file_hash(const std::string& filepath) {
    // FNV-1a 64-bit hash - fast, non-cryptographic hash suitable for change detection
    constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    constexpr uint64_t FNV_PRIME = 1099511628211ULL;

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    uint64_t hash = FNV_OFFSET_BASIS;
    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        for (std::streamsize i = 0; i < file.gcount(); ++i) {
            hash ^= static_cast<uint8_t>(buffer[i]);
            hash *= FNV_PRIME;
        }
    }

    // Convert to hex string
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

FileDiff compute_file_diff(
    const std::vector<std::string>& current_files,
    std::map<std::string, FileMetadata>& indexed_files
) {
    FileDiff diff;

    // Convert current files to a set for fast lookup
    std::set<std::string> current_set(current_files.begin(), current_files.end());

    // Find added and modified files
    for (const auto& filepath : current_files) {
        auto it = indexed_files.find(filepath);
        if (it == indexed_files.end()) {
            // File is new
            diff.added.push_back(filepath);
        } else {
            // File exists - check if modified by timestamp first (fast check)
            int64_t current_mtime = get_file_mtime(filepath);
            if (current_mtime != it->second.last_modified) {
                // Timestamp changed - check if content actually changed using hash
                std::string current_hash = compute_file_hash(filepath);

                if (it->second.content_hash.empty()) {
                    // No stored hash - must reindex (legacy entry without hash)
                    diff.modified.push_back(filepath);
                } else if (current_hash != it->second.content_hash) {
                    // Hash differs - content changed, needs reindex
                    diff.modified.push_back(filepath);
                } else {
                    // Hash matches - content unchanged, just update timestamp
                    it->second.last_modified = current_mtime;
                }
            }
        }
    }

    // Find removed files
    for (const auto& [filepath, metadata] : indexed_files) {
        if (current_set.find(filepath) == current_set.end()) {
            diff.removed.push_back(filepath);
        }
    }

    return diff;
}

std::string create_vector_store(
    const std::vector<std::string>& file_patterns,
    OpenAIClient& client,
    Console& console,
    std::map<std::string, FileMetadata>& indexed_files
) {
    // Resolve file patterns to actual files
    std::vector<std::string> files_to_upload = resolve_file_patterns(file_patterns, console);

    if (files_to_upload.empty()) {
        console.print_error("Error: No supported files found");
        console.println();
        console.println("Usage: crag 'docs/*.md' 'src/**/*.py'");
        console.println("Supported: .txt, .md, .pdf, .py, .js, .json, .yaml, and many more");
        return "";
    }

    console.println();
    console.print_warning("Uploading " + std::to_string(files_to_upload.size()) + " files (8 parallel connections)...");

    std::vector<std::string> file_ids;

    // Clear any existing indexed files since we're creating fresh
    indexed_files.clear();

    // Upload files in parallel
    auto results = client.upload_files_parallel(
        files_to_upload,
        [&console](size_t completed, size_t total) {
            console.start_status("Uploading (" + std::to_string(completed) + "/" +
                                std::to_string(total) + ")...");
        },
        8  // max parallel connections
    );

    console.clear_status();

    // Process results
    for (const auto& result : results) {
        if (result.success()) {
            file_ids.push_back(result.file_id);

            // Record the file metadata including content hash
            FileMetadata metadata;
            metadata.openai_file_id = result.file_id;
            metadata.last_modified = get_file_mtime(result.filepath);
            metadata.content_hash = compute_file_hash(result.filepath);
            indexed_files[result.filepath] = metadata;

            console.print_success(result.filepath);
        } else {
            console.print_warning("Skipping " + result.filepath + ": " + result.error);
        }
    }

    console.println();

    if (file_ids.empty()) {
        console.print_error("Error: No files were successfully uploaded");
        return "";
    }

    console.print_warning("Creating vector store...");

    std::string vector_store_id;
    try {
        vector_store_id = client.create_vector_store("cli-rag-store");
        console.print_success("Vector store created: " + vector_store_id);
    } catch (const std::exception& e) {
        console.print_error("Failed to create vector store: " + std::string(e.what()));
        return "";
    }

    console.print_warning("Starting batch indexing...");

    std::string batch_id;
    try {
        batch_id = client.create_file_batch(vector_store_id, file_ids);
    } catch (const std::exception& e) {
        console.print_error("Failed to create file batch: " + std::string(e.what()));
        return "";
    }

    console.start_status("Indexing " + std::to_string(file_ids.size()) +
                        " files (this may take a minute)...");

    // Poll for batch completion
    while (true) {
        try {
            std::string status = client.get_batch_status(vector_store_id, batch_id);

            if (status == "completed") {
                break;
            } else if (status == "failed") {
                console.clear_status();
                console.print_error("Error: Vector store indexing failed");
                return "";
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        } catch (const std::exception& e) {
            console.clear_status();
            console.print_error("Error checking batch status: " + std::string(e.what()));
            return "";
        }
    }

    console.clear_status();
    console.print_success("Vector store ready.");

    return vector_store_id;
}

void update_vector_store(
    const std::string& vector_store_id,
    const FileDiff& diff,
    OpenAIClient& client,
    Console& console,
    std::map<std::string, FileMetadata>& indexed_files
) {
    size_t total_changes = diff.added.size() + diff.modified.size() + diff.removed.size();

    if (total_changes == 0) {
        console.print_success("No changes detected. Vector store is up to date.");
        return;
    }

    console.println();
    console.print_info("Changes detected:");
    if (!diff.added.empty()) {
        console.println("  + " + std::to_string(diff.added.size()) + " new file(s)");
    }
    if (!diff.modified.empty()) {
        console.println("  ~ " + std::to_string(diff.modified.size()) + " modified file(s)");
    }
    if (!diff.removed.empty()) {
        console.println("  - " + std::to_string(diff.removed.size()) + " removed file(s)");
    }
    console.println();

    // Process removals first
    for (const auto& filepath : diff.removed) {
        auto it = indexed_files.find(filepath);
        if (it != indexed_files.end()) {
            std::string display_name = fs::path(filepath).filename().string();
            console.start_status("Removing: " + display_name);

            bool removal_ok = true;
            try {
                // Remove from vector store - ignore "not found" errors
                try {
                    client.remove_file_from_vector_store(vector_store_id, it->second.openai_file_id);
                } catch (const std::exception& e) {
                    std::string err(e.what());
                    if (err.find("No such") == std::string::npos &&
                        err.find("not found") == std::string::npos) {
                        throw;
                    }
                }

                // Delete the file from OpenAI storage - ignore "not found" errors
                try {
                    client.delete_file(it->second.openai_file_id);
                } catch (const std::exception& e) {
                    std::string err(e.what());
                    if (err.find("No such") == std::string::npos &&
                        err.find("not found") == std::string::npos) {
                        throw;
                    }
                }
            } catch (const std::exception& e) {
                console.clear_status();
                console.print_error("Failed to remove " + filepath + ": " + e.what());
                removal_ok = false;
            }

            if (removal_ok) {
                console.clear_status();
                console.print_warning("- " + filepath);
                indexed_files.erase(it);
            }
        }
    }

    // Process modifications (remove old, add new)
    for (const auto& filepath : diff.modified) {
        auto it = indexed_files.find(filepath);
        if (it != indexed_files.end()) {
            std::string display_name = fs::path(filepath).filename().string();
            console.start_status("Updating: " + display_name);

            try {
                // Try to remove old version from vector store and storage.
                // If the file no longer exists in OpenAI (e.g., was cleaned up),
                // we treat that as success and proceed with uploading the new version.
                try {
                    client.remove_file_from_vector_store(vector_store_id, it->second.openai_file_id);
                } catch (const std::exception& e) {
                    // Ignore "not found" errors - file may already be removed
                    std::string err(e.what());
                    if (err.find("No such") == std::string::npos &&
                        err.find("not found") == std::string::npos) {
                        throw;  // Re-throw other errors
                    }
                }

                try {
                    client.delete_file(it->second.openai_file_id);
                } catch (const std::exception& e) {
                    // Ignore "not found" errors - file may already be deleted
                    std::string err(e.what());
                    if (err.find("No such") == std::string::npos &&
                        err.find("not found") == std::string::npos) {
                        throw;  // Re-throw other errors
                    }
                }

                // Upload new version
                std::string new_file_id = client.upload_file(filepath);
                // Add to vector store
                client.add_file_to_vector_store(vector_store_id, new_file_id);

                // Update metadata including content hash
                it->second.openai_file_id = new_file_id;
                it->second.last_modified = get_file_mtime(filepath);
                it->second.content_hash = compute_file_hash(filepath);

                console.clear_status();
                console.print_info("~ " + filepath);
            } catch (const std::exception& e) {
                console.clear_status();
                console.print_error("Failed to update " + filepath + ": " + e.what());
                // Continue with other files
            }
        }
    }

    // Process additions
    for (const auto& filepath : diff.added) {
        std::string display_name = fs::path(filepath).filename().string();
        console.start_status("Adding: " + display_name);

        try {
            // Upload file
            std::string file_id = client.upload_file(filepath);
            // Add to vector store
            client.add_file_to_vector_store(vector_store_id, file_id);

            // Record metadata including content hash
            FileMetadata metadata;
            metadata.openai_file_id = file_id;
            metadata.last_modified = get_file_mtime(filepath);
            metadata.content_hash = compute_file_hash(filepath);
            indexed_files[filepath] = metadata;

            console.clear_status();
            console.print_success("+ " + filepath);
        } catch (const std::exception& e) {
            console.clear_status();
            console.print_error("Failed to add " + filepath + ": " + e.what());
            // Continue with other files
        }
    }

    console.println();
    console.print_success("Vector store updated.");
}

std::string rebuild_vector_store(
    const std::string& old_vector_store_id,
    const std::vector<std::string>& file_patterns,
    OpenAIClient& client,
    Console& console,
    std::map<std::string, FileMetadata>& indexed_files
) {
    console.println();
    console.print_header("=== Rebuilding Vector Store ===");
    console.println();

    // Step 1: Delete all files from the vector store and OpenAI storage (in parallel)
    if (!indexed_files.empty()) {
        console.print_warning("Deleting " + std::to_string(indexed_files.size()) + " files (8 parallel connections)...");

        // Collect all file IDs
        std::vector<std::string> file_ids;
        file_ids.reserve(indexed_files.size());
        for (const auto& [filepath, metadata] : indexed_files) {
            file_ids.push_back(metadata.openai_file_id);
        }

        // Delete files in parallel
        auto results = client.delete_files_parallel(
            old_vector_store_id,
            file_ids,
            [&console](size_t completed, size_t total) {
                console.start_status("Deleting (" + std::to_string(completed) + "/" +
                                    std::to_string(total) + ")...");
            },
            8  // max parallel connections
        );

        console.clear_status();

        // Count successes and report errors
        size_t deleted = 0;
        for (const auto& result : results) {
            if (result.success()) {
                deleted++;
            } else {
                console.print_error("Failed to delete " + result.file_id + ": " + result.error);
            }
        }

        console.print_success("Deleted " + std::to_string(deleted) + " files from storage.");
    }

    // Step 2: Delete the vector store itself
    console.print_warning("Deleting vector store: " + old_vector_store_id);
    try {
        client.delete_vector_store(old_vector_store_id);
        console.print_success("Vector store deleted.");
    } catch (const std::exception& e) {
        std::string err(e.what());
        // Ignore "not found" errors - store may already be deleted
        if (err.find("No such") == std::string::npos &&
            err.find("not found") == std::string::npos) {
            console.print_error("Failed to delete vector store: " + std::string(e.what()));
            // Continue anyway - we'll create a new one
        } else {
            console.print_warning("Vector store already deleted or not found.");
        }
    }
    console.println();

    // Step 3 & 4: Clear indexed files and create a new vector store with fresh uploads
    // This reuses the existing create_vector_store function which handles parallel uploads
    indexed_files.clear();
    return create_vector_store(file_patterns, client, console, indexed_files);
}

} // namespace rag
