#include "settings.hpp"
#include "config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace rag {

namespace fs = std::filesystem;
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

        if (j.contains("chats") && j["chats"].is_array()) {
            for (const auto& chat_json : j["chats"]) {
                ChatInfo chat;
                chat.id = chat_json.value("id", "");
                chat.log_file = chat_json.value("log_file", "");
                chat.json_file = chat_json.value("json_file", "");
                chat.openai_response_id = chat_json.value("openai_response_id", "");
                chat.created_at = chat_json.value("created_at", "");
                chat.title = chat_json.value("title", "");
                chat.agent_id = chat_json.value("agent_id", "");
                if (!chat.id.empty()) {
                    settings.chats.push_back(chat);
                }
            }
        }

        if (j.contains("agents") && j["agents"].is_array()) {
            for (const auto& agent_json : j["agents"]) {
                AgentInfo agent;
                agent.id = agent_json.value("id", "");
                agent.name = agent_json.value("name", "");
                agent.instructions = agent_json.value("instructions", "");
                agent.created_at = agent_json.value("created_at", "");
                if (!agent.id.empty()) {
                    settings.agents.push_back(agent);
                }
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

    json chats_json = json::array();
    for (const auto& chat : settings.chats) {
        json chat_obj = {
            {"id", chat.id},
            {"log_file", chat.log_file},
            {"json_file", chat.json_file},
            {"openai_response_id", chat.openai_response_id},
            {"created_at", chat.created_at},
            {"title", chat.title}
        };
        if (!chat.agent_id.empty()) {
            chat_obj["agent_id"] = chat.agent_id;
        }
        chats_json.push_back(chat_obj);
    }
    j["chats"] = chats_json;

    json agents_json = json::array();
    for (const auto& agent : settings.agents) {
        agents_json.push_back({
            {"id", agent.id},
            {"name", agent.name},
            {"instructions", agent.instructions},
            {"created_at", agent.created_at}
        });
    }
    j["agents"] = agents_json;

    std::ofstream file(SETTINGS_FILE);
    if (file.is_open()) {
        file << j.dump(2) << std::endl;
    }
}

void validate_chats(Settings& settings) {
    auto it = std::remove_if(settings.chats.begin(), settings.chats.end(),
        [](const ChatInfo& chat) {
            // Remove if JSON file doesn't exist
            return !fs::exists(chat.json_file);
        });
    settings.chats.erase(it, settings.chats.end());
}

void upsert_chat(Settings& settings, const ChatInfo& chat) {
    // Find existing chat by ID
    auto it = std::find_if(settings.chats.begin(), settings.chats.end(),
        [&chat](const ChatInfo& c) { return c.id == chat.id; });

    if (it != settings.chats.end()) {
        // Update existing
        *it = chat;
    } else {
        // Add new
        settings.chats.push_back(chat);
    }
}

const ChatInfo* find_chat(const Settings& settings, const std::string& chat_id) {
    auto it = std::find_if(settings.chats.begin(), settings.chats.end(),
        [&chat_id](const ChatInfo& c) { return c.id == chat_id; });

    if (it != settings.chats.end()) {
        return &(*it);
    }
    return nullptr;
}

void upsert_agent(Settings& settings, const AgentInfo& agent) {
    // Find existing agent by ID
    auto it = std::find_if(settings.agents.begin(), settings.agents.end(),
        [&agent](const AgentInfo& a) { return a.id == agent.id; });

    if (it != settings.agents.end()) {
        // Update existing
        *it = agent;
    } else {
        // Add new
        settings.agents.push_back(agent);
    }
}

const AgentInfo* find_agent(const Settings& settings, const std::string& agent_id) {
    auto it = std::find_if(settings.agents.begin(), settings.agents.end(),
        [&agent_id](const AgentInfo& a) { return a.id == agent_id; });

    if (it != settings.agents.end()) {
        return &(*it);
    }
    return nullptr;
}

bool delete_agent(Settings& settings, const std::string& agent_id) {
    auto it = std::find_if(settings.agents.begin(), settings.agents.end(),
        [&agent_id](const AgentInfo& a) { return a.id == agent_id; });

    if (it != settings.agents.end()) {
        settings.agents.erase(it);
        return true;
    }
    return false;
}

} // namespace rag
