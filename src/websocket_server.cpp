#include "websocket_server.hpp"
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
                // Create a new chat session for this connection
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                sessions_[conn_id] = std::make_unique<ChatSession>(system_prompt_, log_dir_);
            }
            else if (msg->type == ix::WebSocketMessageType::Close) {
                // Clean up the session
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                sessions_.erase(conn_id);
            }
            else if (msg->type == ix::WebSocketMessageType::Message) {
                handle_message(webSocket, msg->str);
            }
            else if (msg->type == ix::WebSocketMessageType::Error) {
                std::cerr << "WebSocket error: " << msg->errorInfo.reason << std::endl;
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

void WebSocketServer::handle_message(ix::WebSocket& ws, const std::string& message) {
    try {
        auto json = nlohmann::json::parse(message);

        std::string type = json.value("type", "");
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
        ChatSession* session = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            // Use the WebSocket's underlying connection as key
            // Since we can't easily get the connection state here, we'll use a simpler approach
            // For now, we'll create a session if needed
            void* conn_id = &ws;
            auto it = sessions_.find(conn_id);
            if (it == sessions_.end()) {
                sessions_[conn_id] = std::make_unique<ChatSession>(system_prompt_, log_dir_);
                it = sessions_.find(conn_id);
            }
            session = it->second.get();
        }

        // Add user message to conversation
        session->add_user_message(content);

        // Stream the response
        std::string full_response;

        try {
            client_.stream_response(
                model_,
                session->get_conversation(),
                vector_store_id_,
                reasoning_effort_,
                [this, &ws, &full_response](const std::string& delta) {
                    full_response += delta;
                    send_json(ws, {{"type", "delta"}, {"content", delta}});
                }
            );

            // Add assistant response to conversation
            session->add_assistant_message(full_response);

            // Send done message
            send_json(ws, {{"type", "done"}});

        } catch (const std::exception& e) {
            send_json(ws, {{"type", "error"}, {"message", e.what()}});
        }

    } catch (const nlohmann::json::exception& e) {
        send_json(ws, {{"type", "error"}, {"message", "Invalid JSON"}});
    }
}

void WebSocketServer::send_json(ix::WebSocket& ws, const nlohmann::json& msg) {
    ws.send(msg.dump());
}

} // namespace rag
