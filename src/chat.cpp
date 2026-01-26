#include "chat.hpp"
#include "config.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace rag {

ChatSession::ChatSession() = default;

ChatSession::ChatSession(const std::string& system_prompt, const std::string& log_dir)
    : log_dir_(log_dir)
    , system_prompt_(system_prompt)
    , api_window_(json::array())
{
    // Add system message to conversation
    conversation_.push_back({"system", system_prompt});

    // Also add to API window
    api_window_.push_back({{"role", "system"}, {"content", system_prompt}});

    // Session starts in pending state - no ID, no files yet
    // Files will be created when first real user message is added
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

        // Initialize API window with system message
        session->api_window_ = json::array();
        session->api_window_.push_back({{"role", "system"}, {"content", system_prompt}});

        // Load conversation messages
        if (j.contains("messages") && j["messages"].is_array()) {
            for (const auto& msg : j["messages"]) {
                std::string role = msg.value("role", "");
                std::string content = msg.value("content", "");
                if (!role.empty() && role != "system") {
                    session->conversation_.push_back({role, content});
                    // Also add to API window
                    session->api_window_.push_back({{"role", role}, {"content", content}});
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

void ChatSession::materialize() {
    if (is_materialized()) {
        return;  // Already materialized
    }

    // Mark where visible messages start (after any hidden intro messages)
    visible_start_index_ = conversation_.size();

    // Create log directory if it doesn't exist
    fs::create_directories(log_dir_);

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
    log_oss << log_dir_ << "/" << chat_id_ << ".md";
    log_path_ = log_oss.str();

    // Generate JSON file path
    std::ostringstream json_oss;
    json_oss << log_dir_ << "/" << chat_id_ << ".json";
    json_path_ = json_oss.str();

    // Open log file
    log_file_.open(log_path_, std::ios::out | std::ios::app);
}

void ChatSession::add_user_message(const std::string& content) {
    // Materialize the chat on first real user message
    materialize();

    conversation_.push_back({"user", content});
    api_window_.push_back({{"role", "user"}, {"content", content}});
    log("user", content);
    update_title(content);
    save_json();
}

void ChatSession::add_hidden_user_message(const std::string& content) {
    // Just add to conversation - no materialization, no logging, no saving
    conversation_.push_back({"user", content});
    api_window_.push_back({{"role", "user"}, {"content", content}});
}

void ChatSession::add_assistant_message(const std::string& content) {
    conversation_.push_back({"assistant", content});
    api_window_.push_back({{"role", "assistant"}, {"content", content}});

    // Only log/save if materialized
    if (is_materialized()) {
        log("assistant", content);
        save_json();
    }
}

const std::vector<Message>& ChatSession::get_conversation() const {
    return conversation_;
}

std::vector<Message> ChatSession::get_visible_messages() const {
    std::vector<Message> visible;
    for (const auto& msg : conversation_) {
        if (msg.role != "system") {
            visible.push_back(msg);
        }
    }
    return visible;
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
    // Only save messages from visible_start_index_ onward (skip hidden intro)
    for (size_t i = visible_start_index_; i < conversation_.size(); ++i) {
        const auto& msg = conversation_[i];
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

void maybe_compact_chat_window(
    providers::IAIProvider& provider,
    ChatSession& session,
    const std::string& model,
    const providers::ResponseUsage& usage
) {
    // Check if provider supports compaction
    if (!provider.chat().supports_compaction()) {
        return;
    }

    // Get max context window for this model
    const int max_ctx = get_max_context_tokens_for_model(model);
    if (max_ctx <= 0) {
        return;  // Safety check
    }

    // Calculate how full the context window is
    const double fullness = static_cast<double>(usage.input_tokens) / static_cast<double>(max_ctx);

    // Only compact if we're over 90% of the context window
    if (fullness <= 0.9) {
        return;
    }

    const std::string& response_id = session.get_openai_response_id();
    if (response_id.empty()) {
        return;  // No response ID to compact
    }

    std::cerr << "[Compact] Context is " << static_cast<int>(fullness * 100)
              << "% full (" << usage.input_tokens << "/" << max_ctx
              << " tokens), compacting..." << std::endl;

    try {
        auto compacted = provider.chat().compact_window(model, response_id);
        if (compacted) {
            session.set_api_window(*compacted);
            // Clear response ID - subsequent calls should use the compacted input directly
            session.set_openai_response_id("");
            std::cerr << "[Compact] Window compacted successfully" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Compact] Warning: Failed to compact window: " << e.what() << std::endl;
        // Don't fail the overall request if compaction fails
    }
}

// Overload for backward compatibility with OpenAIClient
void maybe_compact_chat_window(
    OpenAIClient& client,
    ChatSession& session,
    const std::string& model,
    const ResponseUsage& usage
) {
    // OpenAIClient wraps an OpenAIProvider which supports compaction
    // Get max context window for this model
    const int max_ctx = get_max_context_tokens_for_model(model);
    if (max_ctx <= 0) {
        return;  // Safety check
    }

    // Calculate how full the context window is
    const double fullness = static_cast<double>(usage.input_tokens) / static_cast<double>(max_ctx);

    // Only compact if we're over 90% of the context window
    if (fullness <= 0.9) {
        return;
    }

    const std::string& response_id = session.get_openai_response_id();
    if (response_id.empty()) {
        return;  // No response ID to compact
    }

    std::cerr << "[Compact] Context is " << static_cast<int>(fullness * 100)
              << "% full (" << usage.input_tokens << "/" << max_ctx
              << " tokens), compacting..." << std::endl;

    try {
        nlohmann::json compacted = client.compact_window(model, response_id);
        session.set_api_window(compacted);
        // Clear response ID - subsequent calls should use the compacted input directly
        session.set_openai_response_id("");
        std::cerr << "[Compact] Window compacted successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Compact] Warning: Failed to compact window: " << e.what() << std::endl;
        // Don't fail the overall request if compaction fails
    }
}

} // namespace rag
