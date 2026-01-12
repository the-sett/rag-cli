#include "mcp_server.hpp"
#include "config.hpp"
#include <iostream>
#include <sstream>

namespace rag {

using json = nlohmann::json;

// JSON-RPC error codes
constexpr int PARSE_ERROR = -32700;
constexpr int INVALID_REQUEST = -32600;
constexpr int METHOD_NOT_FOUND = -32601;
constexpr int INVALID_PARAMS = -32602;
constexpr int INTERNAL_ERROR = -32603;
constexpr int OPENAI_ERROR = -32000;
constexpr int CHAT_NOT_FOUND = -32002;

// MCP protocol version
constexpr const char* MCP_PROTOCOL_VERSION = "2024-11-05";

MCPServer::MCPServer(
    OpenAIClient& client,
    Settings& settings,
    const std::string& model,
    const std::string& vector_store_id,
    const std::string& reasoning_effort,
    const std::string& system_prompt,
    const std::string& log_dir
)
    : client_(client)
    , settings_(settings)
    , model_(model)
    , vector_store_id_(vector_store_id)
    , reasoning_effort_(reasoning_effort)
    , system_prompt_(system_prompt)
    , log_dir_(log_dir)
{
}

void MCPServer::run() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        handle_message(line);
    }
}

void MCPServer::handle_message(const std::string& line) {
    std::cerr << "MCP: Received: " << line.substr(0, 200) << std::endl;

    json request;
    try {
        request = json::parse(line);
    } catch (const json::exception& e) {
        send_error(0, PARSE_ERROR, "Parse error: " + std::string(e.what()));
        return;
    }

    // Check for required fields
    if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") {
        send_error(0, INVALID_REQUEST, "Invalid JSON-RPC version");
        return;
    }

    std::string method = request.value("method", "");
    if (method.empty()) {
        send_error(0, INVALID_REQUEST, "Missing method");
        return;
    }

    // Get request ID (may be null for notifications)
    int id = 0;
    bool is_notification = !request.contains("id");
    if (!is_notification) {
        if (request["id"].is_number()) {
            id = request["id"].get<int>();
        }
    }

    json params = request.value("params", json::object());

    try {
        // Handle MCP methods
        if (method == "initialize") {
            auto result = handle_initialize(params);
            send_response(id, result);
        }
        else if (method == "notifications/initialized") {
            // Notification - no response needed
            initialized_ = true;
            std::cerr << "MCP: Client initialized" << std::endl;
        }
        else if (method == "tools/list") {
            auto result = handle_tools_list();
            send_response(id, result);
        }
        else if (method == "tools/call") {
            auto result = handle_tools_call(params);
            send_response(id, result);
        }
        else if (method == "ping") {
            send_response(id, json::object());
        }
        else {
            send_error(id, METHOD_NOT_FOUND, "Method not found: " + method);
        }
    } catch (const std::exception& e) {
        send_error(id, INTERNAL_ERROR, "Internal error: " + std::string(e.what()));
    }
}

void MCPServer::send_response(int id, const json& result) {
    json response = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
    std::string msg = response.dump();
    std::cerr << "MCP: Sending: " << msg.substr(0, 200) << std::endl;
    std::cout << msg << std::endl;
    std::cout.flush();
}

void MCPServer::send_error(int id, int code, const std::string& message) {
    json response = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };
    std::string msg = response.dump();
    std::cerr << "MCP: Sending error: " << msg << std::endl;
    std::cout << msg << std::endl;
    std::cout.flush();
}

void MCPServer::send_notification(const std::string& method, const json& params) {
    json notification = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params}
    };
    std::cout << notification.dump() << std::endl;
    std::cout.flush();
}

json MCPServer::handle_initialize(const json& params) {
    std::cerr << "MCP: Handling initialize" << std::endl;

    return {
        {"protocolVersion", MCP_PROTOCOL_VERSION},
        {"capabilities", {
            {"tools", json::object()}
        }},
        {"serverInfo", {
            {"name", "crag"},
            {"version", "1.0.0"}
        }}
    };
}

json MCPServer::handle_tools_list() {
    std::cerr << "MCP: Listing tools" << std::endl;

    json tools = json::array();

    // query tool
    tools.push_back({
        {"name", "query"},
        {"description", "Query the knowledge base. Pass a chat_id to continue an existing "
                       "conversation with full context, or omit it to start a new conversation. "
                       "Returns a chat_id that can be used for follow-up queries."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"question", {
                    {"type", "string"},
                    {"description", "The question to ask about the indexed documents"}
                }},
                {"chat_id", {
                    {"type", "string"},
                    {"description", "Optional. Chat ID from a previous query to continue that conversation with full context."}
                }}
            }},
            {"required", json::array({"question"})}
        }}
    });

    // get_status tool
    tools.push_back({
        {"name", "get_status"},
        {"description", "Get information about the knowledge base configuration: model, indexed files, and file patterns."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", json::object()},
            {"required", json::array()}
        }}
    });

    // list_chats tool
    tools.push_back({
        {"name", "list_chats"},
        {"description", "List previous conversations that can be continued by passing their chat_id to the query tool."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"limit", {
                    {"type", "number"},
                    {"description", "Maximum number of chats to return (default: 10)"}
                }}
            }},
            {"required", json::array()}
        }}
    });

    return {{"tools", tools}};
}

