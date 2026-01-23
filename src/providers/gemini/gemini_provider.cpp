#include "gemini_provider.hpp"
#include "../../verbose.hpp"
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <thread>
#include <chrono>

namespace rag::providers::gemini {

using json = nlohmann::json;

// CURL write callback for collecting response data into a string.
static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* response = static_cast<std::string*>(userdata);
    response->append(ptr, size * nmemb);
    return size * nmemb;
}

// Context for streaming SSE responses.
struct StreamContext {
    std::function<void(const std::string&)> on_data;
    std::string buffer;
    CancelCallback cancel_check;
    bool cancelled = false;
};

// CURL progress callback for cancellation support.
static int progress_callback(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<StreamContext*>(userdata);
    if (ctx->cancel_check && ctx->cancel_check()) {
        ctx->cancelled = true;
        return 1;
    }
    return 0;
}

// CURL write callback for streaming SSE data.
static size_t stream_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<StreamContext*>(userdata);
    size_t total_size = size * nmemb;

    ctx->buffer.append(ptr, total_size);

    // Process complete lines
    size_t pos;
    while ((pos = ctx->buffer.find("\n")) != std::string::npos) {
        std::string line = ctx->buffer.substr(0, pos);
        ctx->buffer.erase(0, pos + 1);

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Gemini SSE format: "data: {...json...}"
        if (line.substr(0, 6) == "data: ") {
            std::string data = line.substr(6);
            if (!data.empty() && data != "[DONE]") {
                ctx->on_data(data);
            }
        }
    }

    return total_size;
}

GeminiProvider::GeminiProvider(const std::string& api_key, const std::string& api_base_url)
    : api_key_(api_key)
    , api_base_(api_base_url.empty() ? GEMINI_API_BASE : api_base_url) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

GeminiProvider::~GeminiProvider() {
    curl_global_cleanup();
}

std::string GeminiProvider::build_url(const std::string& path) {
    // Gemini uses API key as query parameter
    std::string url = api_base_ + path;
    if (url.find('?') == std::string::npos) {
        url += "?key=" + api_key_;
    } else {
        url += "&key=" + api_key_;
    }
    return url;
}

std::string GeminiProvider::http_get(const std::string& url) {
    std::string full_url = build_url(url);
    rag::verbose_out("CURL", "GET " + full_url);

    CURL* curl = curl_easy_init();
    if (!curl) {
        rag::verbose_err("CURL", "Failed to initialize CURL");
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    if (rag::is_verbose()) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        rag::verbose_err("CURL", std::string("GET failed: ") + curl_easy_strerror(res));
        throw std::runtime_error(std::string("HTTP GET failed: ") + curl_easy_strerror(res));
    }

    rag::verbose_in("CURL", "HTTP " + std::to_string(http_code) + " - " + rag::truncate(response, 500));

    if (http_code >= 400) {
        try {
            json j = json::parse(response);
            if (j.contains("error") && j["error"].contains("message")) {
                throw std::runtime_error("Gemini API error: " + j["error"]["message"].get<std::string>());
            }
        } catch (const json::exception&) {}
        throw std::runtime_error("HTTP error " + std::to_string(http_code) + ": " + response.substr(0, 200));
    }

    return response;
}

std::string GeminiProvider::http_post_json(const std::string& url, const json& body) {
    std::string full_url = build_url(url);
    std::string body_str = body.dump();
    rag::verbose_out("CURL", "POST " + full_url);
    rag::verbose_out("CURL", "Body: " + rag::format_json_compact(body_str));

    CURL* curl = curl_easy_init();
    if (!curl) {
        rag::verbose_err("CURL", "Failed to initialize CURL");
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    if (rag::is_verbose()) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        rag::verbose_err("CURL", std::string("POST failed: ") + curl_easy_strerror(res));
        throw std::runtime_error(std::string("HTTP POST failed: ") + curl_easy_strerror(res));
    }

    rag::verbose_in("CURL", "HTTP " + std::to_string(http_code) + " - " + rag::truncate(response, 500));

    if (http_code >= 400) {
        try {
            json j = json::parse(response);
            if (j.contains("error") && j["error"].contains("message")) {
                throw std::runtime_error("Gemini API error: " + j["error"]["message"].get<std::string>());
            }
        } catch (const json::exception&) {}
        throw std::runtime_error("HTTP error " + std::to_string(http_code) + ": " + response.substr(0, 200));
    }

    return response;
}

