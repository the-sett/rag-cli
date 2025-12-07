#pragma once

/**
 * Chat session management with conversation logging.
 *
 * Maintains conversation history and persists messages to a log file
 * for later review.
 */

#include "openai_client.hpp"
#include <string>
#include <vector>
#include <fstream>

namespace rag {

/**
 * Manages a conversation session with automatic logging.
 *
 * Stores the conversation history in memory and writes each message to a
 * timestamped log file in the specified directory.
 */
class ChatSession {
public:
    // Creates a session with the given system prompt and log directory.
    ChatSession(const std::string& system_prompt, const std::string& log_dir);

    // Closes the log file.
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

private:
    std::vector<Message> conversation_;  // In-memory conversation history.
    std::string log_path_;               // Path to the log file.
    std::ofstream log_file_;             // Open log file stream.

    // Writes a message to the log file.
    void log(const std::string& role, const std::string& text);
};

} // namespace rag
