#pragma once

/**
 * Chat session management with conversation logging.
 *
 * Maintains conversation history and persists messages to log files
 * for later review and session reconnection.
 */

#include "openai_client.hpp"
#include <string>
#include <vector>
#include <fstream>

namespace rag {

/**
 * Manages a conversation session with automatic logging.
 *
 * Stores the conversation history in memory and writes each message to
 * timestamped log files (markdown for human reading, JSON for reload).
 */
class ChatSession {
public:
    // Creates a new session with the given system prompt and log directory.
    ChatSession(const std::string& system_prompt, const std::string& log_dir);

    // Loads an existing session from a JSON file.
    // Returns nullptr if load fails.
    static std::unique_ptr<ChatSession> load(const std::string& json_path,
                                              const std::string& system_prompt);

    // Closes the log files.
    ~ChatSession();

    // Adds a user message to the conversation.
    void add_user_message(const std::string& content);

    // Adds a user message to the conversation without logging it.
    // Used for hidden prompts like the initial greeting.
    void add_hidden_user_message(const std::string& content);

    // Adds an assistant message to the conversation.
    void add_assistant_message(const std::string& content);

    // Returns the full conversation history.
    const std::vector<Message>& get_conversation() const;

    // Returns the chat ID.
    const std::string& get_chat_id() const { return chat_id_; }

    // Returns the path to the markdown log file.
    const std::string& get_log_path() const { return log_path_; }

    // Returns the path to the JSON conversation file.
    const std::string& get_json_path() const { return json_path_; }

    // Returns the OpenAI response ID for continuation.
    const std::string& get_openai_response_id() const { return openai_response_id_; }

    // Sets the OpenAI response ID after a response completes.
    void set_openai_response_id(const std::string& id) { openai_response_id_ = id; }

    // Returns the chat title (first line of first user message).
    const std::string& get_title() const { return title_; }

    // Returns the ISO 8601 creation timestamp.
    const std::string& get_created_at() const { return created_at_; }

private:
    std::string chat_id_;                // Unique chat ID
    std::string created_at_;             // ISO 8601 timestamp
    std::string title_;                  // First line of first user message
    std::string openai_response_id_;     // Last OpenAI response ID
    std::vector<Message> conversation_;  // In-memory conversation history.
    std::string log_path_;               // Path to the markdown log file.
    std::string json_path_;              // Path to the JSON file.
    std::ofstream log_file_;             // Open markdown log file stream.

    // Private constructor for loading existing sessions.
    ChatSession();

    // Writes a message to the markdown log file.
    void log(const std::string& role, const std::string& text);

    // Saves the full conversation to the JSON file.
    void save_json();

    // Extracts title from first user message.
    void update_title(const std::string& user_message);
};

} // namespace rag