std::string GeminiProvider::http_post_multipart(const std::string& url,
                                                 const std::string& filepath,
                                                 const json& metadata) {
    std::string full_url = build_url(url);
    rag::verbose_out("CURL", "POST (multipart) " + full_url);
    rag::verbose_out("CURL", "File: " + filepath);

    CURL* curl = curl_easy_init();
    if (!curl) {
        rag::verbose_err("CURL", "Failed to initialize CURL");
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response;

    curl_mime* mime = curl_mime_init(curl);

    // Add file part
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, filepath.c_str());

    // Add metadata if provided
    if (!metadata.empty()) {
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "config");
        std::string metadata_str = metadata.dump();
        curl_mime_data(part, metadata_str.c_str(), CURL_ZERO_TERMINATED);
        curl_mime_type(part, "application/json");
    }

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    if (rag::is_verbose()) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        rag::verbose_err("CURL", std::string("POST multipart failed: ") + curl_easy_strerror(res));
        throw std::runtime_error(std::string("HTTP POST multipart failed: ") + curl_easy_strerror(res));
    }

    rag::verbose_in("CURL", "HTTP " + std::to_string(http_code) + " - " + rag::truncate(response, 500));

    if (http_code >= 400) {
        try {
            json j = json::parse(response);
            if (j.contains("error") && j["error"].contains("message")) {
                throw std::runtime_error("Gemini API error: " + j["error"]["message"].get<std::string>());
            }
        } catch (const json::exception&) {}
        throw std::runtime_error("HTTP error " + std::to_string(http_code) + ": " + response.substr(0, 200));
    }

    return response;
}

bool GeminiProvider::http_post_stream(const std::string& url,
                                       const json& body,
                                       std::function<void(const std::string&)> on_data,
                                       CancelCallback cancel_check) {
    // Gemini streaming uses ?alt=sse
    std::string streaming_url = url;
    if (streaming_url.find('?') == std::string::npos) {
        streaming_url += "?alt=sse";
    } else {
        streaming_url += "&alt=sse";
    }
    std::string full_url = build_url(streaming_url);

    std::string body_str = body.dump();
    rag::verbose_out("CURL", "POST (stream) " + full_url);
    rag::verbose_out("CURL", "Body: " + rag::format_json_compact(body_str, 1000));

    CURL* curl = curl_easy_init();
    if (!curl) {
        rag::verbose_err("CURL", "Failed to initialize CURL");
        throw std::runtime_error("Failed to initialize CURL");
    }

    StreamContext ctx{on_data, "", cancel_check, false};

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    if (cancel_check) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    }

    if (rag::is_verbose()) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    rag::verbose_log("CURL", "Starting streaming request...");
    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (ctx.cancelled) {
        rag::verbose_log("CURL", "Stream cancelled by user");
        return false;
    }

    if (res != CURLE_OK) {
        if (res == CURLE_ABORTED_BY_CALLBACK) {
            rag::verbose_log("CURL", "Stream aborted by cancellation callback");
            return false;
        }
        rag::verbose_err("CURL", std::string("Streaming POST failed: ") + curl_easy_strerror(res));
        throw std::runtime_error(std::string("HTTP streaming POST failed: ") + curl_easy_strerror(res));
    }

    rag::verbose_in("CURL", "Stream complete, HTTP " + std::to_string(http_code));
    return true;
}

std::string GeminiProvider::http_delete(const std::string& url) {
    std::string full_url = build_url(url);
    rag::verbose_out("CURL", "DELETE " + full_url);

    CURL* curl = curl_easy_init();
    if (!curl) {
        rag::verbose_err("CURL", "Failed to initialize CURL");
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    if (rag::is_verbose()) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        rag::verbose_err("CURL", std::string("DELETE failed: ") + curl_easy_strerror(res));
        throw std::runtime_error(std::string("HTTP DELETE failed: ") + curl_easy_strerror(res));
    }

    rag::verbose_in("CURL", "HTTP " + std::to_string(http_code) + " - " + rag::truncate(response, 500));

    if (http_code >= 400 && http_code != 404) {
        try {
            json j = json::parse(response);
            if (j.contains("error") && j["error"].contains("message")) {
                throw std::runtime_error("Gemini API error: " + j["error"]["message"].get<std::string>());
            }
        } catch (const json::exception&) {}
        throw std::runtime_error("HTTP error " + std::to_string(http_code) + ": " + response.substr(0, 200));
    }

    return response;
}

