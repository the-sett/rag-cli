#include "websocket_server.hpp"
#include "settings.hpp"
#include "config.hpp"
#include "verbose.hpp"
#include "mcp_tools.hpp"
#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>
#include <iostream>

namespace rag {

WebSocketServer::WebSocketServer(
    OpenAIClient& client,
    const std::string& model,
    const std::string& vector_store_id,
    const std::string& reasoning_effort,
    const std::string& system_prompt,
    const std::string& log_dir
)
    : client_(client)
    , model_(model)
    , vector_store_id_(vector_store_id)
    , reasoning_effort_(reasoning_effort)
    , system_prompt_(system_prompt)
    , log_dir_(log_dir)
{
}

WebSocketServer::~WebSocketServer() {
    stop();
}

void WebSocketServer::on_start(std::function<void(const std::string&, int)> callback) {
    on_start_callback_ = std::move(callback);
}

bool WebSocketServer::start(const std::string& address, int port) {
    server_ = std::make_unique<ix::WebSocketServer>(port, address);

    server_->setOnClientMessageCallback(
        [this](std::shared_ptr<ix::ConnectionState> connectionState,
               ix::WebSocket& webSocket,
               const ix::WebSocketMessagePtr& msg) {

            void* conn_id = connectionState.get();

            if (msg->type == ix::WebSocketMessageType::Open) {
                verbose_log("WS", "Client connected: " + connectionState->getRemoteIp());
                // Don't create session yet - wait for init message from client
            }
            else if (msg->type == ix::WebSocketMessageType::Close) {
                verbose_log("WS", "Client disconnected: " + connectionState->getRemoteIp());
                // Clean up the session
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                sessions_.erase(conn_id);
            }
            else if (msg->type == ix::WebSocketMessageType::Message) {
                verbose_in("WS", "Message: " + truncate(msg->str, 500));
                handle_message(conn_id, webSocket, msg->str);
            }
            else if (msg->type == ix::WebSocketMessageType::Error) {
                // Ignore "Could not parse url" errors - these are typically from
                // non-WebSocket requests (like browser pre-flight checks)
                if (msg->errorInfo.reason.find("Could not parse url") == std::string::npos) {
                    verbose_err("WS", "Error: " + msg->errorInfo.reason);
                    std::cerr << "WebSocket error: " << msg->errorInfo.reason << std::endl;
                }
            }
        }
    );

    auto result = server_->listen();
    if (!result.first) {
        std::cerr << "WebSocket server failed to listen: " << result.second << std::endl;
        return false;
    }

    if (on_start_callback_) {
        on_start_callback_(address, port);
    }

    server_->start();
    return true;
}

void WebSocketServer::stop() {
    if (server_) {
        server_->stop();
        server_.reset();
    }

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_.clear();
}

void WebSocketServer::handle_message(void* conn_id, ix::WebSocket& ws, const std::string& message) {
    try {
        auto json = nlohmann::json::parse(message);

        std::string type = json.value("type", "");

        if (type == "init") {
            // Initialize session - either new or reconnecting to existing
            std::string chat_id = json.value("chat_id", "");
            std::string agent_id = json.value("agent_id", "");
            handle_init(conn_id, ws, chat_id, agent_id);
            return;
        }

        if (type != "query") {
            send_json(ws, {{"type", "error"}, {"message", "Unknown message type"}});
            return;
        }

        std::string content = json.value("content", "");
        if (content.empty()) {
            send_json(ws, {{"type", "error"}, {"message", "Empty query"}});
            return;
        }

        // Find the session for this connection
        std::shared_ptr<ChatSession> session;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(conn_id);
            if (it == sessions_.end()) {
                // No session - client didn't send init message
                send_json(ws, {{"type", "error"}, {"message", "Session not initialized"}});
                return;
            }
            session = it->second;
        }

        process_query(ws, session, content, false);

    } catch (const nlohmann::json::exception& e) {
        send_json(ws, {{"type", "error"}, {"message", "Invalid JSON"}});
    }
}

