#include "openai_client.hpp"
#include "config.hpp"
#include <curl/curl.h>
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <filesystem>

namespace rag {

using json = nlohmann::json;

// CURL write callback for collecting response data into a string.
static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* response = static_cast<std::string*>(userdata);
    response->append(ptr, size * nmemb);
    return size * nmemb;
}

// Context for streaming SSE responses.
struct StreamContext {
    std::function<void(const std::string&)> on_data;  // Callback for each data event.
    std::string buffer;                                // Partial line buffer.
};

// CURL write callback for streaming SSE data. Parses Server-Sent Events and
// invokes the context callback for each data event.
static size_t stream_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<StreamContext*>(userdata);
    size_t total_size = size * nmemb;

    ctx->buffer.append(ptr, total_size);

    // Process complete SSE lines.
    size_t pos;
    while ((pos = ctx->buffer.find("\n")) != std::string::npos) {
        std::string line = ctx->buffer.substr(0, pos);
        ctx->buffer.erase(0, pos + 1);

        // Remove trailing carriage return if present.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // SSE format: "data: {...}" or "data: [DONE]".
        if (line.substr(0, 6) == "data: ") {
            std::string data = line.substr(6);
            if (data != "[DONE]") {
                ctx->on_data(data);
            }
        }
    }

    return total_size;
}

OpenAIClient::OpenAIClient(const std::string& api_key) : api_key_(api_key) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

OpenAIClient::~OpenAIClient() {
    curl_global_cleanup();
}

std::string OpenAIClient::http_get(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response;
    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + api_key_;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP GET failed: ") + curl_easy_strerror(res));
    }

    return response;
}

std::string OpenAIClient::http_post_json(const std::string& url, const json& body) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response;
    std::string body_str = body.dump();

    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + api_key_;
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP POST failed: ") + curl_easy_strerror(res));
    }

    return response;
}

std::string OpenAIClient::http_post_multipart(const std::string& url,
                                               const std::string& filepath,
                                               const std::string& purpose) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response;

    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + api_key_;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_mime* mime = curl_mime_init(curl);

    // Add file part.
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, filepath.c_str());

    // Add purpose part.
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "purpose");
    curl_mime_data(part, purpose.c_str(), CURL_ZERO_TERMINATED);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    curl_mime_free(mime);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP POST multipart failed: ") + curl_easy_strerror(res));
    }

    return response;
}

void OpenAIClient::http_post_stream(const std::string& url,
                                     const json& body,
                                     std::function<void(const std::string&)> on_data) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string body_str = body.dump();
    StreamContext ctx{on_data, ""};

    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + api_key_;
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP streaming POST failed: ") + curl_easy_strerror(res));
    }
}

std::vector<std::string> OpenAIClient::list_models() {
    std::string url = std::string(OPENAI_API_BASE) + "/models";
    std::string response = http_get(url);

    json j = json::parse(response);
    std::vector<std::string> models;

    if (j.contains("data") && j["data"].is_array()) {
        for (const auto& model : j["data"]) {
            std::string id = model.value("id", "");
            // Filter to gpt-5* models only.
            if (id.substr(0, 5) == "gpt-5") {
                models.push_back(id);
            }
        }
    }

    std::sort(models.begin(), models.end());
    return models;
}

std::string OpenAIClient::upload_file(const std::string& filepath) {
    std::string url = std::string(OPENAI_API_BASE) + "/files";
    std::string response = http_post_multipart(url, filepath, "assistants");

    json j = json::parse(response);

    if (j.contains("error")) {
        throw std::runtime_error("File upload failed: " + j["error"]["message"].get<std::string>());
    }

    return j.value("id", "");
}

std::string OpenAIClient::create_vector_store(const std::string& name) {
    std::string url = std::string(OPENAI_API_BASE) + "/vector_stores";
    json body = {{"name", name}};

    std::string response = http_post_json(url, body);
    json j = json::parse(response);

    if (j.contains("error")) {
        throw std::runtime_error("Vector store creation failed: " + j["error"]["message"].get<std::string>());
    }

    return j.value("id", "");
}

std::string OpenAIClient::create_file_batch(const std::string& vector_store_id,
                                             const std::vector<std::string>& file_ids) {
    std::string url = std::string(OPENAI_API_BASE) + "/vector_stores/" + vector_store_id + "/file_batches";
    json body = {{"file_ids", file_ids}};

    std::string response = http_post_json(url, body);
    json j = json::parse(response);

    if (j.contains("error")) {
        throw std::runtime_error("File batch creation failed: " + j["error"]["message"].get<std::string>());
    }

    return j.value("id", "");
}

std::string OpenAIClient::get_batch_status(const std::string& vector_store_id,
                                            const std::string& batch_id) {
    std::string url = std::string(OPENAI_API_BASE) + "/vector_stores/" + vector_store_id + "/file_batches/" + batch_id;
    std::string response = http_get(url);

    json j = json::parse(response);
    return j.value("status", "unknown");
}

std::string OpenAIClient::http_delete(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response;
    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + api_key_;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP DELETE failed: ") + curl_easy_strerror(res));
    }

    return response;
}

void OpenAIClient::add_file_to_vector_store(const std::string& vector_store_id,
                                             const std::string& file_id) {
    std::string url = std::string(OPENAI_API_BASE) + "/vector_stores/" + vector_store_id + "/files";
    json body = {{"file_id", file_id}};

    std::string response = http_post_json(url, body);
    json j = json::parse(response);

    if (j.contains("error")) {
        throw std::runtime_error("Failed to add file to vector store: " + j["error"]["message"].get<std::string>());
    }
}

void OpenAIClient::remove_file_from_vector_store(const std::string& vector_store_id,
                                                  const std::string& file_id) {
    std::string url = std::string(OPENAI_API_BASE) + "/vector_stores/" + vector_store_id + "/files/" + file_id;

    std::string response = http_delete(url);
    json j = json::parse(response);

    if (j.contains("error")) {
        throw std::runtime_error("Failed to remove file from vector store: " + j["error"]["message"].get<std::string>());
    }
}

void OpenAIClient::delete_file(const std::string& file_id) {
    std::string url = std::string(OPENAI_API_BASE) + "/files/" + file_id;

    std::string response = http_delete(url);
    json j = json::parse(response);

    if (j.contains("error")) {
        throw std::runtime_error("Failed to delete file: " + j["error"]["message"].get<std::string>());
    }
}

void OpenAIClient::stream_response(
    const std::string& model,
    const std::vector<Message>& conversation,
    const std::string& vector_store_id,
    const std::string& reasoning_effort,
    std::function<void(const std::string&)> on_text
) {
    std::string url = std::string(OPENAI_API_BASE) + "/responses";

    // Build conversation JSON.
    json input = json::array();
    for (const auto& msg : conversation) {
        input.push_back(msg.to_json());
    }

    // Build request body.
    json body = {
        {"model", model},
        {"input", input},
        {"stream", true},
        {"tools", json::array({
            {
                {"type", "file_search"},
                {"vector_store_ids", json::array({vector_store_id})}
            }
        })}
    };

    if (!reasoning_effort.empty()) {
        body["reasoning"] = {{"effort", reasoning_effort}};
    }

    // Stream the response.
    http_post_stream(url, body, [&](const std::string& data) {
        try {
            json event = json::parse(data);
            std::string event_type = event.value("type", "");

            if (event_type == "response.output_text.delta") {
                std::string delta = event.value("delta", "");
                if (!delta.empty() && on_text) {
                    on_text(delta);
                }
            }
        } catch (const json::exception&) {
            // Ignore malformed JSON in stream.
        }
    });
}

} // namespace rag
