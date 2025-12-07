#include "websocket_server.hpp"
#include "settings.hpp"
#include "config.hpp"
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
                // Don't create session yet - wait for init message from client
            }
            else if (msg->type == ix::WebSocketMessageType::Close) {
                // Clean up the session
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                sessions_.erase(conn_id);
            }
            else if (msg->type == ix::WebSocketMessageType::Message) {
                handle_message(conn_id, webSocket, msg->str);
            }
            else if (msg->type == ix::WebSocketMessageType::Error) {
                // Ignore "Could not parse url" errors - these are typically from
                // non-WebSocket requests (like browser pre-flight checks)
                if (msg->errorInfo.reason.find("Could not parse url") == std::string::npos) {
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
            handle_init(conn_id, ws, chat_id);
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

void WebSocketServer::handle_init(void* conn_id, ix::WebSocket& ws, const std::string& chat_id) {
    std::shared_ptr<ChatSession> session;

    if (!chat_id.empty() && settings_) {
        // Try to load existing chat
        const ChatInfo* chat_info = find_chat(*settings_, chat_id);
        if (chat_info) {
            session = ChatSession::load(chat_info->json_file, system_prompt_);
            if (session) {
                // Restore the OpenAI response ID for continuation
                session->set_openai_response_id(chat_info->openai_response_id);
            }
        }
    }

    if (!session) {
        // Create new session (starts in pending state - no files yet)
        session = std::make_shared<ChatSession>(system_prompt_, log_dir_);
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
    // Check if we have a cached intro message
    if (settings_ && !settings_->cached_intro_message.empty()) {
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
    // Track if this is the first real message (will materialize the chat)
    bool was_pending = !session->is_materialized();

    // Add user message to conversation
    if (hidden) {
        session->add_hidden_user_message(content);
    } else {
        session->add_user_message(content);  // This materializes if pending
    }

    // Stream the response
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
    ws.send(msg.dump());
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

    upsert_chat(*settings_, chat);
    save_settings(*settings_);
}

} // namespace rag
