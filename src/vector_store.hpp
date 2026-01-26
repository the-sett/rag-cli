#pragma once

/**
 * Vector store management for file indexing.
 *
 * Provides functions for creating and incrementally updating knowledge
 * stores based on local file changes. Works with any AI provider that
 * implements the IFilesService and IKnowledgeStore interfaces.
 */

#include <string>
#include <vector>
#include <map>
#include "settings.hpp"
#include "providers/provider.hpp"

namespace rag {

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
// Uses content hashes to avoid reindexing files that haven't actually changed.
// Updates indexed_files timestamps/hashes for files with unchanged content.
FileDiff compute_file_diff(
    const std::vector<std::string>& current_files,
    std::map<std::string, FileMetadata>& indexed_files
);

// Returns the modification timestamp of a file in seconds since epoch.
int64_t get_file_mtime(const std::string& filepath);

// Computes a hash of the file contents for change detection.
std::string compute_file_hash(const std::string& filepath);

/**
 * Uploads files and creates a new knowledge store.
 *
 * Returns the store ID and populates indexed_files map with metadata
 * for each uploaded file.
 */
std::string create_vector_store(
    const std::vector<std::string>& file_patterns,
    providers::IAIProvider& provider,
    Console& console,
    std::map<std::string, FileMetadata>& indexed_files
);

/**
 * Applies incremental changes to an existing knowledge store.
 *
 * Uploads new and modified files, removes deleted files from the store,
 * and updates indexed_files map with the new state.
 */
void update_vector_store(
    const std::string& store_id,
    const FileDiff& diff,
    providers::IAIProvider& provider,
    Console& console,
    std::map<std::string, FileMetadata>& indexed_files
);

/**
 * Completely rebuilds the knowledge store from scratch.
 *
 * This will:
 * 1. Delete all files from the existing store
 * 2. Delete all files from provider storage
 * 3. Delete the store itself
 * 4. Create a new store with a new ID
 * 5. Re-upload all files matching the patterns using parallel upload
 *
 * Returns the new store ID.
 */
std::string rebuild_vector_store(
    const std::string& old_store_id,
    const std::vector<std::string>& file_patterns,
    providers::IAIProvider& provider,
    Console& console,
    std::map<std::string, FileMetadata>& indexed_files
);

} // namespace rag