std::string GeminiProvider::extract_model_name(const std::string& full_name) {
    // "models/gemini-3-flash" -> "gemini-3-flash"
    size_t pos = full_name.find('/');
    if (pos != std::string::npos) {
        return full_name.substr(pos + 1);
    }
    return full_name;
}

nlohmann::json GeminiProvider::messages_to_contents(const std::vector<Message>& messages) {
    json contents = json::array();

    for (const auto& msg : messages) {
        // Map our roles to Gemini roles
        std::string role = msg.role;
        if (role == "assistant") {
            role = "model";
        }
        // "user" stays as "user", "system" is handled separately

        // Gemini doesn't have a "system" role in contents - it uses systemInstruction
        // For now, we'll treat system messages as user messages with a prefix
        if (msg.role == "system") {
            // Skip system messages here - they should be handled via systemInstruction
            continue;
        }

        contents.push_back({
            {"role", role},
            {"parts", json::array({{{"text", msg.content}}})}
        });
    }

    return contents;
}

void GeminiProvider::wait_for_operation(const std::string& operation_name) {
    // Poll the operation until it's done
    const int max_attempts = 120;  // 10 minutes max
    const int poll_interval_ms = 5000;  // 5 seconds

    for (int i = 0; i < max_attempts; ++i) {
        std::string response = http_get("/" + operation_name);
        json j = json::parse(response);

        if (j.value("done", false)) {
            if (j.contains("error")) {
                throw std::runtime_error("Operation failed: " + j["error"].value("message", "Unknown error"));
            }
            return;
        }

        rag::verbose_log("GEMINI", "Operation " + operation_name + " still in progress, waiting...");
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    }

    throw std::runtime_error("Operation timed out: " + operation_name);
}

// ========== IModelsService ==========