json MCPServer::handle_tools_call(const json& params) {
    std::string name = params.value("name", "");
    json args = params.value("arguments", json::object());

    std::cerr << "MCP: Calling tool: " << name << std::endl;

    if (name == "query") {
        return tool_query(args);
    } else if (name == "get_status") {
        return tool_get_status(args);
    } else if (name == "list_chats") {
        return tool_list_chats(args);
    } else {
        throw std::runtime_error("Unknown tool: " + name);
    }
}

std::shared_ptr<ChatSession> MCPServer::get_or_create_session(const std::string& chat_id) {
    if (chat_id.empty()) {
        // Create a new session
        return std::make_shared<ChatSession>(system_prompt_, log_dir_);
    }

    // Check cache first
    auto it = session_cache_.find(chat_id);
    if (it != session_cache_.end()) {
        return it->second;
    }

    // Try to load from disk
    const ChatInfo* chat_info = find_chat(settings_, chat_id);
    if (!chat_info) {
        return nullptr;  // Chat not found
    }

    auto session = ChatSession::load(chat_info->json_file, system_prompt_);
    if (session) {
        // Restore the OpenAI response ID for continuation
        session->set_openai_response_id(chat_info->openai_response_id);

        // Cache it
        auto shared_session = std::shared_ptr<ChatSession>(session.release());
        session_cache_[chat_id] = shared_session;
        return shared_session;
    }

    return nullptr;
}

void MCPServer::update_settings(std::shared_ptr<ChatSession> session) {
    if (!session->is_materialized()) {
        return;
    }

    ChatInfo chat;
    chat.id = session->get_chat_id();
    chat.log_file = session->get_log_path();
    chat.json_file = session->get_json_path();
    chat.openai_response_id = session->get_openai_response_id();
    chat.created_at = session->get_created_at();
    chat.title = session->get_title();
    chat.agent_id = session->get_agent_id();

    upsert_chat(settings_, chat);
    save_settings(settings_);

    // Update cache with the new chat_id
    if (session_cache_.find(chat.id) == session_cache_.end()) {
        session_cache_[chat.id] = session;
    }
}

json MCPServer::tool_query(const json& args) {
    std::string question = args.value("question", "");
    std::string chat_id = args.value("chat_id", "");

    if (question.empty()) {
        throw std::runtime_error("question is required");
    }

    std::cerr << "MCP: Query: " << question.substr(0, 100) << std::endl;
    if (!chat_id.empty()) {
        std::cerr << "MCP: Continuing chat: " << chat_id << std::endl;
    }

    // Get or create session
    auto session = get_or_create_session(chat_id);
    if (!session && !chat_id.empty()) {
        // Chat ID was provided but not found
        return {
            {"content", json::array({
                {{"type", "text"}, {"text", "Error: Chat not found: " + chat_id}}
            })},
            {"isError", true}
        };
    }

    if (!session) {
        session = std::make_shared<ChatSession>(system_prompt_, log_dir_);
    }

    // Add user message (this materializes the chat if it's new)
    session->add_user_message(question);

    // Stream response from OpenAI (collect full response)
    std::string full_response;

    try {
        StreamResult result = client_.stream_response(
            model_,
            session->get_api_window(),
            vector_store_id_,
            reasoning_effort_,
            session->get_openai_response_id(),
            [&full_response](const std::string& delta) {
                full_response += delta;
            },
            []() -> bool { return false; }  // No cancellation in MCP mode
        );

        // Store the response ID for conversation continuation
        if (!result.response_id.empty()) {
            session->set_openai_response_id(result.response_id);
        }

        // Add assistant response to conversation
        session->add_assistant_message(full_response);

        // Check if we need to compact the conversation window
        maybe_compact_chat_window(client_, *session, model_, result.usage);

        // Update settings with chat info
        update_settings(session);

        std::cerr << "MCP: Response complete, " << full_response.size() << " chars" << std::endl;

        // Return response with chat_id for continuation
        std::string result_text = full_response + "\n\n---\nchat_id: " + session->get_chat_id();

        return {
            {"content", json::array({
                {{"type", "text"}, {"text", result_text}}
            })}
        };

    } catch (const std::exception& e) {
        std::cerr << "MCP: OpenAI error: " << e.what() << std::endl;
        return {
            {"content", json::array({
                {{"type", "text"}, {"text", "Error: " + std::string(e.what())}}
            })},
            {"isError", true}
        };
    }
}

json MCPServer::tool_get_status(const json& args) {
    std::ostringstream oss;
    oss << "Knowledge Base Status:\n";
    oss << "- Model: " << model_ << "\n";
    oss << "- Reasoning: " << reasoning_effort_ << "\n";
    oss << "- Indexed files: " << settings_.indexed_files.size() << "\n";
    oss << "- Patterns: ";

    for (size_t i = 0; i < settings_.file_patterns.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << settings_.file_patterns[i];
    }

    return {
        {"content", json::array({
            {{"type", "text"}, {"text", oss.str()}}
        })}
    };
}

json MCPServer::tool_list_chats(const json& args) {
    int limit = args.value("limit", 10);

    std::ostringstream oss;
    oss << "Recent conversations:\n";

    if (settings_.chats.empty()) {
        oss << "\nNo previous conversations found.";
    } else {
        // List chats in reverse order (most recent first)
        int count = 0;
        for (auto it = settings_.chats.rbegin();
             it != settings_.chats.rend() && count < limit;
             ++it, ++count) {
            oss << "\n" << (count + 1) << ". " << it->id << "\n";
            oss << "   Title: " << it->title << "\n";
            oss << "   Created: " << it->created_at << "\n";
        }
    }

    return {
        {"content", json::array({
            {{"type", "text"}, {"text", oss.str()}}
        })}
    };
}

} // namespace rag
