#pragma once

/**
 * Settings persistence for the crag CLI.
 *
 * Handles loading and saving of application settings to a local JSON file,
 * including model selection, vector store ID, and indexed file metadata.
 */

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <cstdint>

namespace rag {

/**
 * Metadata for a single indexed file.
 *
 * Tracks the OpenAI file ID and modification timestamp to enable incremental
 * updates when files change.
 */
struct FileMetadata {
    std::string openai_file_id;  // OpenAI file ID for this file.
    int64_t last_modified;       // Unix timestamp of last modification.
};

/**
 * Application settings stored in .crag.json.
 *
 * Contains model configuration, vector store reference, and metadata for
 * all indexed files.
 */
struct Settings {
    std::string model;                                    // Selected OpenAI model.
    std::string reasoning_effort;                         // Thinking level (low/medium/high).
    std::string vector_store_id;                          // OpenAI vector store ID.
    std::vector<std::string> file_patterns;               // Original glob patterns.
    std::map<std::string, FileMetadata> indexed_files;    // Filepath to metadata mapping.

    // Returns true if settings contain required fields for operation.
    bool is_valid() const {
        return !model.empty() && !vector_store_id.empty();
    }
};

// Loads settings from .crag.json. Returns empty optional if file doesn't exist.
std::optional<Settings> load_settings();

// Saves settings to .crag.json.
void save_settings(const Settings& settings);

} // namespace rag