std::vector<ModelInfo> GeminiProvider::list_models() {
    std::string response = http_get("/models");
    json j = json::parse(response);

    std::vector<ModelInfo> models;

    if (j.contains("models") && j["models"].is_array()) {
        for (const auto& model : j["models"]) {
            // Check if model supports generateContent
            bool supports_generate = false;
            if (model.contains("supportedGenerationMethods") && model["supportedGenerationMethods"].is_array()) {
                for (const auto& method : model["supportedGenerationMethods"]) {
                    if (method == "generateContent") {
                        supports_generate = true;
                        break;
                    }
                }
            }

            if (!supports_generate) {
                continue;
            }

            std::string full_name = model.value("name", "");
            std::string short_name = extract_model_name(full_name);

            // Filter to gemini models only (skip embedding models, etc.)
            if (short_name.find("gemini") == std::string::npos) {
                continue;
            }

            ModelInfo info;
            info.id = short_name;
            info.display_name = model.value("displayName", short_name);
            info.max_context_tokens = model.value("inputTokenLimit", 128000);
            info.supports_tools = true;  // Most Gemini models support function calling
            info.supports_reasoning = model.value("thinking", false);

            models.push_back(info);
        }
    }

    // Sort by name
    std::sort(models.begin(), models.end(), [](const ModelInfo& a, const ModelInfo& b) {
        return a.id < b.id;
    });

    return models;
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

std::string GeminiProvider::upload_file(const std::string& filepath) {
    // For Gemini, files are uploaded to the general Files API
    // They can then be imported into a File Search Store
    std::string response = http_post_multipart("/files", filepath, {
        {"display_name", std::filesystem::path(filepath).filename().string()}
    });

    json j = json::parse(response);

    // The response contains an operation for async processing
    if (j.contains("name")) {
        // This is the file resource name
        return j["name"].get<std::string>();
    }

    throw std::runtime_error("File upload failed: no file name in response");
}

std::vector<UploadResult> GeminiProvider::upload_files_parallel(
    const std::vector<std::string>& filepaths,
    ProgressCallback on_progress,
    size_t /*max_parallel*/
) {
    // For simplicity, upload sequentially for now
    // TODO: Implement parallel upload with curl multi
    std::vector<UploadResult> results;
    results.reserve(filepaths.size());

    size_t completed = 0;
    for (const auto& filepath : filepaths) {
        UploadResult result;
        result.filepath = filepath;

        try {
            result.file_id = upload_file(filepath);
        } catch (const std::exception& e) {
            result.error = e.what();
        }

        results.push_back(result);
        completed++;

        if (on_progress) {
            on_progress(completed, filepaths.size());
        }
    }

    return results;
}

void GeminiProvider::delete_file(const std::string& file_id) {
    http_delete("/" + file_id);
}

std::vector<DeleteResult> GeminiProvider::delete_files_parallel(
    const std::vector<std::string>& file_ids,
    const std::string& /*store_id*/,
    ProgressCallback on_progress,
    size_t /*max_parallel*/
) {
    // For simplicity, delete sequentially for now
    std::vector<DeleteResult> results;
    results.reserve(file_ids.size());

    size_t completed = 0;
    for (const auto& file_id : file_ids) {
        DeleteResult result;
        result.file_id = file_id;

        try {
            delete_file(file_id);
        } catch (const std::exception& e) {
            result.error = e.what();
        }

        results.push_back(result);
        completed++;

        if (on_progress) {
            on_progress(completed, file_ids.size());
        }
    }

    return results;
}

// ========== IKnowledgeStore ==========

std::string GeminiProvider::create_store(const std::string& name) {
    json body = {
        {"config", {
            {"display_name", name}
        }}
    };

    std::string response = http_post_json("/fileSearchStores", body);
    json j = json::parse(response);

    if (j.contains("name")) {
        return j["name"].get<std::string>();
    }

    throw std::runtime_error("Failed to create File Search Store: no name in response");
}

void GeminiProvider::delete_store(const std::string& store_id) {
    http_delete("/" + store_id);
}

std::string GeminiProvider::add_files(const std::string& store_id, const std::vector<std::string>& file_ids) {
    // Import files into the store one by one
    // The File Search API requires importing each file separately
    for (const auto& file_id : file_ids) {
        add_file(store_id, file_id);
    }

    // Return a pseudo operation ID - not used for Gemini
    return "batch_complete";
}

void GeminiProvider::add_file(const std::string& store_id, const std::string& file_id) {
    json body = {
        {"file_name", file_id}
    };

    std::string response = http_post_json("/" + store_id + "/documents:importFile", body);
    json j = json::parse(response);

    // This returns an operation - we need to wait for it
    if (j.contains("name")) {
        wait_for_operation(j["name"].get<std::string>());
    }
}

void GeminiProvider::remove_file(const std::string& store_id, const std::string& file_id) {
    // Delete the document from the store
    // The file_id here is actually the document name within the store
    http_delete("/" + store_id + "/documents/" + file_id);
}

std::string GeminiProvider::get_operation_status(const std::string& /*store_id*/, const std::string& operation_id) {
    std::string response = http_get("/" + operation_id);
    json j = json::parse(response);

    if (j.value("done", false)) {
        if (j.contains("error")) {
            return "failed";
        }
        return "completed";
    }

    return "in_progress";
}

// ========== IChatService ==========

StreamResult GeminiProvider::stream_response(
    const ChatConfig& config,
    const std::vector<Message>& conversation,
    OnTextCallback on_text,
    CancelCallback cancel_check
) {
    // Build the model endpoint
    std::string model_name = config.model;
    if (model_name.find("models/") != 0) {
        model_name = "models/" + model_name;
    }

    // Convert messages to Gemini format
    json contents = messages_to_contents(conversation);

    // Build request body
    json body = {
        {"contents", contents}
    };

    // Add system instruction if present
    for (const auto& msg : conversation) {
        if (msg.role == "system") {
            body["systemInstruction"] = {
                {"parts", json::array({{{"text", msg.content}}})}
            };
            break;
        }
    }

    // Add file search tool if knowledge store is configured
    if (!config.knowledge_store_id.empty()) {
        body["tools"] = json::array({
            {{"file_search", {
                {"file_search_store_names", json::array({config.knowledge_store_id})}
            }}}
        });
    }

    // Add generation config
    json gen_config;
    gen_config["temperature"] = 1.0;
    body["generationConfig"] = gen_config;

    // Stream the response
    std::string response_id;
    ResponseUsage usage;
    std::string stream_error;

    bool completed = http_post_stream("/" + model_name + ":streamGenerateContent", body,
        [&](const std::string& data) {
            try {
                json event = json::parse(data);

                // Extract text from candidates
                if (event.contains("candidates") && event["candidates"].is_array() && !event["candidates"].empty()) {
                    const auto& candidate = event["candidates"][0];
                    if (candidate.contains("content") && candidate["content"].contains("parts")) {
                        for (const auto& part : candidate["content"]["parts"]) {
                            if (part.contains("text") && on_text) {
                                on_text(part["text"].get<std::string>());
                            }
                        }
                    }
                }

                // Extract usage metadata
                if (event.contains("usageMetadata")) {
                    const auto& u = event["usageMetadata"];
                    usage.input_tokens = u.value("promptTokenCount", 0);
                    usage.output_tokens = u.value("candidatesTokenCount", 0);
                    // Gemini doesn't have separate reasoning tokens in the same way
                }

                // Check for errors
                if (event.contains("error")) {
                    stream_error = event["error"].value("message", "Unknown error");
                }

            } catch (const json::exception&) {
                // Ignore malformed JSON in stream
            }
        }, cancel_check);

    if (!completed) {
        return StreamResult{"", {}, true};
    }

    if (!stream_error.empty()) {
        throw std::runtime_error(stream_error);
    }

    // Gemini doesn't return a response ID for continuation in the same way
    return StreamResult{response_id, usage, false};
}

StreamResult GeminiProvider::stream_response(
    const ChatConfig& config,
    const nlohmann::json& input,
    OnTextCallback on_text,
    CancelCallback cancel_check
) {
    // If input is already in Gemini format, use it directly
    // Otherwise, try to convert from Message format
    std::vector<Message> messages;

    if (input.is_array()) {
        for (const auto& item : input) {
            if (item.contains("role") && item.contains("content")) {
                Message msg;
                msg.role = item["role"].get<std::string>();
                msg.content = item["content"].get<std::string>();
                messages.push_back(msg);
            } else if (item.contains("role") && item.contains("parts")) {
                // Already in Gemini format - extract text
                Message msg;
                std::string role = item["role"].get<std::string>();
                msg.role = (role == "model") ? "assistant" : role;
                if (item["parts"].is_array() && !item["parts"].empty()) {
                    msg.content = item["parts"][0].value("text", "");
                }
                messages.push_back(msg);
            }
        }
    }

    return stream_response(config, messages, on_text, cancel_check);
}

StreamResult GeminiProvider::stream_response_with_tools(
    const ChatConfig& config,
    const std::vector<Message>& conversation,
    OnTextCallback on_text,
    OnToolCallCallback on_tool_call,
    CancelCallback cancel_check
) {
    // Build the model endpoint
    std::string model_name = config.model;
    if (model_name.find("models/") != 0) {
        model_name = "models/" + model_name;
    }

    // Convert messages to Gemini format
    json contents = messages_to_contents(conversation);

    // Build tools array
    json tools = json::array();

    // Add file search tool if knowledge store is configured
    if (!config.knowledge_store_id.empty()) {
        tools.push_back({
            {"file_search", {
                {"file_search_store_names", json::array({config.knowledge_store_id})}
            }}
        });
    }

    // Add function declarations from additional_tools
    if (!config.additional_tools.empty() && config.additional_tools.is_array()) {
        json function_decls = json::array();
        for (const auto& tool : config.additional_tools) {
            if (tool.contains("function")) {
                function_decls.push_back(tool["function"]);
            } else if (tool.contains("name")) {
                // Tool is already a function declaration
                function_decls.push_back(tool);
            }
        }
        if (!function_decls.empty()) {
            tools.push_back({{"functionDeclarations", function_decls}});
        }
    }

    ResponseUsage total_usage;
    json current_contents = contents;

    while (true) {
        // Build request body
        json body = {
            {"contents", current_contents}
        };

        // Add system instruction if present
        for (const auto& msg : conversation) {
            if (msg.role == "system") {
                body["systemInstruction"] = {
                    {"parts", json::array({{{"text", msg.content}}})}
                };
                break;
            }
        }

        if (!tools.empty()) {
            body["tools"] = tools;
        }

        // Add generation config
        body["generationConfig"] = {{"temperature", 1.0}};

        ResponseUsage call_usage;
        std::string stream_error;
        std::vector<std::tuple<std::string, std::string, json>> pending_function_calls;

        bool completed = http_post_stream("/" + model_name + ":streamGenerateContent", body,
            [&](const std::string& data) {
                try {
                    json event = json::parse(data);

                    // Extract content from candidates
                    if (event.contains("candidates") && event["candidates"].is_array() && !event["candidates"].empty()) {
                        const auto& candidate = event["candidates"][0];
                        if (candidate.contains("content") && candidate["content"].contains("parts")) {
                            for (const auto& part : candidate["content"]["parts"]) {
                                if (part.contains("text") && on_text) {
                                    on_text(part["text"].get<std::string>());
                                }
                                if (part.contains("functionCall")) {
                                    std::string name = part["functionCall"].value("name", "");
                                    json args = part["functionCall"].value("args", json::object());
                                    // Generate a call ID (Gemini doesn't provide one)
                                    std::string call_id = name + "_" + std::to_string(pending_function_calls.size());
                                    pending_function_calls.push_back({call_id, name, args});
                                    rag::verbose_log("GEMINI", "Function call: " + name);
                                }
                            }
                        }
                    }

                    // Extract usage metadata
                    if (event.contains("usageMetadata")) {
                        const auto& u = event["usageMetadata"];
                        call_usage.input_tokens = u.value("promptTokenCount", 0);
                        call_usage.output_tokens = u.value("candidatesTokenCount", 0);
                    }

                    // Check for errors
                    if (event.contains("error")) {
                        stream_error = event["error"].value("message", "Unknown error");
                    }

                } catch (const json::exception&) {
                    // Ignore malformed JSON in stream
                }
            }, cancel_check);

        total_usage.input_tokens = call_usage.input_tokens;
        total_usage.output_tokens += call_usage.output_tokens;

        if (!completed) {
            return StreamResult{"", {}, true};
        }

        if (!stream_error.empty()) {
            throw std::runtime_error(stream_error);
        }

        // If no function calls, we're done
        if (pending_function_calls.empty()) {
            return StreamResult{"", total_usage, false};
        }

        // Execute function calls and add results
        // First, add the model's response to contents
        json model_parts = json::array();
        for (const auto& [call_id, name, args] : pending_function_calls) {
            model_parts.push_back({
                {"functionCall", {
                    {"name", name},
                    {"args", args}
                }}
            });
        }
        current_contents.push_back({
            {"role", "model"},
            {"parts", model_parts}
        });

        // Now add function responses
        json response_parts = json::array();
        for (const auto& [call_id, name, args] : pending_function_calls) {
            std::string result = on_tool_call(call_id, name, args);
            response_parts.push_back({
                {"functionResponse", {
                    {"name", name},
                    {"response", {{"result", result}}}
                }}
            });
            rag::verbose_log("GEMINI", "Function " + name + " returned: " + result.substr(0, 200));
        }
        current_contents.push_back({
            {"role", "user"},
            {"parts", response_parts}
        });

        rag::verbose_log("GEMINI", "Submitting function results and continuing...");
    }
}

StreamResult GeminiProvider::stream_response_with_tools(
    const ChatConfig& config,
    const nlohmann::json& input,
    OnTextCallback on_text,
    OnToolCallCallback on_tool_call,
    CancelCallback cancel_check
) {
    // Convert JSON input to messages
    std::vector<Message> messages;

    if (input.is_array()) {
        for (const auto& item : input) {
            if (item.contains("role") && item.contains("content")) {
                Message msg;
                msg.role = item["role"].get<std::string>();
                msg.content = item["content"].get<std::string>();
                messages.push_back(msg);
            } else if (item.contains("role") && item.contains("parts")) {
                Message msg;
                std::string role = item["role"].get<std::string>();
                msg.role = (role == "model") ? "assistant" : role;
                if (item["parts"].is_array() && !item["parts"].empty()) {
                    msg.content = item["parts"][0].value("text", "");
                }
                messages.push_back(msg);
            }
        }
    }

    return stream_response_with_tools(config, messages, on_text, on_tool_call, cancel_check);
}

std::optional<nlohmann::json> GeminiProvider::compact_window(
    const std::string& /*model*/,
    const std::string& /*previous_response_id*/
) {
    // Gemini doesn't support conversation compaction
    return std::nullopt;
}

} // namespace rag::providers::gemini
