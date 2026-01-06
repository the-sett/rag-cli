#pragma once

/**
 * MCP (Model Context Protocol) server for crag.
 *
 * Implements the MCP protocol over stdio, allowing Claude Code and other
 * MCP clients to query the knowledge base with persistent conversation context.
 */

#include "openai_client.hpp"
#include "settings.hpp"
#include "chat.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <unordered_map>

namespace rag {

/**
 * MCP server that exposes crag as a set of tools.
 *
 * Tools:
 * - query: Query the knowledge base, optionally continuing an existing conversation
 * - get_status: Get information about the knowledge base configuration
 * - list_chats: List previous conversations that can be continued
 *
 * Communication is via JSON-RPC 2.0 over stdio.
 */
class MCPServer {
public:
    MCPServer(
        OpenAIClient& client,
        Settings& settings,
        const std::string& model,
        const std::string& vector_store_id,
        const std::string& reasoning_effort,
        const std::string& system_prompt,
        const std::string& log_dir
    );

    // Main loop - reads from stdin, writes to stdout.
    // Blocks until stdin is closed.
    void run();

private:
    // JSON-RPC message handling
    void handle_message(const std::string& line);
    void send_response(int id, const nlohmann::json& result);
    void send_error(int id, int code, const std::string& message);
    void send_notification(const std::string& method, const nlohmann::json& params = nlohmann::json::object());

    // MCP protocol methods
    nlohmann::json handle_initialize(const nlohmann::json& params);
    nlohmann::json handle_tools_list();
    nlohmann::json handle_tools_call(const nlohmann::json& params);

    // Tool implementations
    nlohmann::json tool_query(const nlohmann::json& args);
    nlohmann::json tool_get_status(const nlohmann::json& args);
    nlohmann::json tool_list_chats(const nlohmann::json& args);

    // Helper to get or load a chat session
    std::shared_ptr<ChatSession> get_or_create_session(const std::string& chat_id);

    // Helper to update settings after a query
    void update_settings(std::shared_ptr<ChatSession> session);

    // State
    OpenAIClient& client_;
    Settings& settings_;
    std::string model_;
    std::string vector_store_id_;
    std::string reasoning_effort_;
    std::string system_prompt_;
    std::string log_dir_;

    // Session cache - maps chat_id to loaded session
    std::unordered_map<std::string, std::shared_ptr<ChatSession>> session_cache_;

    bool initialized_ = false;
};

} // namespace rag
