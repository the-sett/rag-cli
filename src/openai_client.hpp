#pragma once

/**
 * OpenAI API client for the crag CLI.
 *
 * This is a backward-compatibility wrapper that delegates to the provider
 * abstraction layer. New code should use the providers directly.
 *
 * @deprecated Use rag::providers::openai::OpenAIProvider directly.
 */

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include "providers/types.hpp"

namespace rag {

// Re-export types from providers namespace for backward compatibility
using UploadResult = providers::UploadResult;
using DeleteResult = providers::DeleteResult;
using Message = providers::Message;
using ResponseUsage = providers::ResponseUsage;
using StreamResult = providers::StreamResult;
using OnTextCallback = providers::OnTextCallback;
using OnToolCallCallback = std::function<void(const std::string& call_id, const std::string& name, const nlohmann::json& arguments)>;
using CancelCallback = providers::CancelCallback;

// Forward declaration
namespace providers::openai {
class OpenAIProvider;
}

/**
 * HTTP client for OpenAI API interactions.
 *
 * @deprecated Use rag::providers::openai::OpenAIProvider directly.
 */
class OpenAIClient {
public:
    explicit OpenAIClient(const std::string& api_key);
    ~OpenAIClient();

    // Prevent copying
    OpenAIClient(const OpenAIClient&) = delete;
    OpenAIClient& operator=(const OpenAIClient&) = delete;

    // ========== Models API ==========
    std::vector<std::string> list_models();

    // ========== Files API ==========
    std::string upload_file(const std::string& filepath);
    std::vector<UploadResult> upload_files_parallel(
        const std::vector<std::string>& filepaths,
        std::function<void(size_t completed, size_t total)> on_progress = nullptr,
        size_t max_parallel = 8);
    void delete_file(const std::string& file_id);
    std::vector<DeleteResult> delete_files_parallel(
        const std::string& vector_store_id,
        const std::vector<std::string>& file_ids,
        std::function<void(size_t completed, size_t total)> on_progress = nullptr,
        size_t max_parallel = 8);

    // ========== Vector Stores API ==========
    std::string create_vector_store(const std::string& name);
    std::string create_file_batch(const std::string& vector_store_id,
                                   const std::vector<std::string>& file_ids);
    std::string get_batch_status(const std::string& vector_store_id,
                                  const std::string& batch_id);
    void add_file_to_vector_store(const std::string& vector_store_id,
                                   const std::string& file_id);
    void remove_file_from_vector_store(const std::string& vector_store_id,
                                        const std::string& file_id);
    void delete_vector_store(const std::string& vector_store_id);

    // ========== Responses API ==========
    StreamResult stream_response(
        const std::string& model,
        const std::vector<Message>& conversation,
        const std::string& vector_store_id,
        const std::string& reasoning_effort,
        const std::string& previous_response_id,
        std::function<void(const std::string&)> on_text,
        CancelCallback cancel_check = nullptr
    );

    StreamResult stream_response(
        const std::string& model,
        const nlohmann::json& input,
        const std::string& vector_store_id,
        const std::string& reasoning_effort,
        const std::string& previous_response_id,
        std::function<void(const std::string&)> on_text,
        CancelCallback cancel_check = nullptr
    );

    using OnToolCallWithResultCallback = std::function<std::string(const std::string& call_id, const std::string& name, const nlohmann::json& arguments)>;

    StreamResult stream_response_with_tools(
        const std::string& model,
        const std::vector<Message>& conversation,
        const std::string& vector_store_id,
        const std::string& reasoning_effort,
        const std::string& previous_response_id,
        const nlohmann::json& additional_tools,
        OnTextCallback on_text,
        OnToolCallWithResultCallback on_tool_call,
        CancelCallback cancel_check = nullptr
    );

    StreamResult stream_response_with_tools(
        const std::string& model,
        const nlohmann::json& input,
        const std::string& vector_store_id,
        const std::string& reasoning_effort,
        const std::string& previous_response_id,
        const nlohmann::json& additional_tools,
        OnTextCallback on_text,
        OnToolCallWithResultCallback on_tool_call,
        CancelCallback cancel_check = nullptr
    );

    nlohmann::json compact_window(const std::string& model, const std::string& previous_response_id);

private:
    std::unique_ptr<providers::openai::OpenAIProvider> provider_;
};

} // namespace rag
