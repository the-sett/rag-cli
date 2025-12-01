#include "vector_store.hpp"
#include "file_resolver.hpp"
#include "openai_client.hpp"
#include "console.hpp"
#include <filesystem>
#include <thread>
#include <chrono>
#include <set>

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

FileDiff compute_file_diff(
    const std::vector<std::string>& current_files,
    const std::map<std::string, FileMetadata>& indexed_files
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
            // File exists - check if modified
            int64_t current_mtime = get_file_mtime(filepath);
            if (current_mtime != it->second.last_modified) {
                diff.modified.push_back(filepath);
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
    console.print_warning("Uploading " + std::to_string(files_to_upload.size()) + " files...");

    std::vector<std::string> file_ids;
    size_t total_files = files_to_upload.size();

    // Clear any existing indexed files since we're creating fresh
    indexed_files.clear();

    for (size_t i = 0; i < files_to_upload.size(); ++i) {
        const auto& filepath = files_to_upload[i];
        std::string display_name = fs::path(filepath).filename().string();

        console.start_status("Uploading (" + std::to_string(i + 1) + "/" +
                            std::to_string(total_files) + "): " + display_name);

        try {
            std::string file_id = client.upload_file(filepath);
            file_ids.push_back(file_id);

            // Record the file metadata
            FileMetadata metadata;
            metadata.openai_file_id = file_id;
            metadata.last_modified = get_file_mtime(filepath);
            indexed_files[filepath] = metadata;

            console.clear_status();
            console.print_success("(" + std::to_string(i + 1) + "/" +
                                 std::to_string(total_files) + ") " + filepath);
        } catch (const std::exception& e) {
            console.clear_status();
            console.print_warning("Skipping " + filepath + ": " + e.what());
            // Continue with other files instead of stopping
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

            try {
                // Remove from vector store
                client.remove_file_from_vector_store(vector_store_id, it->second.openai_file_id);
                // Delete the file from OpenAI storage
                client.delete_file(it->second.openai_file_id);
                console.clear_status();
                console.print_warning("- " + filepath);
                indexed_files.erase(it);
            } catch (const std::exception& e) {
                console.clear_status();
                console.print_error("Failed to remove " + filepath + ": " + e.what());
                // Continue with other files
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
                // Remove old version from vector store
                client.remove_file_from_vector_store(vector_store_id, it->second.openai_file_id);
                // Delete old file from OpenAI storage
                client.delete_file(it->second.openai_file_id);

                // Upload new version
                std::string new_file_id = client.upload_file(filepath);
                // Add to vector store
                client.add_file_to_vector_store(vector_store_id, new_file_id);

                // Update metadata
                it->second.openai_file_id = new_file_id;
                it->second.last_modified = get_file_mtime(filepath);

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

            // Record metadata
            FileMetadata metadata;
            metadata.openai_file_id = file_id;
            metadata.last_modified = get_file_mtime(filepath);
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

} // namespace rag
