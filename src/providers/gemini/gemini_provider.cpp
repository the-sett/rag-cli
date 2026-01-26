#include "gemini_provider.hpp"
#include "../../verbose.hpp"
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <fstream>
#include <cstring>
#include <map>

namespace rag::providers::gemini {

// Get MIME type from file extension
static std::string get_mime_type(const std::string& filepath) {
    std::string ext = std::filesystem::path(filepath).extension().string();
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Common MIME types for supported file formats
    static const std::map<std::string, std::string> mime_types = {
        {".txt", "text/plain"},
        {".md", "text/markdown"},
        {".markdown", "text/markdown"},
        {".pdf", "application/pdf"},
        {".json", "application/json"},
        {".yaml", "application/x-yaml"},
        {".yml", "application/x-yaml"},
        {".xml", "application/xml"},
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "text/javascript"},
        {".ts", "text/typescript"},
        {".py", "text/x-python"},
        {".c", "text/x-c"},
        {".cpp", "text/x-c++"},
        {".h", "text/x-c"},
        {".hpp", "text/x-c++"},
        {".java", "text/x-java"},
        {".go", "text/x-go"},
        {".rs", "text/x-rust"},
        {".rb", "text/x-ruby"},
        {".php", "text/x-php"},
        {".sh", "text/x-shellscript"},
        {".bash", "text/x-shellscript"},
        {".sql", "text/x-sql"},
        {".csv", "text/csv"},
    };

    auto it = mime_types.find(ext);
    if (it != mime_types.end()) {
        return it->second;
    }
    return "application/octet-stream";
}

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

std::string GeminiProvider::build_upload_url(const std::string& path) {
    // Use upload base URL instead of regular API base
    std::string url = std::string(GEMINI_UPLOAD_BASE) + path;
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

// Callback struct for capturing response headers
struct HeaderContext {
    std::string upload_url;
};

static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total_size = size * nitems;
    auto* ctx = static_cast<HeaderContext*>(userdata);

    std::string header(buffer, total_size);

    // Look for X-Goog-Upload-URL header (case-insensitive)
    const char* prefix = "x-goog-upload-url:";
    size_t prefix_len = strlen(prefix);

    // Convert header to lowercase for comparison
    std::string header_lower = header;
    std::transform(header_lower.begin(), header_lower.end(), header_lower.begin(), ::tolower);

    if (header_lower.substr(0, prefix_len) == prefix) {
        // Extract the URL value
        std::string value = header.substr(prefix_len);
        // Trim whitespace
        size_t start = value.find_first_not_of(" \t\r\n");
        size_t end = value.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos) {
            ctx->upload_url = value.substr(start, end - start + 1);
        }
    }

    return total_size;
}

std::string GeminiProvider::upload_file_resumable(const std::string& filepath) {
    namespace fs = std::filesystem;

    // Get file info
    std::string filename = fs::path(filepath).filename().string();
    std::string mime_type = get_mime_type(filepath);
    uintmax_t file_size = fs::file_size(filepath);

    rag::verbose_log("GEMINI", "Uploading file: " + filename + " (" + std::to_string(file_size) + " bytes, " + mime_type + ")");

    // Step 1: Initiate resumable upload
    std::string init_url = build_upload_url("/files");
    rag::verbose_out("CURL", "POST (resumable init) " + init_url);

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    // Build metadata JSON
    json metadata = {
        {"file", {
            {"display_name", filename}
        }}
    };
    std::string metadata_str = metadata.dump();

    rag::verbose_out("CURL", "Metadata: " + metadata_str);

    // Set up headers for resumable upload initiation
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "X-Goog-Upload-Protocol: resumable");
    headers = curl_slist_append(headers, "X-Goog-Upload-Command: start");
    headers = curl_slist_append(headers, ("X-Goog-Upload-Header-Content-Length: " + std::to_string(file_size)).c_str());
    headers = curl_slist_append(headers, ("X-Goog-Upload-Header-Content-Type: " + mime_type).c_str());

    std::string response;
    HeaderContext header_ctx;

    curl_easy_setopt(curl, CURLOPT_URL, init_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, metadata_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_ctx);

    if (rag::is_verbose()) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("Upload init failed: ") + curl_easy_strerror(res));
    }

    rag::verbose_in("CURL", "HTTP " + std::to_string(http_code) + " - Upload URL: " + header_ctx.upload_url);

    if (http_code >= 400) {
        try {
            json j = json::parse(response);
            if (j.contains("error") && j["error"].contains("message")) {
                throw std::runtime_error("Gemini API error: " + j["error"]["message"].get<std::string>());
            }
        } catch (const json::exception&) {}
        throw std::runtime_error("HTTP error " + std::to_string(http_code) + ": " + response.substr(0, 500));
    }

    if (header_ctx.upload_url.empty()) {
        throw std::runtime_error("No upload URL received from Gemini API");
    }

    // Step 2: Upload the file data
    rag::verbose_out("CURL", "POST (file data) " + header_ctx.upload_url);

    // Read file content
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }

    std::vector<char> file_data((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
    file.close();

    curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    headers = nullptr;
    headers = curl_slist_append(headers, ("Content-Type: " + mime_type).c_str());
    headers = curl_slist_append(headers, "X-Goog-Upload-Command: upload, finalize");
    headers = curl_slist_append(headers, "X-Goog-Upload-Offset: 0");

    response.clear();

    curl_easy_setopt(curl, CURLOPT_URL, header_ctx.upload_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, file_data.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, file_data.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    if (rag::is_verbose()) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    res = curl_easy_perform(curl);

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("File data upload failed: ") + curl_easy_strerror(res));
    }

    rag::verbose_in("CURL", "HTTP " + std::to_string(http_code) + " - " + rag::truncate(response, 500));

    if (http_code >= 400) {
        try {
            json j = json::parse(response);
            if (j.contains("error") && j["error"].contains("message")) {
                throw std::runtime_error("Gemini API error: " + j["error"]["message"].get<std::string>());
            }
        } catch (const json::exception&) {}
        throw std::runtime_error("HTTP error " + std::to_string(http_code) + ": " + response.substr(0, 500));
    }

    // Parse response to get file name
    json j = json::parse(response);

    // Response format: {"file": {"name": "files/abc123", ...}}
    if (j.contains("file") && j["file"].contains("name")) {
        std::string file_name = j["file"]["name"].get<std::string>();
        rag::verbose_log("GEMINI", "File uploaded: " + file_name);
        return file_name;
    }

    throw std::runtime_error("File upload failed: no file name in response. Response: " + response.substr(0, 500));
}

std::string GeminiProvider::upload_file(const std::string& filepath) {
    return upload_file_resumable(filepath);
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
        {"displayName", name}
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
        {"fileName", file_id}
    };

    std::string response = http_post_json("/" + store_id + ":importFile", body);
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
    // Special case: add_files() already waits for all operations to complete,
    // so when it returns "batch_complete", there's nothing more to poll
    if (operation_id == "batch_complete") {
        return "completed";
    }

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
            {{"fileSearch", {
                {"fileSearchStoreNames", json::array({config.knowledge_store_id})}
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
            {"fileSearch", {
                {"fileSearchStoreNames", json::array({config.knowledge_store_id})}
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
