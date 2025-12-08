#pragma once

/**
 * MCP (Model Context Protocol) tool definitions for UI control.
 *
 * Defines tools that allow the AI to control the web UI, such as
 * opening and closing the sidebar panel.
 */

#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <functional>

namespace rag {

/**
 * Available MCP tool names.
 */
constexpr const char* TOOL_OPEN_SIDEBAR = "open_sidebar";
constexpr const char* TOOL_CLOSE_SIDEBAR = "close_sidebar";

/**
 * Result of executing an MCP tool.
 */
struct ToolResult {
    bool success;
    std::string message;
};

/**
 * Parsed tool call from OpenAI response.
 */
struct ToolCall {
    std::string id;           // Tool call ID for response
    std::string name;         // Tool name
    nlohmann::json arguments; // Tool arguments (parsed JSON)
};

/**
 * Returns the JSON tool definitions for OpenAI's Responses API function calling.
 *
 * These are added to the "tools" array in the API request alongside
 * the file_search tool.
 *
 * Note: The Responses API uses a flat format:
 *   {"type": "function", "name": "...", "description": "...", "parameters": {...}}
 * NOT the Chat Completions nested format:
 *   {"type": "function", "function": {"name": "...", ...}}
 */
inline nlohmann::json get_mcp_tool_definitions() {
    return nlohmann::json::array({
        {
            {"type", "function"},
            {"name", TOOL_OPEN_SIDEBAR},
            {"description", "Opens the sidebar panel on the left side of the chat interface. "
                           "Use this when the user wants to see the table of contents or navigation."},
            {"parameters", {
                {"type", "object"},
                {"properties", nlohmann::json::object()},
                {"required", nlohmann::json::array()}
            }}
        },
        {
            {"type", "function"},
            {"name", TOOL_CLOSE_SIDEBAR},
            {"description", "Closes the sidebar panel on the left side of the chat interface. "
                           "Use this when the user wants more space for the chat or wants to hide the navigation."},
            {"parameters", {
                {"type", "object"},
                {"properties", nlohmann::json::object()},
                {"required", nlohmann::json::array()}
            }}
        }
    });
}

/**
 * Parses a tool call from an OpenAI response event.
 *
 * @param event The JSON event from the streaming response
 * @return The parsed tool call, or nullopt if not a tool call event
 */
inline std::optional<ToolCall> parse_tool_call(const nlohmann::json& event) {
    // Look for function_call in the response
    if (!event.contains("type")) {
        return std::nullopt;
    }

    std::string event_type = event["type"].get<std::string>();

    // Handle response.output_item.added with function_call type
    if (event_type == "response.output_item.added") {
        if (event.contains("item") && event["item"].contains("type")) {
            if (event["item"]["type"] == "function_call") {
                ToolCall call;
                call.id = event["item"].value("call_id", "");
                call.name = event["item"].value("name", "");
                // Arguments might be empty initially, will be filled by delta events
                call.arguments = nlohmann::json::object();
                return call;
            }
        }
    }

    // Handle response.function_call_arguments.done - final arguments
    if (event_type == "response.function_call_arguments.done") {
        ToolCall call;
        call.id = event.value("call_id", "");
        call.name = event.value("name", "");
        std::string args_str = event.value("arguments", "{}");
        try {
            call.arguments = nlohmann::json::parse(args_str);
        } catch (...) {
            call.arguments = nlohmann::json::object();
        }
        return call;
    }

    return std::nullopt;
}

/**
 * Executes an MCP tool and returns the result.
 *
 * @param tool_call The tool call to execute
 * @param on_ui_command Callback to send UI commands to the client
 * @return The result of the tool execution
 */
inline ToolResult execute_tool(
    const ToolCall& tool_call,
    std::function<void(const std::string& command, const nlohmann::json& params)> on_ui_command
) {
    if (tool_call.name == TOOL_OPEN_SIDEBAR) {
        on_ui_command("open_sidebar", nlohmann::json::object());
        return {true, "Sidebar opened successfully."};
    }

    if (tool_call.name == TOOL_CLOSE_SIDEBAR) {
        on_ui_command("close_sidebar", nlohmann::json::object());
        return {true, "Sidebar closed successfully."};
    }

    return {false, "Unknown tool: " + tool_call.name};
}

/**
 * Creates the tool output JSON for submitting tool results back to OpenAI.
 *
 * @param call_id The tool call ID
 * @param result The result of the tool execution
 * @return JSON object for the tool output
 */
inline nlohmann::json create_tool_output(const std::string& call_id, const ToolResult& result) {
    return {
        {"type", "function_call_output"},
        {"call_id", call_id},
        {"output", result.message}
    };
}

} // namespace rag
