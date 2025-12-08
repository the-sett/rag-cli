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
 *
 * New sessions are created in a "pending" state - no files are created
 * until the first real user query is submitted. This avoids creating
 * empty chat records for sessions that only show the intro.
 */
class ChatSession {
public:
    // Creates a new session with the given system prompt and log directory.
    // The session starts in pending state - no files are created yet.
    ChatSession(const std::string& system_prompt, const std::string& log_dir);

    // Loads an existing session from a JSON file.
    // Returns nullptr if load fails.
    static std::unique_ptr<ChatSession> load(const std::string& json_path,
                                              const std::string& system_prompt);

    // Closes the log files.
    ~ChatSession();

    // Adds a user message to the conversation.
    // This materializes the chat if it was pending.
    void add_user_message(const std::string& content);

    // Adds a user message to the conversation without logging it.
    // Used for hidden prompts like the initial greeting.
    // Does NOT materialize the chat or save to files.
    void add_hidden_user_message(const std::string& content);

    // Adds an assistant message to the conversation.
    // Only logs/saves if the chat has been materialized.
    void add_assistant_message(const std::string& content);

    // Returns the full conversation history (for API calls).
    const std::vector<Message>& get_conversation() const;

    // Returns only the visible messages (user/assistant, excluding system).
    // Used for replaying history to the client.
    std::vector<Message> get_visible_messages() const;

    // Returns the chat ID (empty if pending).
    const std::string& get_chat_id() const { return chat_id_; }

    // Returns true if the chat has been materialized (has ID and files).
    bool is_materialized() const { return !chat_id_.empty(); }

    // Returns the path to the markdown log file (empty if pending).
    const std::string& get_log_path() const { return log_path_; }

    // Returns the path to the JSON conversation file (empty if pending).
    const std::string& get_json_path() const { return json_path_; }

    // Returns the OpenAI response ID for continuation.
    const std::string& get_openai_response_id() const { return openai_response_id_; }

    // Sets the OpenAI response ID after a response completes.
    void set_openai_response_id(const std::string& id) { openai_response_id_ = id; }

    // Returns the chat title (first line of first user message).
    const std::string& get_title() const { return title_; }

    // Returns the ISO 8601 creation timestamp (empty if pending).
    const std::string& get_created_at() const { return created_at_; }

    // Returns the agent ID if this chat was started with an agent.
    const std::string& get_agent_id() const { return agent_id_; }

    // Sets the agent ID for this chat.
    void set_agent_id(const std::string& id) { agent_id_ = id; }

private:
    std::string log_dir_;                // Directory for log files
    std::string system_prompt_;          // System prompt (for pending state)
    std::string chat_id_;                // Unique chat ID (empty if pending)
    std::string created_at_;             // ISO 8601 timestamp
    std::string title_;                  // First line of first user message
    std::string openai_response_id_;     // Last OpenAI response ID
    std::string agent_id_;               // Agent ID if started with an agent
    std::vector<Message> conversation_;  // In-memory conversation history.
    size_t visible_start_index_ = 0;     // Index where visible messages begin (after hidden intro)
    std::string log_path_;               // Path to the markdown log file.
    std::string json_path_;              // Path to the JSON file.
    std::ofstream log_file_;             // Open markdown log file stream.

    // Private constructor for loading existing sessions.
    ChatSession();

    // Materializes the chat - creates ID, files, and saves conversation.
    // Called on first real user message.
    void materialize();

    // Writes a message to the markdown log file.
    void log(const std::string& role, const std::string& text);

    // Saves the full conversation to the JSON file.
    void save_json();

    // Extracts title from first user message.
    void update_title(const std::string& user_message);
};

} // namespace rag
