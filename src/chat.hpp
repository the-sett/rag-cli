#pragma once

#include "openai_client.hpp"
#include <string>
#include <vector>
#include <fstream>

namespace rag {

class ChatSession {
public:
    ChatSession(const std::string& system_prompt, const std::string& log_dir);
    ~ChatSession();

    void add_user_message(const std::string& content);
    void add_assistant_message(const std::string& content);

    const std::vector<Message>& get_conversation() const;

private:
    std::vector<Message> conversation_;
    std::string log_path_;
    std::ofstream log_file_;

    void log(const std::string& role, const std::string& text);
};

} // namespace rag
