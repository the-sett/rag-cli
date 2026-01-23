#include "openai_client.hpp"
#include "providers/openai/openai_provider.hpp"

namespace rag {

OpenAIClient::OpenAIClient(const std::string& api_key)
    : provider_(std::make_unique<providers::openai::OpenAIProvider>(api_key)) {
}

OpenAIClient::~OpenAIClient() = default;

std::vector<std::string> OpenAIClient::list_models() {
    auto models = provider_->list_models();
    std::vector<std::string> result;
    result.reserve(models.size());
    for (const auto& m : models) {
        result.push_back(m.id);
    }
    return result;
}

std::string OpenAIClient::upload_file(const std::string& filepath) {
    return provider_->upload_file(filepath);
}

std::vector<UploadResult> OpenAIClient::upload_files_parallel(
    const std::vector<std::string>& filepaths,
    std::function<void(size_t completed, size_t total)> on_progress,
    size_t max_parallel
) {
    return provider_->upload_files_parallel(filepaths, on_progress, max_parallel);
}

void OpenAIClient::delete_file(const std::string& file_id) {
    provider_->delete_file(file_id);
}

std::vector<DeleteResult> OpenAIClient::delete_files_parallel(
    const std::string& vector_store_id,
    const std::vector<std::string>& file_ids,
    std::function<void(size_t completed, size_t total)> on_progress,
    size_t max_parallel
) {
    return provider_->delete_files_parallel(file_ids, vector_store_id, on_progress, max_parallel);
}

std::string OpenAIClient::create_vector_store(const std::string& name) {
    return provider_->create_store(name);
}

std::string OpenAIClient::create_file_batch(const std::string& vector_store_id,
                                             const std::vector<std::string>& file_ids) {
    return provider_->add_files(vector_store_id, file_ids);
}

std::string OpenAIClient::get_batch_status(const std::string& vector_store_id,
                                            const std::string& batch_id) {
    return provider_->get_operation_status(vector_store_id, batch_id);
}

void OpenAIClient::add_file_to_vector_store(const std::string& vector_store_id,
                                             const std::string& file_id) {
    provider_->add_file(vector_store_id, file_id);
}

void OpenAIClient::remove_file_from_vector_store(const std::string& vector_store_id,
                                                  const std::string& file_id) {
    provider_->remove_file(vector_store_id, file_id);
}

void OpenAIClient::delete_vector_store(const std::string& vector_store_id) {
    provider_->delete_store(vector_store_id);
}

StreamResult OpenAIClient::stream_response(
    const std::string& model,
    const std::vector<Message>& conversation,
    const std::string& vector_store_id,
    const std::string& reasoning_effort,
    const std::string& previous_response_id,
    std::function<void(const std::string&)> on_text,
    CancelCallback cancel_check
) {
    providers::ChatConfig config;
    config.model = model;
    config.knowledge_store_id = vector_store_id;
    config.reasoning_effort = reasoning_effort;
    config.previous_response_id = previous_response_id;

    return provider_->stream_response(config, conversation, on_text, cancel_check);
}

StreamResult OpenAIClient::stream_response(
    const std::string& model,
    const nlohmann::json& input,
    const std::string& vector_store_id,
    const std::string& reasoning_effort,
    const std::string& previous_response_id,
    std::function<void(const std::string&)> on_text,
    CancelCallback cancel_check
) {
    providers::ChatConfig config;
    config.model = model;
    config.knowledge_store_id = vector_store_id;
    config.reasoning_effort = reasoning_effort;
    config.previous_response_id = previous_response_id;

    return provider_->stream_response(config, input, on_text, cancel_check);
}

StreamResult OpenAIClient::stream_response_with_tools(
    const std::string& model,
    const std::vector<Message>& conversation,
    const std::string& vector_store_id,
    const std::string& reasoning_effort,
    const std::string& previous_response_id,
    const nlohmann::json& additional_tools,
    OnTextCallback on_text,
    OnToolCallWithResultCallback on_tool_call,
    CancelCallback cancel_check
) {
    providers::ChatConfig config;
    config.model = model;
    config.knowledge_store_id = vector_store_id;
    config.reasoning_effort = reasoning_effort;
    config.previous_response_id = previous_response_id;
    config.additional_tools = additional_tools;

    return provider_->stream_response_with_tools(config, conversation, on_text, on_tool_call, cancel_check);
}

StreamResult OpenAIClient::stream_response_with_tools(
    const std::string& model,
    const nlohmann::json& input,
    const std::string& vector_store_id,
    const std::string& reasoning_effort,
    const std::string& previous_response_id,
    const nlohmann::json& additional_tools,
    OnTextCallback on_text,
    OnToolCallWithResultCallback on_tool_call,
    CancelCallback cancel_check
) {
    providers::ChatConfig config;
    config.model = model;
    config.knowledge_store_id = vector_store_id;
    config.reasoning_effort = reasoning_effort;
    config.previous_response_id = previous_response_id;
    config.additional_tools = additional_tools;

    return provider_->stream_response_with_tools(config, input, on_text, on_tool_call, cancel_check);
}

nlohmann::json OpenAIClient::compact_window(const std::string& model, const std::string& previous_response_id) {
    auto result = provider_->compact_window(model, previous_response_id);
    if (result) {
        return *result;
    }
    throw std::runtime_error("Compact window failed");
}

} // namespace rag