void WebSocketServer::handle_init(void* conn_id, ix::WebSocket& ws,
                                   const std::string& chat_id, const std::string& agent_id) {
    verbose_log("WS", "handle_init: chat_id=" + (chat_id.empty() ? "(new)" : chat_id) +
                      " agent_id=" + (agent_id.empty() ? "(none)" : agent_id));

    std::shared_ptr<ChatSession> session;
    std::string effective_agent_id = agent_id;

    if (!chat_id.empty() && settings_) {
        // Try to load existing chat
        const ChatInfo* chat_info = find_chat(*settings_, chat_id);
        if (chat_info) {
            // Use the agent_id stored with the chat (for reconnections)
            effective_agent_id = chat_info->agent_id;

            // Build system prompt with agent instructions if applicable
            std::string effective_prompt = system_prompt_;
            if (!effective_agent_id.empty()) {
                const AgentInfo* agent = find_agent(*settings_, effective_agent_id);
                if (agent) {
                    effective_prompt = system_prompt_ + "\n\n" + agent->instructions;
                }
            }

            session = ChatSession::load(chat_info->json_file, effective_prompt);
            if (session) {
                // Restore the OpenAI response ID and agent ID for continuation
                session->set_openai_response_id(chat_info->openai_response_id);
                session->set_agent_id(effective_agent_id);
            }
        }
    }

    if (!session) {
        // Build system prompt with agent instructions if applicable
        std::string effective_prompt = system_prompt_;
        if (!effective_agent_id.empty() && settings_) {
            const AgentInfo* agent = find_agent(*settings_, effective_agent_id);
            if (agent) {
                effective_prompt = system_prompt_ + "\n\n" + agent->instructions;
            }
        }

        // Create new session (starts in pending state - no files yet)
        session = std::make_shared<ChatSession>(effective_prompt, log_dir_);
        session->set_agent_id(effective_agent_id);
    }

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_[conn_id] = session;
    }

    // For new chats, send the intro message
    if (chat_id.empty()) {
        send_intro(ws, session);
    } else {
        // For existing chats, replay history then send ready
        send_history(ws, session);
        send_json(ws, {{"type", "ready"}, {"chat_id", session->get_chat_id()}});
    }
}

void WebSocketServer::send_history(ix::WebSocket& ws, std::shared_ptr<ChatSession> session) {
    auto messages = session->get_visible_messages();
    for (const auto& msg : messages) {
        send_json(ws, {
            {"type", "history"},
            {"role", msg.role},
            {"content", msg.content}
        });
    }
}

void WebSocketServer::send_intro(ix::WebSocket& ws, std::shared_ptr<ChatSession> session) {
    verbose_log("WS", "send_intro: sending intro message to client");

    // Check if we have a cached intro message
    if (settings_ && !settings_->cached_intro_message.empty()) {
        verbose_log("WS", "Using cached intro message");
        // Use cached intro - send it as delta messages
        const std::string& cached = settings_->cached_intro_message;

        // Add the hidden prompt to conversation (so context is correct)
        session->add_hidden_user_message(INITIAL_PROMPT);

        // Send cached response as a single delta
        send_json(ws, {{"type", "delta"}, {"content", cached}});

        // Add to session (won't save to files since not materialized)
        session->add_assistant_message(cached);

        // Send done - no chat_id since chat isn't materialized yet
        send_json(ws, {{"type", "done"}});
    } else {
        // Generate intro and cache it
        verbose_log("WS", "Generating new intro message via OpenAI API");
        session->add_hidden_user_message(INITIAL_PROMPT);

        std::string full_response;

        try {
            std::string response_id = client_.stream_response(
                model_,
                session->get_conversation(),
                vector_store_id_,
                reasoning_effort_,
                session->get_openai_response_id(),
                [this, &ws, &full_response](const std::string& delta) {
                    full_response += delta;
                    send_json(ws, {{"type", "delta"}, {"content", delta}});
                }
            );

            // Add assistant response to conversation (won't save since not materialized)
            session->add_assistant_message(full_response);

            // Store the response ID
            if (!response_id.empty()) {
                session->set_openai_response_id(response_id);
            }

            // Cache the intro message for future use
            if (settings_) {
                settings_->cached_intro_message = full_response;
                save_settings(*settings_);
            }

            // Send done - no chat_id since chat isn't materialized yet
            send_json(ws, {{"type", "done"}});

        } catch (const std::exception& e) {
            send_json(ws, {{"type", "error"}, {"message", e.what()}});
        }
    }
}

