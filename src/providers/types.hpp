#pragma once

/**
 * Common types for AI provider interfaces.
 *
 * These types are provider-agnostic and used across all provider implementations.
 */

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace rag::providers {

/**
 * Identifies the AI provider.
 */
enum class ProviderType {
    OpenAI,
    Gemini
    // Future: Anthropic, Azure, etc.
};

/**
 * A chat message with role and content.
 */
struct Message {
    std::string role;    // "system", "user", or "assistant"
    std::string content; // The text content of the message

    nlohmann::json to_json() const {
        return {{"role", role}, {"content", content}};
    }
};

/**
 * Token usage information for a response.
 */
struct ResponseUsage {
    int input_tokens = 0;      // Tokens in the prompt
    int output_tokens = 0;     // Tokens generated in the response
    int reasoning_tokens = 0;  // For reasoning models; 0 otherwise
};

/**
 * Result of a streaming response.
 */
struct StreamResult {
    std::string response_id;   // Provider-specific ID for conversation continuation
    ResponseUsage usage;       // Token usage reported by the provider
    bool cancelled = false;    // True if the stream was cancelled
};

/**
 * Result of a file upload operation.
 */
struct UploadResult {
    std::string filepath;      // Original file path
    std::string file_id;       // Provider-specific file ID (empty on error)
    std::string error;         // Error message (empty on success)

    bool success() const { return !file_id.empty(); }
};

/**
 * Result of a file deletion operation.
 */
struct DeleteResult {
    std::string file_id;       // Provider-specific file ID that was deleted
    std::string error;         // Error message (empty on success)

    bool success() const { return error.empty(); }
};

/**
 * Information about an available model.
 */
struct ModelInfo {
    std::string id;                    // Model identifier (e.g., "gpt-4o", "gemini-2.0-flash")
    std::string display_name;          // Human-readable name
    int max_context_tokens = 128000;   // Maximum context window size
    bool supports_tools = true;        // Whether the model supports function calling
    bool supports_reasoning = false;   // Whether the model has reasoning capabilities
};

/**
 * Status of a knowledge store operation (e.g., batch indexing).
 */
enum class StoreStatus {
    Pending,
    Processing,
    Completed,
    Failed
};

/**
 * Configuration for a chat request.
 */
struct ChatConfig {
    std::string model;                  // Model to use
    std::string reasoning_effort;       // "low", "medium", "high" (if supported)
    std::string knowledge_store_id;     // Optional knowledge store for RAG
    std::string previous_response_id;   // For conversation continuation
    nlohmann::json additional_tools;    // Additional tools beyond default (e.g., file_search)
};

// Callback types

/**
 * Callback invoked when text is received during streaming.
 */
using OnTextCallback = std::function<void(const std::string&)>;

/**
 * Callback invoked when a tool call is requested.
 * Should execute the tool and return the result as a string.
 */
using OnToolCallCallback = std::function<std::string(
    const std::string& call_id,
    const std::string& name,
    const nlohmann::json& arguments
)>;

/**
 * Callback to check if cancellation has been requested.
 * Returns true if the operation should be cancelled.
 */
using CancelCallback = std::function<bool()>;

/**
 * Callback for progress updates during batch operations.
 */
using ProgressCallback = std::function<void(size_t completed, size_t total)>;

} // namespace rag::providers
