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
    std::string content_hash;    // Hash of file contents for change detection.
};

/**
 * Metadata for a chat session.
 *
 * Tracks the chat ID, log file path, OpenAI response ID for continuation,
 * and display information.
 */
struct ChatInfo {
    std::string id;                  // Unique chat ID (e.g., "chat_20241207_143022")
    std::string log_file;            // Path to the markdown log file
    std::string json_file;           // Path to the JSON conversation file
    std::string openai_response_id;  // Last OpenAI response ID for continuation
    std::string created_at;          // ISO 8601 timestamp
    std::string title;               // First line of first user message
    std::string agent_id;            // Agent ID if chat was started with an agent (empty otherwise)
};

/**
 * Agent definition.
 *
 * An agent is a named set of instructions that can be used to customize
 * the AI's behavior for specific tasks.
 */
struct AgentInfo {
    std::string id;           // Unique agent ID (e.g., "agent_20241207_143022")
    std::string name;         // Agent name (first line of instructions)
    std::string instructions; // Full agent instructions
    std::string created_at;   // ISO 8601 timestamp
};

/**
 * Submit shortcut mode for the web interface.
 * Controls how Enter key behaves in the query input.
 */
enum class SubmitShortcut {
    EnterOnce,       // Single Enter submits
    ShiftEnter,      // Shift+Enter submits
    EnterTwice       // Double Enter (quick succession) submits
};

// Convert SubmitShortcut to/from string for JSON serialization
std::string submit_shortcut_to_string(SubmitShortcut mode);
SubmitShortcut submit_shortcut_from_string(const std::string& str);

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
    std::vector<ChatInfo> chats;                          // Chat session history.
    std::vector<AgentInfo> agents;                        // Agent definitions.
    SubmitShortcut submit_shortcut = SubmitShortcut::ShiftEnter;  // Query submit shortcut mode.

    // Returns true if settings contain required fields for operation.
    bool is_valid() const {
        return !model.empty() && !vector_store_id.empty();
    }
};

// Loads settings from .crag.json. Returns empty optional if file doesn't exist.
std::optional<Settings> load_settings();

// Saves settings to .crag.json.
void save_settings(const Settings& settings);

// Validates chats by checking log files exist, removes invalid entries.
void validate_chats(Settings& settings);

// Adds or updates a chat in settings.
void upsert_chat(Settings& settings, const ChatInfo& chat);

// Finds a chat by ID. Returns nullptr if not found.
const ChatInfo* find_chat(const Settings& settings, const std::string& chat_id);

// Adds or updates an agent in settings.
void upsert_agent(Settings& settings, const AgentInfo& agent);

// Finds an agent by ID. Returns nullptr if not found.
const AgentInfo* find_agent(const Settings& settings, const std::string& agent_id);

// Deletes an agent by ID. Returns true if deleted.
bool delete_agent(Settings& settings, const std::string& agent_id);

// Deletes a chat by ID. Returns true if deleted.
bool delete_chat(Settings& settings, const std::string& chat_id);

} // namespace rag