void WebSocketServer::process_query(ix::WebSocket& ws, std::shared_ptr<ChatSession> session,
                                     const std::string& content, bool hidden) {
    verbose_log("WS", "process_query: content=" + truncate(content, 100) +
                      " hidden=" + (hidden ? "true" : "false"));

    // Track if this is the first real message (will materialize the chat)
    bool was_pending = !session->is_materialized();

    // Add user message to conversation
    if (hidden) {
        session->add_hidden_user_message(content);
    } else {
        session->add_user_message(content);  // This materializes if pending
    }

    // Stream the response with MCP tools enabled
    std::string full_response;

    // Get MCP tool definitions
    nlohmann::json mcp_tools = get_mcp_tool_definitions();

    try {
        std::string response_id = client_.stream_response_with_tools(
            model_,
            session->get_conversation(),
            vector_store_id_,
            reasoning_effort_,
            session->get_openai_response_id(),
            mcp_tools,
            // on_text callback
            [this, &ws, &full_response](const std::string& delta) {
                full_response += delta;
                send_json(ws, {{"type", "delta"}, {"content", delta}});
            },
            // on_tool_call callback - returns result string
            [this, &ws](const std::string& call_id, const std::string& name, const nlohmann::json& args) -> std::string {
                verbose_log("MCP", "Executing tool: " + name);

                // Execute the tool and send UI command
                if (name == TOOL_OPEN_SIDEBAR) {
                    send_ui_command(ws, "open_sidebar", nlohmann::json::object());
                    return "Sidebar opened successfully.";
                } else if (name == TOOL_CLOSE_SIDEBAR) {
                    send_ui_command(ws, "close_sidebar", nlohmann::json::object());
                    return "Sidebar closed successfully.";
                } else {
                    verbose_err("MCP", "Unknown tool: " + name);
                    return "Unknown tool: " + name;
                }
            }
        );

        // Add assistant response to conversation
        session->add_assistant_message(full_response);

        // Store the response ID for conversation continuation
        if (!response_id.empty()) {
            session->set_openai_response_id(response_id);
        }

        // Update settings with chat info (only if materialized)
        if (session->is_materialized()) {
            update_settings(session);
        }

        // Send done message with chat ID (will be empty if still pending)
        nlohmann::json done_msg = {{"type", "done"}};
        if (session->is_materialized()) {
            done_msg["chat_id"] = session->get_chat_id();
        }
        send_json(ws, done_msg);

    } catch (const std::exception& e) {
        send_json(ws, {{"type", "error"}, {"message", e.what()}});
    }
}

void WebSocketServer::send_json(ix::WebSocket& ws, const nlohmann::json& msg) {
    std::string msg_str = msg.dump();
    verbose_out("WS", "Send: " + truncate(msg_str, 500));
    ws.send(msg_str);
}

void WebSocketServer::send_ui_command(ix::WebSocket& ws, const std::string& command, const nlohmann::json& params) {
    nlohmann::json msg = {
        {"type", "ui_command"},
        {"command", command},
        {"params", params}
    };
    verbose_log("MCP", "Sending UI command: " + command);
    send_json(ws, msg);
}

void WebSocketServer::update_settings(std::shared_ptr<ChatSession> session) {
    if (!settings_ || !session->is_materialized()) {
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

    upsert_chat(*settings_, chat);
    save_settings(*settings_);
}

} // namespace rag
