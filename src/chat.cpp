#include "chat.hpp"
#include "config.hpp"
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace rag {

ChatSession::ChatSession(const std::string& system_prompt, const std::string& log_dir) {
    // Add system message
    conversation_.push_back({"system", system_prompt});

    // Create log directory if it doesn't exist
    fs::create_directories(log_dir);

    // Generate timestamped log filename
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
    localtime_r(&time_t_now, &tm_now);

    std::ostringstream oss;
    oss << log_dir << "/chat_"
        << std::put_time(&tm_now, "%Y%m%d_%H%M%S")
        << ".md";
    log_path_ = oss.str();

    log_file_.open(log_path_, std::ios::out | std::ios::app);
}

ChatSession::~ChatSession() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

void ChatSession::add_user_message(const std::string& content) {
    conversation_.push_back({"user", content});
    log("user", content);
}

void ChatSession::add_hidden_user_message(const std::string& content) {
    conversation_.push_back({"user", content});
    // No logging - this is a hidden prompt
}

void ChatSession::add_assistant_message(const std::string& content) {
    conversation_.push_back({"assistant", content});
    log("assistant", content);
}

const std::vector<Message>& ChatSession::get_conversation() const {
    return conversation_;
}

void ChatSession::log(const std::string& role, const std::string& text) {
    if (log_file_.is_open()) {
        // Convert role to uppercase for header
        std::string header = role;
        for (auto& c : header) {
            c = std::toupper(c);
        }
        log_file_ << "## " << header << "\n" << text << "\n\n";
        log_file_.flush();
    }
}

} // namespace rag
