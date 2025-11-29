#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <cstdint>

namespace rag {

// Metadata for a single indexed file
struct FileMetadata {
    std::string openai_file_id;  // OpenAI file ID
    int64_t last_modified;       // Unix timestamp of last modification
};

struct Settings {
    std::string model;
    std::string reasoning_effort;
    std::string vector_store_id;
    std::vector<std::string> file_patterns;  // Original glob patterns
    std::map<std::string, FileMetadata> indexed_files;  // filepath -> metadata

    bool is_valid() const {
        return !model.empty() && !vector_store_id.empty();
    }
};

// Load settings from settings.json, returns empty optional if file doesn't exist
std::optional<Settings> load_settings();

// Save settings to settings.json
void save_settings(const Settings& settings);

} // namespace rag
