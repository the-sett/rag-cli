#pragma once

/**
 * Abstract interfaces for AI provider services.
 *
 * These interfaces define the contract that all AI providers must implement,
 * allowing the application to work with different providers (OpenAI, Gemini, etc.)
 * through a common API.
 */

#include "types.hpp"
#include <memory>
#include <optional>

namespace rag::providers {

/**
 * Interface for listing and querying available models.
 */
class IModelsService {
public:
    virtual ~IModelsService() = default;

    /**
     * Lists available models from this provider.
     * The list may be filtered to models relevant to this application.
     */
    virtual std::vector<ModelInfo> list_models() = 0;

    /**
     * Gets information about a specific model.
     * Returns nullopt if the model is not found or not supported.
     */
    virtual std::optional<ModelInfo> get_model_info(const std::string& model_id) = 0;
};

/**
 * Interface for file upload and deletion operations.
 *
 * Note: Some providers may not require file uploads (e.g., if they use inline content).
 * In such cases, these methods may be no-ops or throw unsupported operation exceptions.
 */
class IFilesService {
public:
    virtual ~IFilesService() = default;

    /**
     * Uploads a single file.
     * Returns the provider-specific file ID.
     * Throws on error.
     */
    virtual std::string upload_file(const std::string& filepath) = 0;

    /**
     * Uploads multiple files in parallel.
     * Returns results for all files, including any errors.
     */
    virtual std::vector<UploadResult> upload_files_parallel(
        const std::vector<std::string>& filepaths,
        ProgressCallback on_progress = nullptr,
        size_t max_parallel = 8
    ) = 0;

    /**
     * Deletes a single file from the provider's storage.
     * Throws on error.
     */
    virtual void delete_file(const std::string& file_id) = 0;

    /**
     * Deletes multiple files in parallel.
     * If store_id is provided, removes files from the store first, then deletes.
     * Returns results for all files, including any errors.
     */
    virtual std::vector<DeleteResult> delete_files_parallel(
        const std::vector<std::string>& file_ids,
        const std::string& store_id = "",
        ProgressCallback on_progress = nullptr,
        size_t max_parallel = 8
    ) = 0;

    /**
     * Returns true if this provider requires file uploads for RAG.
     * If false, documents may be passed inline in chat requests.
     */
    virtual bool requires_file_upload() const = 0;
};

/**
 * Interface for knowledge/RAG storage.
 *
 * This abstracts over different RAG approaches:
 * - OpenAI: Uses dedicated Vector Stores with file_search tool
 * - Gemini: May use inline context or Google Search grounding
 */
class IKnowledgeStore {
public:
    virtual ~IKnowledgeStore() = default;

    /**
     * Creates a new knowledge store.
     * Returns the provider-specific store ID.
     */
    virtual std::string create_store(const std::string& name) = 0;

    /**
     * Deletes a knowledge store and optionally its contents.
     */
    virtual void delete_store(const std::string& store_id) = 0;

    /**
     * Adds multiple files to a knowledge store as a batch.
     * Returns an operation ID for status tracking.
     */
    virtual std::string add_files(
        const std::string& store_id,
        const std::vector<std::string>& file_ids
    ) = 0;

    /**
     * Adds a single file to a knowledge store.
     */
    virtual void add_file(const std::string& store_id, const std::string& file_id) = 0;

    /**
     * Removes a file from a knowledge store (does not delete the file itself).
     */
    virtual void remove_file(const std::string& store_id, const std::string& file_id) = 0;

    /**
     * Gets the status of a batch operation.
     * Returns the status string (e.g., "in_progress", "completed", "failed").
     */
    virtual std::string get_operation_status(
        const std::string& store_id,
        const std::string& operation_id
    ) = 0;

    /**
     * Returns true if this provider supports dedicated knowledge stores.
     * If false, RAG may be implemented via inline context or other means.
     */
    virtual bool supports_dedicated_stores() const = 0;
};

/**
 * Interface for chat/response streaming.
 */
class IChatService {
public:
    virtual ~IChatService() = default;

    /**
     * Streams a response from the model.
     * The on_text callback is invoked for each text delta received.
     * Returns StreamResult with response ID and token usage.
     */
    virtual StreamResult stream_response(
        const ChatConfig& config,
        const std::vector<Message>& conversation,
        OnTextCallback on_text,
        CancelCallback cancel_check = nullptr
    ) = 0;

    /**
     * Streams a response using JSON input (for compacted conversations).
     */
    virtual StreamResult stream_response(
        const ChatConfig& config,
        const nlohmann::json& input,
        OnTextCallback on_text,
        CancelCallback cancel_check = nullptr
    ) = 0;

    /**
     * Streams a response with tool/function calling support.
     * The on_tool_call callback is invoked when the model requests a tool call.
     * It should execute the tool and return the result as a string.
     */
    virtual StreamResult stream_response_with_tools(
        const ChatConfig& config,
        const std::vector<Message>& conversation,
        OnTextCallback on_text,
        OnToolCallCallback on_tool_call,
        CancelCallback cancel_check = nullptr
    ) = 0;

    /**
     * Streams a response with tools using JSON input.
     */
    virtual StreamResult stream_response_with_tools(
        const ChatConfig& config,
        const nlohmann::json& input,
        OnTextCallback on_text,
        OnToolCallCallback on_tool_call,
        CancelCallback cancel_check = nullptr
    ) = 0;

    /**
     * Compacts a long-running conversation window.
     * Returns the compacted input for the next request, or nullopt if not supported.
     */
    virtual std::optional<nlohmann::json> compact_window(
        const std::string& model,
        const std::string& previous_response_id
    ) = 0;

    /**
     * Returns true if this provider supports conversation compaction.
     */
    virtual bool supports_compaction() const = 0;
};

/**
 * Composite interface that combines all provider services.
 *
 * Providers implement this interface to provide a unified API for all operations.
 * The individual service interfaces are also exposed for cases where only
 * specific functionality is needed.
 */
class IAIProvider {
public:
    virtual ~IAIProvider() = default;

    /**
     * Returns the provider type.
     */
    virtual ProviderType get_type() const = 0;

    /**
     * Returns a human-readable provider name.
     */
    virtual std::string get_name() const = 0;

    /**
     * Access to individual services.
     */
    virtual IModelsService& models() = 0;
    virtual IFilesService& files() = 0;
    virtual IKnowledgeStore& knowledge() = 0;
    virtual IChatService& chat() = 0;
};

} // namespace rag::providers
