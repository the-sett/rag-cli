#include "gemini_provider.hpp"
#include <stdexcept>

namespace rag::providers::gemini {

GeminiProvider::GeminiProvider(const std::string& api_key, const std::string& api_base_url)
    : api_key_(api_key)
    , api_base_(api_base_url.empty() ? GEMINI_API_BASE : api_base_url) {
}

GeminiProvider::~GeminiProvider() = default;

// ========== IModelsService ==========

std::vector<ModelInfo> GeminiProvider::list_models() {
    // Return a static list of known Gemini models
    return {
        {"gemini-2.0-flash", "Gemini 2.0 Flash", 1000000, true, false},
        {"gemini-2.0-flash-thinking-exp", "Gemini 2.0 Flash Thinking", 1000000, true, true},
        {"gemini-1.5-pro", "Gemini 1.5 Pro", 2000000, true, false},
        {"gemini-1.5-flash", "Gemini 1.5 Flash", 1000000, true, false},
    };
}

std::optional<ModelInfo> GeminiProvider::get_model_info(const std::string& model_id) {
    auto models = list_models();
    for (const auto& model : models) {
        if (model.id == model_id) {
            return model;
        }
    }
    return std::nullopt;
}

// ========== IFilesService ==========

std::string GeminiProvider::upload_file(const std::string& /*filepath*/) {
    throw std::runtime_error("Gemini provider: file upload not yet implemented");
}

std::vector<UploadResult> GeminiProvider::upload_files_parallel(
    const std::vector<std::string>& filepaths,
    ProgressCallback /*on_progress*/,
    size_t /*max_parallel*/
) {
    std::vector<UploadResult> results;
    for (const auto& fp : filepaths) {
        results.push_back({fp, "", "Gemini provider: file upload not yet implemented"});
    }
    return results;
}

void GeminiProvider::delete_file(const std::string& /*file_id*/) {
    throw std::runtime_error("Gemini provider: file deletion not yet implemented");
}

std::vector<DeleteResult> GeminiProvider::delete_files_parallel(
    const std::vector<std::string>& file_ids,
    const std::string& /*store_id*/,
    ProgressCallback /*on_progress*/,
    size_t /*max_parallel*/
) {
    std::vector<DeleteResult> results;
    for (const auto& fid : file_ids) {
        results.push_back({fid, "Gemini provider: file deletion not yet implemented"});
    }
    return results;
}

// ========== IKnowledgeStore ==========

std::string GeminiProvider::create_store(const std::string& /*name*/) {
    throw std::runtime_error("Gemini provider does not support dedicated knowledge stores");
}

void GeminiProvider::delete_store(const std::string& /*store_id*/) {
    throw std::runtime_error("Gemini provider does not support dedicated knowledge stores");
}

std::string GeminiProvider::add_files(const std::string& /*store_id*/, const std::vector<std::string>& /*file_ids*/) {
    throw std::runtime_error("Gemini provider does not support dedicated knowledge stores");
}

void GeminiProvider::add_file(const std::string& /*store_id*/, const std::string& /*file_id*/) {
    throw std::runtime_error("Gemini provider does not support dedicated knowledge stores");
}

void GeminiProvider::remove_file(const std::string& /*store_id*/, const std::string& /*file_id*/) {
    throw std::runtime_error("Gemini provider does not support dedicated knowledge stores");
}

std::string GeminiProvider::get_operation_status(const std::string& /*store_id*/, const std::string& /*operation_id*/) {
    throw std::runtime_error("Gemini provider does not support dedicated knowledge stores");
}

// ========== IChatService ==========

StreamResult GeminiProvider::stream_response(
    const ChatConfig& /*config*/,
    const std::vector<Message>& /*conversation*/,
    OnTextCallback /*on_text*/,
    CancelCallback /*cancel_check*/
) {
    throw std::runtime_error("Gemini provider: chat streaming not yet implemented");
}

StreamResult GeminiProvider::stream_response(
    const ChatConfig& /*config*/,
    const nlohmann::json& /*input*/,
    OnTextCallback /*on_text*/,
    CancelCallback /*cancel_check*/
) {
    throw std::runtime_error("Gemini provider: chat streaming not yet implemented");
}

StreamResult GeminiProvider::stream_response_with_tools(
    const ChatConfig& /*config*/,
    const std::vector<Message>& /*conversation*/,
    OnTextCallback /*on_text*/,
    OnToolCallCallback /*on_tool_call*/,
    CancelCallback /*cancel_check*/
) {
    throw std::runtime_error("Gemini provider: chat streaming with tools not yet implemented");
}

StreamResult GeminiProvider::stream_response_with_tools(
    const ChatConfig& /*config*/,
    const nlohmann::json& /*input*/,
    OnTextCallback /*on_text*/,
    OnToolCallCallback /*on_tool_call*/,
    CancelCallback /*cancel_check*/
) {
    throw std::runtime_error("Gemini provider: chat streaming with tools not yet implemented");
}

std::optional<nlohmann::json> GeminiProvider::compact_window(
    const std::string& /*model*/,
    const std::string& /*previous_response_id*/
) {
    // Gemini doesn't support conversation compaction
    return std::nullopt;
}

} // namespace rag::providers::gemini
