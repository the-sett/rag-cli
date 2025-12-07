#include "chat.hpp"
#include "config.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace rag {

ChatSession::ChatSession() = default;

ChatSession::ChatSession(const std::string& system_prompt, const std::string& log_dir) {
    // Add system message
    conversation_.push_back({"system", system_prompt});

    // Create log directory if it doesn't exist
    fs::create_directories(log_dir);

    // Generate timestamp for filenames
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
    localtime_r(&time_t_now, &tm_now);

    // Generate chat ID
    std::ostringstream id_oss;
    id_oss << "chat_" << std::put_time(&tm_now, "%Y%m%d_%H%M%S");
    chat_id_ = id_oss.str();

    // Generate ISO 8601 timestamp
    std::ostringstream ts_oss;
    ts_oss << std::put_time(&tm_now, "%Y-%m-%dT%H:%M:%S");
    created_at_ = ts_oss.str();

    // Generate log file path
    std::ostringstream log_oss;
    log_oss << log_dir << "/" << chat_id_ << ".md";
    log_path_ = log_oss.str();

    // Generate JSON file path
    std::ostringstream json_oss;
    json_oss << log_dir << "/" << chat_id_ << ".json";
    json_path_ = json_oss.str();

    log_file_.open(log_path_, std::ios::out | std::ios::app);
}

std::unique_ptr<ChatSession> ChatSession::load(const std::string& json_path,
                                                const std::string& system_prompt) {
    if (!fs::exists(json_path)) {
        return nullptr;
    }

    std::ifstream file(json_path);
    if (!file.is_open()) {
        return nullptr;
    }

    try {
        json j;
        file >> j;

        auto session = std::unique_ptr<ChatSession>(new ChatSession());

        session->chat_id_ = j.value("chat_id", "");
        session->created_at_ = j.value("created_at", "");
        session->title_ = j.value("title", "");
        session->openai_response_id_ = j.value("openai_response_id", "");
        session->json_path_ = json_path;

        // Derive log path from json path
        std::string log_path = json_path;
        if (log_path.size() > 5 && log_path.substr(log_path.size() - 5) == ".json") {
            log_path = log_path.substr(0, log_path.size() - 5) + ".md";
        }
        session->log_path_ = log_path;

        // Add system message first
        session->conversation_.push_back({"system", system_prompt});

        // Load conversation messages
        if (j.contains("messages") && j["messages"].is_array()) {
            for (const auto& msg : j["messages"]) {
                std::string role = msg.value("role", "");
                std::string content = msg.value("content", "");
                if (!role.empty() && role != "system") {
                    session->conversation_.push_back({role, content});
                }
            }
        }

        // Open log file for appending
        session->log_file_.open(session->log_path_, std::ios::out | std::ios::app);

        return session;
    } catch (const json::exception&) {
        return nullptr;
    }
}

ChatSession::~ChatSession() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

void ChatSession::add_user_message(const std::string& content) {
    conversation_.push_back({"user", content});
    log("user", content);
    update_title(content);
    save_json();
}

void ChatSession::add_hidden_user_message(const std::string& content) {
    conversation_.push_back({"user", content});
    // No logging - this is a hidden prompt
    // Still save to JSON for conversation continuity
    save_json();
}

void ChatSession::add_assistant_message(const std::string& content) {
    conversation_.push_back({"assistant", content});
    log("assistant", content);
    save_json();
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

void ChatSession::save_json() {
    if (json_path_.empty()) {
        return;
    }

    json j;
    j["chat_id"] = chat_id_;
    j["created_at"] = created_at_;
    j["title"] = title_;
    j["openai_response_id"] = openai_response_id_;

    json messages = json::array();
    for (const auto& msg : conversation_) {
        // Skip system messages - they're provided fresh on load
        if (msg.role != "system") {
            messages.push_back({
                {"role", msg.role},
                {"content", msg.content}
            });
        }
    }
    j["messages"] = messages;

    std::ofstream file(json_path_);
    if (file.is_open()) {
        file << j.dump(2) << std::endl;
    }
}

void ChatSession::update_title(const std::string& user_message) {
    // Only set title from the first user message
    if (!title_.empty()) {
        return;
    }

    // Extract first line, truncate if too long
    std::string first_line;
    auto newline_pos = user_message.find('\n');
    if (newline_pos != std::string::npos) {
        first_line = user_message.substr(0, newline_pos);
    } else {
        first_line = user_message;
    }

    // Truncate to reasonable length
    const size_t max_title_length = 80;
    if (first_line.length() > max_title_length) {
        first_line = first_line.substr(0, max_title_length - 3) + "...";
    }

    title_ = first_line;
}

} // namespace rag
