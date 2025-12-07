#pragma once

/**
 * OpenAI API client for the crag CLI.
 *
 * Provides methods for interacting with OpenAI's Models, Files, Vector Stores,
 * and Responses APIs using libcurl for HTTP transport.
 */

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace rag {

/**
 * A chat message with role and content.
 */
struct Message {
    std::string role;    // Message role: "system", "user", or "assistant".
    std::string content; // The text content of the message.

    // Converts this message to JSON format for the API.
    nlohmann::json to_json() const {
        return {{"role", role}, {"content", content}};
    }
};

/**
 * HTTP client for OpenAI API interactions.
 *
 * Handles authentication, request formatting, and response parsing for all
 * OpenAI API endpoints used by the application.
 */
class OpenAIClient {
public:
    // Creates a client with the given API key.
    explicit OpenAIClient(const std::string& api_key);

    // Cleans up CURL global state.
    ~OpenAIClient();

    // ========== Models API ==========

    // Lists available models, filtered to gpt-5* models only.
    std::vector<std::string> list_models();

    // ========== Files API ==========

    // Uploads a file for use with assistants. Returns the file ID.
    std::string upload_file(const std::string& filepath);

    // Deletes a file from OpenAI storage.
    void delete_file(const std::string& file_id);

    // ========== Vector Stores API ==========

    // Creates a new vector store with the given name. Returns the store ID.
    std::string create_vector_store(const std::string& name);

    // Creates a batch of files in a vector store. Returns the batch ID.
    std::string create_file_batch(const std::string& vector_store_id,
                                   const std::vector<std::string>& file_ids);

    // Gets the status of a file batch operation.
    std::string get_batch_status(const std::string& vector_store_id,
                                  const std::string& batch_id);

    // Adds a single file to an existing vector store.
    void add_file_to_vector_store(const std::string& vector_store_id,
                                   const std::string& file_id);

    // Removes a file from a vector store (does not delete the file itself).
    void remove_file_from_vector_store(const std::string& vector_store_id,
                                        const std::string& file_id);

    // ========== Responses API ==========

    // Streams a response from the model with file_search tool enabled.
    // The on_text callback is invoked for each text delta received.
    // If previous_response_id is provided, continues an existing conversation.
    // Returns the new response ID for conversation continuation.
    std::string stream_response(
        const std::string& model,
        const std::vector<Message>& conversation,
        const std::string& vector_store_id,
        const std::string& reasoning_effort,
        const std::string& previous_response_id,
        std::function<void(const std::string&)> on_text
    );

private:
    std::string api_key_;  // OpenAI API key.

    // ========== HTTP Helpers ==========

    // Performs an HTTP GET request.
    std::string http_get(const std::string& url);

    // Performs an HTTP POST with JSON body.
    std::string http_post_json(const std::string& url, const nlohmann::json& body);

    // Performs an HTTP POST with multipart form data for file upload.
    // If display_filename is provided, it overrides the filename sent to the API.
    std::string http_post_multipart(const std::string& url,
                                     const std::string& filepath,
                                     const std::string& purpose,
                                     const std::string& display_filename = "");

    // Performs a streaming HTTP POST for Server-Sent Events.
    void http_post_stream(const std::string& url,
                          const nlohmann::json& body,
                          std::function<void(const std::string&)> on_data);

    // Performs an HTTP DELETE request.
    std::string http_delete(const std::string& url);
};

} // namespace rag
