#pragma once

#include <string>
#include <vector>
#include <map>
#include "settings.hpp"

namespace rag {

class OpenAIClient;
class Console;

// Result of file diff computation
struct FileDiff {
    std::vector<std::string> added;    // New files to upload
    std::vector<std::string> removed;  // Files to remove from vector store
    std::vector<std::string> modified; // Files that need re-upload (timestamp changed)
};

// Compute the diff between current files and previously indexed files
FileDiff compute_file_diff(
    const std::vector<std::string>& current_files,
    const std::map<std::string, FileMetadata>& indexed_files
);

// Get file modification timestamp
int64_t get_file_mtime(const std::string& filepath);

// Upload files and create a new vector store
// Returns the vector store ID and populates indexed_files map
std::string create_vector_store(
    const std::vector<std::string>& file_patterns,
    OpenAIClient& client,
    Console& console,
    std::map<std::string, FileMetadata>& indexed_files
);

// Apply incremental changes to an existing vector store
// Updates indexed_files map with new state
void update_vector_store(
    const std::string& vector_store_id,
    const FileDiff& diff,
    OpenAIClient& client,
    Console& console,
    std::map<std::string, FileMetadata>& indexed_files
);

} // namespace rag
