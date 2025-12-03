#pragma once

/**
 * WebSocket server for real-time chat communication.
 *
 * Handles WebSocket connections from the Elm frontend, processes chat queries,
 * and streams responses back to clients.
 */

#include "openai_client.hpp"
#include "chat.hpp"
#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace ix {
class WebSocket;
class WebSocketServer;
}

namespace rag {

/**
 * WebSocket server that handles chat queries from web clients.
 *
 * Each WebSocket connection gets its own ChatSession for conversation history.
 * Messages are JSON-formatted with a simple protocol:
 *   - Client sends: {"type": "query", "content": "..."}
 *   - Server sends: {"type": "delta", "content": "..."} for streaming
 *   - Server sends: {"type": "done"} when complete
 *   - Server sends: {"type": "error", "message": "..."} on error
 */
class WebSocketServer {
public:
    /**
     * Creates a WebSocket server.
     *
     * @param client Reference to the OpenAI client for API calls
     * @param model The model name to use for responses
     * @param vector_store_id The vector store ID for file search
     * @param reasoning_effort The reasoning effort level (low/medium/high)
     * @param system_prompt The system prompt for new chat sessions
     * @param log_dir Directory for chat logs
     */
    WebSocketServer(
        OpenAIClient& client,
        const std::string& model,
        const std::string& vector_store_id,
        const std::string& reasoning_effort,
        const std::string& system_prompt,
        const std::string& log_dir
    );

    ~WebSocketServer();

    /**
     * Sets a callback to be invoked when the server starts listening.
     */
    void on_start(std::function<void(const std::string&, int)> callback);

    /**
     * Starts the WebSocket server.
     *
     * @param address The address to bind to (e.g., "0.0.0.0")
     * @param port The port to listen on
     * @return true if the server started successfully
     */
    bool start(const std::string& address, int port);

    /**
     * Stops the WebSocket server.
     */
    void stop();

private:
    OpenAIClient& client_;
    std::string model_;
    std::string vector_store_id_;
    std::string reasoning_effort_;
    std::string system_prompt_;
    std::string log_dir_;

    std::unique_ptr<ix::WebSocketServer> server_;
    std::function<void(const std::string&, int)> on_start_callback_;

    // Per-connection chat sessions
    std::mutex sessions_mutex_;
    std::unordered_map<void*, std::unique_ptr<ChatSession>> sessions_;

    // Handles an incoming message from a client
    void handle_message(ix::WebSocket& ws, const std::string& message);

    // Sends a JSON message to a client
    void send_json(ix::WebSocket& ws, const nlohmann::json& msg);
};

} // namespace rag
