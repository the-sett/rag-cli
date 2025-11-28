#pragma once

#include <string>
#include <vector>
#include <optional>

namespace rag {

struct Settings {
    std::string model;
    std::string reasoning_effort;
    std::string vector_store_id;
    std::vector<std::string> file_patterns;

    bool is_valid() const {
        return !model.empty() && !vector_store_id.empty();
    }
};

// Load settings from settings.json, returns empty optional if file doesn't exist
std::optional<Settings> load_settings();

// Save settings to settings.json
void save_settings(const Settings& settings);

} // namespace rag
