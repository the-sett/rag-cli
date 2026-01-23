#pragma once

/**
 * OpenAI provider implementation.
 *
 * Implements the AI provider interfaces for OpenAI's API, including
 * Models, Files, Vector Stores, and Responses (chat) endpoints.
 */

#include "../provider.hpp"
#include <curl/curl.h>

namespace rag::providers::openai {

/**
 * OpenAI API base URL.
 */
constexpr const char* OPENAI_API_BASE = "https://api.openai.com/v1";

/**
 * OpenAI provider implementation.
 */
class OpenAIProvider : public IAIProvider,
                       public IModelsService,
                       public IFilesService,
                       public IKnowledgeStore,
                       public IChatService {
public:
    /**
     * Creates an OpenAI provider with the given API key.
     * @param api_key The OpenAI API key.
     * @param api_base_url Optional base URL override (defaults to OPENAI_API_BASE).
     */
    OpenAIProvider(const std::string& api_key, const std::string& api_base_url = "");
    ~OpenAIProvider() override;

    // Prevent copying
    OpenAIProvider(const OpenAIProvider&) = delete;
    OpenAIProvider& operator=(const OpenAIProvider&) = delete;

    // ========== IAIProvider ==========
    ProviderType get_type() const override { return ProviderType::OpenAI; }
    std::string get_name() const override { return "OpenAI"; }

    IModelsService& models() override { return *this; }
    IFilesService& files() override { return *this; }
    IKnowledgeStore& knowledge() override { return *this; }
    IChatService& chat() override { return *this; }

    // ========== IModelsService ==========
    std::vector<ModelInfo> list_models() override;
    std::optional<ModelInfo> get_model_info(const std::string& model_id) override;

    // ========== IFilesService ==========
    std::string upload_file(const std::string& filepath) override;
    std::vector<UploadResult> upload_files_parallel(
        const std::vector<std::string>& filepaths,
        ProgressCallback on_progress = nullptr,
        size_t max_parallel = 8
    ) override;
    void delete_file(const std::string& file_id) override;
    std::vector<DeleteResult> delete_files_parallel(
        const std::vector<std::string>& file_ids,
        const std::string& store_id = "",
        ProgressCallback on_progress = nullptr,
        size_t max_parallel = 8
    ) override;
    bool requires_file_upload() const override { return true; }

    // ========== IKnowledgeStore ==========
    std::string create_store(const std::string& name) override;
    void delete_store(const std::string& store_id) override;
    std::string add_files(const std::string& store_id, const std::vector<std::string>& file_ids) override;
    void add_file(const std::string& store_id, const std::string& file_id) override;
    void remove_file(const std::string& store_id, const std::string& file_id) override;
    std::string get_operation_status(const std::string& store_id, const std::string& operation_id) override;
    bool supports_dedicated_stores() const override { return true; }

    // ========== IChatService ==========
    StreamResult stream_response(
        const ChatConfig& config,
        const std::vector<Message>& conversation,
        OnTextCallback on_text,
        CancelCallback cancel_check = nullptr
    ) override;

    StreamResult stream_response(
        const ChatConfig& config,
        const nlohmann::json& input,
        OnTextCallback on_text,
        CancelCallback cancel_check = nullptr
    ) override;

    StreamResult stream_response_with_tools(
        const ChatConfig& config,
        const std::vector<Message>& conversation,
        OnTextCallback on_text,
        OnToolCallCallback on_tool_call,
        CancelCallback cancel_check = nullptr
    ) override;

    StreamResult stream_response_with_tools(
        const ChatConfig& config,
        const nlohmann::json& input,
        OnTextCallback on_text,
        OnToolCallCallback on_tool_call,
        CancelCallback cancel_check = nullptr
    ) override;

    std::optional<nlohmann::json> compact_window(
        const std::string& model,
        const std::string& previous_response_id
    ) override;

    bool supports_compaction() const override { return true; }

private:
    std::string api_key_;
    std::string api_base_;

    // ========== HTTP Helpers ==========
    std::string http_get(const std::string& url);
    std::string http_post_json(const std::string& url, const nlohmann::json& body);
    std::string http_post_multipart(const std::string& url,
                                    const std::string& filepath,
                                    const std::string& purpose,
                                    const std::string& display_filename = "");
    bool http_post_stream(const std::string& url,
                          const nlohmann::json& body,
                          std::function<void(const std::string&)> on_data,
                          CancelCallback cancel_check = nullptr);
    std::string http_delete(const std::string& url);
};

} // namespace rag::providers::openai
