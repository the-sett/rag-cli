#pragma once

/**
 * Vector store management for file indexing.
 *
 * Provides functions for creating and incrementally updating OpenAI vector
 * stores based on local file changes.
 */

#include <string>
#include <vector>
#include <map>
#include "settings.hpp"

namespace rag {

class OpenAIClient;
class Console;

/**
 * Result of comparing current files against previously indexed files.
 */
struct FileDiff {
    std::vector<std::string> added;    // New files to upload.
    std::vector<std::string> removed;  // Files to remove from vector store.
    std::vector<std::string> modified; // Files that need re-upload (timestamp changed).
};

// Computes the diff between current files and previously indexed files.
FileDiff compute_file_diff(
    const std::vector<std::string>& current_files,
    const std::map<std::string, FileMetadata>& indexed_files
);

// Returns the modification timestamp of a file in seconds since epoch.
int64_t get_file_mtime(const std::string& filepath);

/**
 * Uploads files and creates a new vector store.
 *
 * Returns the vector store ID and populates indexed_files map with metadata
 * for each uploaded file.
 */
std::string create_vector_store(
    const std::vector<std::string>& file_patterns,
    OpenAIClient& client,
    Console& console,
    std::map<std::string, FileMetadata>& indexed_files
);

/**
 * Applies incremental changes to an existing vector store.
 *
 * Uploads new and modified files, removes deleted files from the store,
 * and updates indexed_files map with the new state.
 */
void update_vector_store(
    const std::string& vector_store_id,
    const FileDiff& diff,
    OpenAIClient& client,
    Console& console,
    std::map<std::string, FileMetadata>& indexed_files
);

} // namespace rag
