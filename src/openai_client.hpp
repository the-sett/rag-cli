#pragma once

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace rag {

struct Message {
    std::string role;    // "system", "user", "assistant"
    std::string content;

    nlohmann::json to_json() const {
        return {{"role", role}, {"content", content}};
    }
};

class OpenAIClient {
public:
    explicit OpenAIClient(const std::string& api_key);
    ~OpenAIClient();

    // Models API - list available models, filtered to gpt-5*
    std::vector<std::string> list_models();

    // Files API - upload a file, returns file ID
    std::string upload_file(const std::string& filepath);

    // Vector Stores API
    std::string create_vector_store(const std::string& name);
    std::string create_file_batch(const std::string& vector_store_id,
                                   const std::vector<std::string>& file_ids);
    std::string get_batch_status(const std::string& vector_store_id,
                                  const std::string& batch_id);

    // Responses API with streaming
    // on_text callback is called for each text delta
    void stream_response(
        const std::string& model,
        const std::vector<Message>& conversation,
        const std::string& vector_store_id,
        const std::string& reasoning_effort,
        std::function<void(const std::string&)> on_text
    );

private:
    std::string api_key_;

    // HTTP helpers
    std::string http_get(const std::string& url);
    std::string http_post_json(const std::string& url, const nlohmann::json& body);
    std::string http_post_multipart(const std::string& url,
                                     const std::string& filepath,
                                     const std::string& purpose);
    void http_post_stream(const std::string& url,
                          const nlohmann::json& body,
                          std::function<void(const std::string&)> on_data);
};

} // namespace rag
