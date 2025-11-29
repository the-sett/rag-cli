#include "settings.hpp"
#include "config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace rag {

using json = nlohmann::json;

std::optional<Settings> load_settings() {
    if (!std::filesystem::exists(SETTINGS_FILE)) {
        return std::nullopt;
    }

    std::ifstream file(SETTINGS_FILE);
    if (!file.is_open()) {
        return std::nullopt;
    }

    try {
        json j;
        file >> j;

        Settings settings;
        settings.model = j.value("model", "");
        settings.reasoning_effort = j.value("reasoning_effort", "");
        settings.vector_store_id = j.value("vector_store_id", "");

        if (j.contains("file_patterns") && j["file_patterns"].is_array()) {
            for (const auto& pattern : j["file_patterns"]) {
                if (pattern.is_string()) {
                    settings.file_patterns.push_back(pattern.get<std::string>());
                }
            }
        }

        if (j.contains("indexed_files") && j["indexed_files"].is_object()) {
            for (const auto& [filepath, metadata] : j["indexed_files"].items()) {
                FileMetadata fm;
                fm.openai_file_id = metadata.value("openai_file_id", "");
                fm.last_modified = metadata.value("last_modified", int64_t(0));
                settings.indexed_files[filepath] = fm;
            }
        }

        return settings;
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

void save_settings(const Settings& settings) {
    json j;
    j["model"] = settings.model;
    j["reasoning_effort"] = settings.reasoning_effort;
    j["vector_store_id"] = settings.vector_store_id;
    j["file_patterns"] = settings.file_patterns;

    json indexed_files_json = json::object();
    for (const auto& [filepath, metadata] : settings.indexed_files) {
        indexed_files_json[filepath] = {
            {"openai_file_id", metadata.openai_file_id},
            {"last_modified", metadata.last_modified}
        };
    }
    j["indexed_files"] = indexed_files_json;

    std::ofstream file(SETTINGS_FILE);
    if (file.is_open()) {
        file << j.dump(2) << std::endl;
    }
}

} // namespace rag
