#include "openai_client.hpp"
#include "config.hpp"
#include "verbose.hpp"
#include <curl/curl.h>
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <map>
#include <queue>
#include <memory>

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
    CancelCallback cancel_check;                       // Optional cancellation callback.
    bool cancelled = false;                            // Set when cancelled.
};

// CURL progress callback for cancellation support.
// Returns non-zero to abort the transfer.
static int progress_callback(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<StreamContext*>(userdata);
    if (ctx->cancel_check && ctx->cancel_check()) {
        ctx->cancelled = true;
        return 1;  // Non-zero aborts the transfer
    }
    return 0;
}

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
    verbose_out("CURL", "GET " + url);

    CURL* curl = curl_easy_init();
    if (!curl) {
        verbose_err("CURL", "Failed to initialize CURL");
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

    // Enable verbose CURL output in verbose mode
    if (is_verbose()) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        verbose_err("CURL", std::string("GET failed: ") + curl_easy_strerror(res));
        throw std::runtime_error(std::string("HTTP GET failed: ") + curl_easy_strerror(res));
    }

    verbose_in("CURL", "HTTP " + std::to_string(http_code) + " - " + truncate(response, 500));
    return response;
}

std::string OpenAIClient::http_post_json(const std::string& url, const json& body) {
    std::string body_str = body.dump();
    verbose_out("CURL", "POST " + url);
    verbose_out("CURL", "Body: " + format_json_compact(body_str));

    CURL* curl = curl_easy_init();
    if (!curl) {
        verbose_err("CURL", "Failed to initialize CURL");
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response;

    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + api_key_;
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    if (is_verbose()) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        verbose_err("CURL", std::string("POST failed: ") + curl_easy_strerror(res));
        throw std::runtime_error(std::string("HTTP POST failed: ") + curl_easy_strerror(res));
    }

    verbose_in("CURL", "HTTP " + std::to_string(http_code) + " - " + truncate(response, 500));
    return response;
}

std::string OpenAIClient::http_post_multipart(const std::string& url,
                                               const std::string& filepath,
                                               const std::string& purpose,
                                               const std::string& display_filename) {
    verbose_out("CURL", "POST (multipart) " + url);
    verbose_out("CURL", "File: " + filepath + " purpose: " + purpose);

    CURL* curl = curl_easy_init();
    if (!curl) {
        verbose_err("CURL", "Failed to initialize CURL");
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

    // Override the filename if specified.
    if (!display_filename.empty()) {
        curl_mime_filename(part, display_filename.c_str());
    }

    // Add purpose part.
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "purpose");
    curl_mime_data(part, purpose.c_str(), CURL_ZERO_TERMINATED);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    if (is_verbose()) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_mime_free(mime);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        verbose_err("CURL", std::string("POST multipart failed: ") + curl_easy_strerror(res));
        throw std::runtime_error(std::string("HTTP POST multipart failed: ") + curl_easy_strerror(res));
    }

    verbose_in("CURL", "HTTP " + std::to_string(http_code) + " - " + truncate(response, 500));
    return response;
}

bool OpenAIClient::http_post_stream(const std::string& url,
                                     const json& body,
                                     std::function<void(const std::string&)> on_data,
                                     CancelCallback cancel_check) {
    std::string body_str = body.dump();
    verbose_out("CURL", "POST (stream) " + url);
    verbose_out("CURL", "Body: " + format_json_compact(body_str, 1000));

    CURL* curl = curl_easy_init();
    if (!curl) {
        verbose_err("CURL", "Failed to initialize CURL");
        throw std::runtime_error("Failed to initialize CURL");
    }

    StreamContext ctx{on_data, "", cancel_check, false};

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

    // Set up progress callback for cancellation support
    if (cancel_check) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    }

    if (is_verbose()) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    verbose_log("CURL", "Starting streaming request...");
    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // Check if we were cancelled
    if (ctx.cancelled) {
        verbose_log("CURL", "Stream cancelled by user");
        return false;
    }

    if (res != CURLE_OK) {
        // CURLE_ABORTED_BY_CALLBACK is expected when we cancel
        if (res == CURLE_ABORTED_BY_CALLBACK) {
            verbose_log("CURL", "Stream aborted by cancellation callback");
            return false;
        }
        verbose_err("CURL", std::string("Streaming POST failed: ") + curl_easy_strerror(res));
        throw std::runtime_error(std::string("HTTP streaming POST failed: ") + curl_easy_strerror(res));
    }

    verbose_in("CURL", "Stream complete, HTTP " + std::to_string(http_code));
    return true;
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
        std::string error_msg = j["error"]["message"].get<std::string>();

        // Check if it's an extension error - retry with .txt appended to filename
        if (error_msg.find("Invalid extension") != std::string::npos) {
            std::string filename = std::filesystem::path(filepath).filename().string();
            std::string txt_filename = filename + ".txt";

            response = http_post_multipart(url, filepath, "assistants", txt_filename);
            j = json::parse(response);

            if (j.contains("error")) {
                throw std::runtime_error("File upload failed: " + j["error"]["message"].get<std::string>());
            }

            return j.value("id", "");
        }

        throw std::runtime_error("File upload failed: " + error_msg);
    }

    return j.value("id", "");
}

// Context for tracking each upload in the multi handle
struct MultiUploadContext {
    std::string filepath;
    std::string display_filename;  // For retry with .txt extension
    std::string response;
    CURL* easy;
    curl_mime* mime;
    curl_slist* headers;
    int retry_count;
    long http_code;
};

// Item in the upload queue
struct UploadQueueItem {
    size_t index;
    int retry_count;
    std::string display_filename;  // Non-empty if retrying with .txt extension
};

std::vector<UploadResult> OpenAIClient::upload_files_parallel(
    const std::vector<std::string>& filepaths,
    std::function<void(size_t completed, size_t total)> on_progress,
    size_t max_parallel
) {
    if (filepaths.empty()) {
        return {};
    }

    const int MAX_RETRIES = 5;
    std::string url = std::string(OPENAI_API_BASE) + "/files";
    std::vector<UploadResult> results;
    results.reserve(filepaths.size());

    // Initialize results with empty entries
    for (const auto& fp : filepaths) {
        results.push_back({fp, "", ""});
    }

    // Create multi handle
    CURLM* multi = curl_multi_init();
    if (!multi) {
        throw std::runtime_error("Failed to initialize CURL multi handle");
    }

    // Track active uploads: maps easy handle to (result_index, context)
    std::map<CURL*, std::pair<size_t, std::unique_ptr<MultiUploadContext>>> active;

    // Queue of files to upload (with retry counts)
    std::queue<UploadQueueItem> pending;
    for (size_t i = 0; i < filepaths.size(); ++i) {
        pending.push({i, 0, ""});
    }

    size_t completed = 0;
    size_t current_max_parallel = max_parallel;

    // Lambda to start a new upload
    auto start_upload = [&](const UploadQueueItem& item) {
        const std::string& filepath = filepaths[item.index];

        auto ctx = std::make_unique<MultiUploadContext>();
        ctx->filepath = filepath;
        ctx->display_filename = item.display_filename;
        ctx->retry_count = item.retry_count;
        ctx->http_code = 0;

        CURL* easy = curl_easy_init();
        if (!easy) {
            results[item.index].error = "Failed to initialize CURL";
            completed++;
            return;
        }

        ctx->easy = easy;
        ctx->headers = nullptr;
        std::string auth_header = "Authorization: Bearer " + api_key_;
        ctx->headers = curl_slist_append(ctx->headers, auth_header.c_str());

        ctx->mime = curl_mime_init(easy);

        // Add file part
        curl_mimepart* part = curl_mime_addpart(ctx->mime);
        curl_mime_name(part, "file");
        curl_mime_filedata(part, filepath.c_str());

        // Override filename if retrying with .txt extension
        if (!item.display_filename.empty()) {
            curl_mime_filename(part, item.display_filename.c_str());
        }

        // Add purpose part
        part = curl_mime_addpart(ctx->mime);
        curl_mime_name(part, "purpose");
        curl_mime_data(part, "assistants", CURL_ZERO_TERMINATED);

        curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, ctx->headers);
        curl_easy_setopt(easy, CURLOPT_MIMEPOST, ctx->mime);
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, &ctx->response);

        if (is_verbose()) {
            curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
        }

        curl_multi_add_handle(multi, easy);
        active[easy] = {item.index, std::move(ctx)};

        verbose_log("CURL", "Started upload (attempt " + std::to_string(item.retry_count + 1) + "): " + filepath);
    };

    // Start initial batch of uploads
    while (!pending.empty() && active.size() < current_max_parallel) {
        auto item = pending.front();
        pending.pop();
        start_upload(item);
    }

    // Main event loop
    int still_running = 1;
    while (still_running > 0 || !pending.empty()) {
        // Perform transfers
        CURLMcode mc = curl_multi_perform(multi, &still_running);
        if (mc != CURLM_OK) {
            verbose_err("CURL", std::string("curl_multi_perform failed: ") + curl_multi_strerror(mc));
            break;
        }

        // Check for completed transfers
        int msgs_left;
        CURLMsg* msg;
        while ((msg = curl_multi_info_read(multi, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                CURL* easy = msg->easy_handle;
                auto it = active.find(easy);
                if (it == active.end()) continue;

                size_t idx = it->second.first;
                auto& ctx = it->second.second;

                // Remove from multi handle
                curl_multi_remove_handle(multi, easy);

                // Get HTTP status code
                curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &ctx->http_code);

                // Determine outcome
                bool should_retry = false;
                bool use_txt_extension = false;
                std::string error_msg;

                CURLcode res = msg->data.result;
                if (res != CURLE_OK) {
                    error_msg = curl_easy_strerror(res);
                    should_retry = true;  // Network errors are retryable
                } else if (ctx->response.empty()) {
                    error_msg = "Empty response (HTTP " + std::to_string(ctx->http_code) + ")";
                    should_retry = (ctx->http_code >= 500 || ctx->http_code == 429);
                } else if (ctx->http_code == 429) {
                    // Rate limited - reduce parallelism
                    error_msg = "Rate limited (HTTP 429)";
                    should_retry = true;
                    if (current_max_parallel > 1) {
                        current_max_parallel = current_max_parallel / 2;
                        verbose_log("CURL", "Rate limited, reducing parallelism to " + std::to_string(current_max_parallel));
                    }
                } else if (ctx->http_code >= 500) {
                    // Server error - retryable
                    error_msg = "Server error (HTTP " + std::to_string(ctx->http_code) + ")";
                    should_retry = true;
                } else if (ctx->http_code >= 400) {
                    // Client error - check if it's a retryable extension error
                    try {
                        json j = json::parse(ctx->response);
                        if (j.contains("error") && j["error"].contains("message")) {
                            error_msg = j["error"]["message"].get<std::string>();
                            if (error_msg.find("Invalid extension") != std::string::npos &&
                                ctx->display_filename.empty()) {
                                use_txt_extension = true;
                            }
                        } else {
                            error_msg = "HTTP " + std::to_string(ctx->http_code);
                        }
                    } catch (...) {
                        error_msg = "HTTP " + std::to_string(ctx->http_code) + ": " + ctx->response.substr(0, 200);
                    }
                } else {
                    // Success case
                    try {
                        json j = json::parse(ctx->response);
                        if (j.contains("error")) {
                            error_msg = j["error"]["message"].get<std::string>();
                            if (error_msg.find("Invalid extension") != std::string::npos &&
                                ctx->display_filename.empty()) {
                                use_txt_extension = true;
                            }
                        } else {
                            results[idx].file_id = j.value("id", "");
                            if (results[idx].file_id.empty()) {
                                error_msg = "No file ID in response";
                            }
                        }
                    } catch (const json::exception&) {
                        error_msg = "JSON parse error: " + ctx->response.substr(0, 200);
                        should_retry = true;
                    }
                }

                int retry_count = ctx->retry_count;
                std::string filepath = ctx->filepath;
                std::string display_filename = ctx->display_filename;

                // Cleanup this context
                curl_mime_free(ctx->mime);
                curl_slist_free_all(ctx->headers);
                curl_easy_cleanup(easy);
                active.erase(it);

                // Decide what to do next
                if (results[idx].success()) {
                    // Success!
                    completed++;
                    if (on_progress) {
                        on_progress(completed, filepaths.size());
                    }
                    verbose_log("CURL", "Uploaded: " + filepath + " -> " + results[idx].file_id);
                } else if (use_txt_extension) {
                    // Retry with .txt extension (doesn't count as a retry)
                    std::string filename = std::filesystem::path(filepath).filename().string();
                    std::string txt_filename = filename + ".txt";
                    verbose_log("CURL", "Retrying with .txt extension: " + filepath);
                    pending.push({idx, retry_count, txt_filename});
                } else if (should_retry && retry_count < MAX_RETRIES) {
                    // Transient error - retry
                    verbose_log("CURL", "Retrying (" + std::to_string(retry_count + 1) + "/" +
                               std::to_string(MAX_RETRIES) + "): " + filepath + " - " + error_msg);
                    pending.push({idx, retry_count + 1, display_filename});
                } else {
                    // Permanent failure
                    results[idx].error = error_msg;
                    completed++;
                    if (on_progress) {
                        on_progress(completed, filepaths.size());
                    }
                    verbose_log("CURL", "Failed: " + filepath + " - " + error_msg);
                }

                // Start more uploads if we have capacity
                while (!pending.empty() && active.size() < current_max_parallel) {
                    auto item = pending.front();
                    pending.pop();
                    start_upload(item);
                }
            }
        }

        // If we have pending items but no active transfers, start more
        while (!pending.empty() && active.size() < current_max_parallel) {
            auto item = pending.front();
            pending.pop();
            start_upload(item);
            // Update still_running since we added new transfers
            curl_multi_perform(multi, &still_running);
        }

        // Wait for activity (with timeout to avoid spinning)
        if (still_running > 0) {
            curl_multi_wait(multi, nullptr, 0, 100, nullptr);
        }
    }

    // Cleanup any remaining active handles (shouldn't happen normally)
    for (auto& [easy, pair] : active) {
        auto& ctx = pair.second;
        curl_multi_remove_handle(multi, easy);
        curl_mime_free(ctx->mime);
        curl_slist_free_all(ctx->headers);
        curl_easy_cleanup(easy);
    }

    curl_multi_cleanup(multi);

    return results;
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
    verbose_out("CURL", "DELETE " + url);

    CURL* curl = curl_easy_init();
    if (!curl) {
        verbose_err("CURL", "Failed to initialize CURL");
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

    if (is_verbose()) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        verbose_err("CURL", std::string("DELETE failed: ") + curl_easy_strerror(res));
        throw std::runtime_error(std::string("HTTP DELETE failed: ") + curl_easy_strerror(res));
    }

    verbose_in("CURL", "HTTP " + std::to_string(http_code) + " - " + truncate(response, 500));
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

void OpenAIClient::delete_vector_store(const std::string& vector_store_id) {
    std::string url = std::string(OPENAI_API_BASE) + "/vector_stores/" + vector_store_id;

    std::string response = http_delete(url);
    json j = json::parse(response);

    if (j.contains("error")) {
        throw std::runtime_error("Failed to delete vector store: " + j["error"]["message"].get<std::string>());
    }
}

// Context for tracking each delete operation in the multi handle
struct MultiDeleteContext {
    std::string file_id;
    std::string response;
    CURL* easy;
    curl_slist* headers;
    int retry_count;
    long http_code;
    bool removing_from_store;  // true = removing from vector store, false = deleting file
};

// Item in the delete queue
struct DeleteQueueItem {
    size_t index;
    int retry_count;
    bool removing_from_store;  // true = removing from vector store, false = deleting file
};

std::vector<DeleteResult> OpenAIClient::delete_files_parallel(
    const std::string& vector_store_id,
    const std::vector<std::string>& file_ids,
    std::function<void(size_t completed, size_t total)> on_progress,
    size_t max_parallel
) {
    if (file_ids.empty()) {
        return {};
    }

    const int MAX_RETRIES = 5;
    std::vector<DeleteResult> results;
    results.reserve(file_ids.size());

    // Initialize results with empty entries
    for (const auto& fid : file_ids) {
        results.push_back({fid, ""});
    }

    // Create multi handle
    CURLM* multi = curl_multi_init();
    if (!multi) {
        throw std::runtime_error("Failed to initialize CURL multi handle");
    }

    // Track active deletes: maps easy handle to (result_index, context)
    std::map<CURL*, std::pair<size_t, std::unique_ptr<MultiDeleteContext>>> active;

    // Queue of files to delete - each file goes through two phases:
    // 1. Remove from vector store
    // 2. Delete file from storage
    std::queue<DeleteQueueItem> pending;
    for (size_t i = 0; i < file_ids.size(); ++i) {
        pending.push({i, 0, true});  // Start with removing from vector store
    }

    size_t completed = 0;
    size_t current_max_parallel = max_parallel;

    // Lambda to start a new delete operation
    auto start_delete = [&](const DeleteQueueItem& item) {
        const std::string& file_id = file_ids[item.index];

        auto ctx = std::make_unique<MultiDeleteContext>();
        ctx->file_id = file_id;
        ctx->retry_count = item.retry_count;
        ctx->http_code = 0;
        ctx->removing_from_store = item.removing_from_store;

        CURL* easy = curl_easy_init();
        if (!easy) {
            results[item.index].error = "Failed to initialize CURL";
            completed++;
            return;
        }

        ctx->easy = easy;
        ctx->headers = nullptr;
        std::string auth_header = "Authorization: Bearer " + api_key_;
        ctx->headers = curl_slist_append(ctx->headers, auth_header.c_str());

        // Build URL based on operation phase
        std::string url;
        if (item.removing_from_store) {
            url = std::string(OPENAI_API_BASE) + "/vector_stores/" + vector_store_id + "/files/" + file_id;
        } else {
            url = std::string(OPENAI_API_BASE) + "/files/" + file_id;
        }

        curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "DELETE");
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, ctx->headers);
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, &ctx->response);

        if (is_verbose()) {
            curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
        }

        curl_multi_add_handle(multi, easy);
        active[easy] = {item.index, std::move(ctx)};

        verbose_log("CURL", "Started " + std::string(item.removing_from_store ? "remove from store" : "delete file") +
                   " (attempt " + std::to_string(item.retry_count + 1) + "): " + file_id);
    };

    // Start initial batch of deletes
    while (!pending.empty() && active.size() < current_max_parallel) {
        auto item = pending.front();
        pending.pop();
        start_delete(item);
    }

    // Main event loop
    int still_running = 1;
    while (still_running > 0 || !pending.empty()) {
        // Perform transfers
        CURLMcode mc = curl_multi_perform(multi, &still_running);
        if (mc != CURLM_OK) {
            verbose_err("CURL", std::string("curl_multi_perform failed: ") + curl_multi_strerror(mc));
            break;
        }

        // Check for completed transfers
        int msgs_left;
        CURLMsg* msg;
        while ((msg = curl_multi_info_read(multi, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                CURL* easy = msg->easy_handle;
                auto it = active.find(easy);
                if (it == active.end()) continue;

                size_t idx = it->second.first;
                auto& ctx = it->second.second;

                // Remove from multi handle
                curl_multi_remove_handle(multi, easy);

                // Get HTTP status code
                curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &ctx->http_code);

                // Determine outcome
                bool should_retry = false;
                bool is_not_found = false;
                std::string error_msg;

                CURLcode res = msg->data.result;
                if (res != CURLE_OK) {
                    error_msg = curl_easy_strerror(res);
                    should_retry = true;  // Network errors are retryable
                } else if (ctx->http_code == 429) {
                    // Rate limited - reduce parallelism
                    error_msg = "Rate limited (HTTP 429)";
                    should_retry = true;
                    if (current_max_parallel > 1) {
                        current_max_parallel = current_max_parallel / 2;
                        verbose_log("CURL", "Rate limited, reducing parallelism to " + std::to_string(current_max_parallel));
                    }
                } else if (ctx->http_code >= 500) {
                    // Server error - retryable
                    error_msg = "Server error (HTTP " + std::to_string(ctx->http_code) + ")";
                    should_retry = true;
                } else if (ctx->http_code == 404 || ctx->http_code == 400) {
                    // Not found or bad request - check if it's a "not found" error (which we ignore)
                    try {
                        json j = json::parse(ctx->response);
                        if (j.contains("error") && j["error"].contains("message")) {
                            error_msg = j["error"]["message"].get<std::string>();
                            if (error_msg.find("No such") != std::string::npos ||
                                error_msg.find("not found") != std::string::npos) {
                                is_not_found = true;
                            }
                        }
                    } catch (...) {
                        // Treat as not found if we can't parse
                        is_not_found = true;
                    }
                } else if (ctx->http_code >= 400) {
                    // Other client error
                    try {
                        json j = json::parse(ctx->response);
                        if (j.contains("error") && j["error"].contains("message")) {
                            error_msg = j["error"]["message"].get<std::string>();
                        } else {
                            error_msg = "HTTP " + std::to_string(ctx->http_code);
                        }
                    } catch (...) {
                        error_msg = "HTTP " + std::to_string(ctx->http_code);
                    }
                }

                int retry_count = ctx->retry_count;
                std::string file_id = ctx->file_id;
                bool was_removing_from_store = ctx->removing_from_store;
                long http_code = ctx->http_code;

                // Cleanup this context
                curl_slist_free_all(ctx->headers);
                curl_easy_cleanup(easy);
                active.erase(it);

                // Decide what to do next
                if (http_code == 200 || is_not_found) {
                    // Success or "not found" (which we treat as success)
                    if (was_removing_from_store) {
                        // Phase 1 complete, queue phase 2 (delete file)
                        pending.push({idx, 0, false});
                        verbose_log("CURL", "Removed from store: " + file_id + (is_not_found ? " (was not found)" : ""));
                    } else {
                        // Phase 2 complete, file fully deleted
                        completed++;
                        if (on_progress) {
                            on_progress(completed, file_ids.size());
                        }
                        verbose_log("CURL", "Deleted file: " + file_id + (is_not_found ? " (was not found)" : ""));
                    }
                } else if (should_retry && retry_count < MAX_RETRIES) {
                    // Transient error - retry same operation
                    verbose_log("CURL", "Retrying (" + std::to_string(retry_count + 1) + "/" +
                               std::to_string(MAX_RETRIES) + "): " + file_id + " - " + error_msg);
                    pending.push({idx, retry_count + 1, was_removing_from_store});
                } else {
                    // Permanent failure
                    results[idx].error = error_msg;
                    completed++;
                    if (on_progress) {
                        on_progress(completed, file_ids.size());
                    }
                    verbose_log("CURL", "Failed to delete: " + file_id + " - " + error_msg);
                }

                // Start more deletes if we have capacity
                while (!pending.empty() && active.size() < current_max_parallel) {
                    auto item = pending.front();
                    pending.pop();
                    start_delete(item);
                }
            }
        }

        // If we have pending items but no active transfers, start more
        while (!pending.empty() && active.size() < current_max_parallel) {
            auto item = pending.front();
            pending.pop();
            start_delete(item);
            // Update still_running since we added new transfers
            curl_multi_perform(multi, &still_running);
        }

        // Wait for activity (with timeout to avoid spinning)
        if (still_running > 0) {
            curl_multi_wait(multi, nullptr, 0, 100, nullptr);
        }
    }

    // Cleanup any remaining active handles (shouldn't happen normally)
    for (auto& [easy, pair] : active) {
        auto& ctx = pair.second;
        curl_multi_remove_handle(multi, easy);
        curl_slist_free_all(ctx->headers);
        curl_easy_cleanup(easy);
    }

    curl_multi_cleanup(multi);

    return results;
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
    std::string url = std::string(OPENAI_API_BASE) + "/responses";

    // Build input JSON.
    // When using previous_response_id, only send the latest user message
    // (the API maintains conversation context server-side).
    // Otherwise, send the full conversation.
    json input = json::array();
    if (!previous_response_id.empty() && !conversation.empty()) {
        // Find the last user message to send
        for (auto it = conversation.rbegin(); it != conversation.rend(); ++it) {
            if (it->role == "user") {
                input.push_back(it->to_json());
                break;
            }
        }
    } else {
        // No previous response - send full conversation
        for (const auto& msg : conversation) {
            input.push_back(msg.to_json());
        }
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

    // Add previous_response_id for conversation continuation
    if (!previous_response_id.empty()) {
        body["previous_response_id"] = previous_response_id;
    }

    // Track response ID, usage, and errors during streaming.
    std::string response_id;
    ResponseUsage usage;
    std::string stream_error;

    // Stream the response.
    bool completed = http_post_stream(url, body, [&](const std::string& data) {
        try {
            json event = json::parse(data);
            std::string event_type = event.value("type", "");

            if (event_type == "response.created") {
                // Capture the response ID when the response is created
                if (event.contains("response") && event["response"].contains("id")) {
                    response_id = event["response"]["id"].get<std::string>();
                }
            } else if (event_type == "response.output_text.delta") {
                std::string delta = event.value("delta", "");
                if (!delta.empty() && on_text) {
                    on_text(delta);
                }
            } else if (event_type == "response.completed") {
                // Capture token usage from completed response
                if (event.contains("response") && event["response"].contains("usage")) {
                    auto u = event["response"]["usage"];
                    if (u.contains("input_tokens")) {
                        usage.input_tokens = u["input_tokens"].get<int>();
                    }
                    if (u.contains("output_tokens")) {
                        usage.output_tokens = u["output_tokens"].get<int>();
                    }
                    if (u.contains("output_tokens_details") &&
                        u["output_tokens_details"].contains("reasoning_tokens")) {
                        usage.reasoning_tokens = u["output_tokens_details"]["reasoning_tokens"].get<int>();
                    }
                }
            } else if (event_type == "error") {
                // Capture error from the stream.
                if (event.contains("error") && event["error"].contains("message")) {
                    stream_error = event["error"]["message"].get<std::string>();
                } else {
                    stream_error = "Unknown API error";
                }
            } else if (event_type == "response.failed") {
                // Capture error from failed response.
                if (event.contains("response") &&
                    event["response"].contains("error") &&
                    event["response"]["error"].contains("message")) {
                    stream_error = event["response"]["error"]["message"].get<std::string>();
                }
            }
        } catch (const json::exception&) {
            // Ignore malformed JSON in stream.
        }
    }, cancel_check);

    // If cancelled, return empty result to indicate cancellation
    if (!completed) {
        return StreamResult{"", {}};
    }

    // Throw if we encountered an error during streaming.
    if (!stream_error.empty()) {
        throw std::runtime_error(stream_error);
    }

    return StreamResult{response_id, usage};
}

StreamResult OpenAIClient::stream_response(
    const std::string& model,
    const nlohmann::json& input_window,
    const std::string& vector_store_id,
    const std::string& reasoning_effort,
    const std::string& previous_response_id,
    std::function<void(const std::string&)> on_text,
    CancelCallback cancel_check
) {
    std::string url = std::string(OPENAI_API_BASE) + "/responses";

    // Build input JSON.
    // When using previous_response_id, only send the latest user message
    // (the API maintains conversation context server-side).
    // Otherwise, send the full input window (which may contain compaction items).
    json input = json::array();
    if (!previous_response_id.empty() && input_window.is_array() && !input_window.empty()) {
        // Find the last user message to send
        for (auto it = input_window.rbegin(); it != input_window.rend(); ++it) {
            if (it->contains("role") && (*it)["role"] == "user") {
                input.push_back(*it);
                break;
            }
        }
    } else {
        // No previous response - send full input window
        input = input_window;
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

    // Add previous_response_id for conversation continuation
    if (!previous_response_id.empty()) {
        body["previous_response_id"] = previous_response_id;
    }

    // Track response ID, usage, and errors during streaming.
    std::string response_id;
    ResponseUsage usage;
    std::string stream_error;

    // Stream the response.
    bool completed = http_post_stream(url, body, [&](const std::string& data) {
        try {
            json event = json::parse(data);
            std::string event_type = event.value("type", "");

            if (event_type == "response.created") {
                if (event.contains("response") && event["response"].contains("id")) {
                    response_id = event["response"]["id"].get<std::string>();
                }
            } else if (event_type == "response.output_text.delta") {
                std::string delta = event.value("delta", "");
                if (!delta.empty() && on_text) {
                    on_text(delta);
                }
            } else if (event_type == "response.completed") {
                if (event.contains("response") && event["response"].contains("usage")) {
                    auto u = event["response"]["usage"];
                    if (u.contains("input_tokens")) {
                        usage.input_tokens = u["input_tokens"].get<int>();
                    }
                    if (u.contains("output_tokens")) {
                        usage.output_tokens = u["output_tokens"].get<int>();
                    }
                    if (u.contains("output_tokens_details") &&
                        u["output_tokens_details"].contains("reasoning_tokens")) {
                        usage.reasoning_tokens = u["output_tokens_details"]["reasoning_tokens"].get<int>();
                    }
                }
            } else if (event_type == "error") {
                if (event.contains("error") && event["error"].contains("message")) {
                    stream_error = event["error"]["message"].get<std::string>();
                } else {
                    stream_error = "Unknown API error";
                }
            } else if (event_type == "response.failed") {
                if (event.contains("response") &&
                    event["response"].contains("error") &&
                    event["response"]["error"].contains("message")) {
                    stream_error = event["response"]["error"]["message"].get<std::string>();
                }
            }
        } catch (const json::exception&) {
            // Ignore malformed JSON in stream.
        }
    }, cancel_check);

    if (!completed) {
        return StreamResult{"", {}};
    }

    if (!stream_error.empty()) {
        throw std::runtime_error(stream_error);
    }

    return StreamResult{response_id, usage};
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
    std::string url = std::string(OPENAI_API_BASE) + "/responses";

    // Build input JSON.
    // When using previous_response_id, only send the latest user message
    // (the API maintains conversation context server-side).
    // Otherwise, send the full conversation.
    json input = json::array();
    if (!previous_response_id.empty() && !conversation.empty()) {
        // Find the last user message to send
        for (auto it = conversation.rbegin(); it != conversation.rend(); ++it) {
            if (it->role == "user") {
                input.push_back(it->to_json());
                break;
            }
        }
    } else {
        // No previous response - send full conversation
        for (const auto& msg : conversation) {
            input.push_back(msg.to_json());
        }
    }

    // Build tools array - start with file_search, add additional tools
    json tools = json::array({
        {
            {"type", "file_search"},
            {"vector_store_ids", json::array({vector_store_id})}
        }
    });

    // Add additional tools (MCP function tools)
    for (const auto& tool : additional_tools) {
        tools.push_back(tool);
    }

    // Build request body.
    json body = {
        {"model", model},
        {"input", input},
        {"stream", true},
        {"tools", tools}
    };

    if (!reasoning_effort.empty()) {
        body["reasoning"] = {{"effort", reasoning_effort}};
    }

    // Add previous_response_id for conversation continuation
    std::string current_response_id = previous_response_id;
    if (!current_response_id.empty()) {
        body["previous_response_id"] = current_response_id;
    }

    // Track cumulative usage across all calls in this turn
    ResponseUsage total_usage;

    // Loop to handle tool calls - after each tool call, we submit results and continue
    while (true) {
        // Track response ID, usage, errors, and tool calls during streaming.
        std::string response_id;
        ResponseUsage call_usage;
        std::string stream_error;

        // Collect tool calls during this response
        struct PendingToolCall {
            std::string call_id;
            std::string name;
            std::string arguments;
        };
        std::vector<PendingToolCall> pending_tool_calls;

        // Track current tool call being built (for streaming tool calls)
        std::string current_call_id;
        std::string current_tool_name;
        std::string current_arguments;

        // Stream the response.
        bool completed = http_post_stream(url, body, [&](const std::string& data) {
            try {
                json event = json::parse(data);
                std::string event_type = event.value("type", "");

                if (event_type == "response.created") {
                    // Capture the response ID when the response is created
                    if (event.contains("response") && event["response"].contains("id")) {
                        response_id = event["response"]["id"].get<std::string>();
                    }
                } else if (event_type == "response.output_text.delta") {
                    std::string delta = event.value("delta", "");
                    if (!delta.empty() && on_text) {
                        on_text(delta);
                    }
                } else if (event_type == "response.completed") {
                    // Capture token usage from completed response
                    if (event.contains("response") && event["response"].contains("usage")) {
                        auto u = event["response"]["usage"];
                        if (u.contains("input_tokens")) {
                            call_usage.input_tokens = u["input_tokens"].get<int>();
                        }
                        if (u.contains("output_tokens")) {
                            call_usage.output_tokens = u["output_tokens"].get<int>();
                        }
                        if (u.contains("output_tokens_details") &&
                            u["output_tokens_details"].contains("reasoning_tokens")) {
                            call_usage.reasoning_tokens = u["output_tokens_details"]["reasoning_tokens"].get<int>();
                        }
                    }
                } else if (event_type == "response.output_item.added") {
                    // Check if this is a function call
                    if (event.contains("item") && event["item"].contains("type")) {
                        if (event["item"]["type"] == "function_call") {
                            current_call_id = event["item"].value("call_id", "");
                            current_tool_name = event["item"].value("name", "");
                            current_arguments.clear();
                            verbose_log("MCP", "Tool call started: " + current_tool_name + " (id: " + current_call_id + ")");
                        }
                    }
                } else if (event_type == "response.function_call_arguments.delta") {
                    // Accumulate function call arguments
                    std::string delta = event.value("delta", "");
                    current_arguments += delta;
                } else if (event_type == "response.function_call_arguments.done") {
                    // Function call complete - store it for later execution
                    std::string call_id = event.value("call_id", current_call_id);
                    std::string name = event.value("name", current_tool_name);
                    std::string args_str = event.value("arguments", current_arguments);

                    verbose_log("MCP", "Tool call complete: " + name + " args: " + args_str);

                    pending_tool_calls.push_back({call_id, name, args_str});

                    // Reset current tool call state
                    current_call_id.clear();
                    current_tool_name.clear();
                    current_arguments.clear();
                } else if (event_type == "error") {
                    // Capture error from the stream.
                    if (event.contains("error") && event["error"].contains("message")) {
                        stream_error = event["error"]["message"].get<std::string>();
                    } else {
                        stream_error = "Unknown API error";
                    }
                } else if (event_type == "response.failed") {
                    // Capture error from failed response.
                    if (event.contains("response") &&
                        event["response"].contains("error") &&
                        event["response"]["error"].contains("message")) {
                        stream_error = event["response"]["error"]["message"].get<std::string>();
                    }
                }
            } catch (const json::exception&) {
                // Ignore malformed JSON in stream.
            }
        }, cancel_check);

        // Accumulate usage from this call
        total_usage.input_tokens += call_usage.input_tokens;
        total_usage.output_tokens += call_usage.output_tokens;
        total_usage.reasoning_tokens += call_usage.reasoning_tokens;

        // If cancelled, return empty result to indicate cancellation
        if (!completed) {
            return StreamResult{"", {}};
        }

        // Throw if we encountered an error during streaming.
        if (!stream_error.empty()) {
            throw std::runtime_error(stream_error);
        }

        // If no tool calls, we're done
        if (pending_tool_calls.empty()) {
            return StreamResult{response_id, total_usage};
        }

        // Execute tool calls and collect results
        json tool_outputs = json::array();
        for (const auto& tc : pending_tool_calls) {
            std::string result;
            if (on_tool_call) {
                try {
                    json args = json::parse(tc.arguments.empty() ? "{}" : tc.arguments);
                    result = on_tool_call(tc.call_id, tc.name, args);
                } catch (const json::exception& e) {
                    verbose_err("MCP", "Failed to parse tool arguments: " + std::string(e.what()));
                    result = on_tool_call(tc.call_id, tc.name, json::object());
                }
            } else {
                result = "Tool executed";
            }

            tool_outputs.push_back({
                {"type", "function_call_output"},
                {"call_id", tc.call_id},
                {"output", result}
            });

            verbose_log("MCP", "Tool " + tc.name + " returned: " + result);
        }

        // Build follow-up request with tool outputs
        current_response_id = response_id;
        body = {
            {"model", model},
            {"input", tool_outputs},
            {"stream", true},
            {"tools", tools},
            {"previous_response_id", current_response_id}
        };

        if (!reasoning_effort.empty()) {
            body["reasoning"] = {{"effort", reasoning_effort}};
        }

        verbose_log("MCP", "Submitting tool results and continuing...");
    }
}

StreamResult OpenAIClient::stream_response_with_tools(
    const std::string& model,
    const nlohmann::json& input_window,
    const std::string& vector_store_id,
    const std::string& reasoning_effort,
    const std::string& previous_response_id,
    const nlohmann::json& additional_tools,
    OnTextCallback on_text,
    OnToolCallWithResultCallback on_tool_call,
    CancelCallback cancel_check
) {
    std::string url = std::string(OPENAI_API_BASE) + "/responses";

    // Build input JSON.
    // When using previous_response_id, only send the latest user message.
    // Otherwise, send the full input window (which may contain compaction items).
    json input = json::array();
    if (!previous_response_id.empty() && input_window.is_array() && !input_window.empty()) {
        // Find the last user message to send
        for (auto it = input_window.rbegin(); it != input_window.rend(); ++it) {
            if (it->contains("role") && (*it)["role"] == "user") {
                input.push_back(*it);
                break;
            }
        }
    } else {
        // No previous response - send full input window
        input = input_window;
    }

    // Build tools array - start with file_search, add additional tools
    json tools = json::array({
        {
            {"type", "file_search"},
            {"vector_store_ids", json::array({vector_store_id})}
        }
    });

    for (const auto& tool : additional_tools) {
        tools.push_back(tool);
    }

    // Build request body.
    json body = {
        {"model", model},
        {"input", input},
        {"stream", true},
        {"tools", tools}
    };

    if (!reasoning_effort.empty()) {
        body["reasoning"] = {{"effort", reasoning_effort}};
    }

    std::string current_response_id = previous_response_id;

    ResponseUsage total_usage;
    std::string full_text;

    while (true) {
        if (!current_response_id.empty()) {
            body["previous_response_id"] = current_response_id;
        }

        std::string response_id;
        ResponseUsage call_usage;
        std::string stream_error;
        std::string current_call_id;
        std::string current_tool_name;
        std::string current_arguments;
        std::vector<std::tuple<std::string, std::string, std::string>> pending_tool_calls;

        bool completed = http_post_stream(url, body, [&](const std::string& data) {
            try {
                json event = json::parse(data);
                std::string event_type = event.value("type", "");

                if (event_type == "response.created") {
                    if (event.contains("response") && event["response"].contains("id")) {
                        response_id = event["response"]["id"].get<std::string>();
                        verbose_log("MCP", "Response created with ID: " + response_id);
                    }
                } else if (event_type == "response.output_text.delta") {
                    std::string delta = event.value("delta", "");
                    if (!delta.empty()) {
                        full_text += delta;
                        if (on_text) {
                            on_text(delta);
                        }
                    }
                } else if (event_type == "response.completed") {
                    if (event.contains("response") && event["response"].contains("usage")) {
                        auto u = event["response"]["usage"];
                        if (u.contains("input_tokens")) {
                            call_usage.input_tokens = u["input_tokens"].get<int>();
                        }
                        if (u.contains("output_tokens")) {
                            call_usage.output_tokens = u["output_tokens"].get<int>();
                        }
                        if (u.contains("output_tokens_details") &&
                            u["output_tokens_details"].contains("reasoning_tokens")) {
                            call_usage.reasoning_tokens = u["output_tokens_details"]["reasoning_tokens"].get<int>();
                        }
                    }
                } else if (event_type == "response.output_item.added") {
                    if (event.contains("item") && event["item"].contains("type")) {
                        if (event["item"]["type"] == "function_call") {
                            current_call_id = event["item"].value("call_id", "");
                            current_tool_name = event["item"].value("name", "");
                            current_arguments.clear();
                            verbose_log("MCP", "Tool call started: " + current_tool_name + " (id: " + current_call_id + ")");
                        }
                    }
                } else if (event_type == "response.function_call_arguments.delta") {
                    std::string delta = event.value("delta", "");
                    current_arguments += delta;
                } else if (event_type == "response.function_call_arguments.done") {
                    std::string call_id = event.value("call_id", current_call_id);
                    std::string name = event.value("name", current_tool_name);
                    std::string args_str = event.value("arguments", current_arguments);

                    verbose_log("MCP", "Tool call complete: " + name + " args: " + args_str);
                    pending_tool_calls.push_back({call_id, name, args_str});

                    current_call_id.clear();
                    current_tool_name.clear();
                    current_arguments.clear();
                } else if (event_type == "error") {
                    if (event.contains("error") && event["error"].contains("message")) {
                        stream_error = event["error"]["message"].get<std::string>();
                    } else {
                        stream_error = "Unknown API error";
                    }
                } else if (event_type == "response.failed") {
                    if (event.contains("response") &&
                        event["response"].contains("error") &&
                        event["response"]["error"].contains("message")) {
                        stream_error = event["response"]["error"]["message"].get<std::string>();
                    }
                }
            } catch (const json::exception&) {
                // Ignore malformed JSON in stream.
            }
        }, cancel_check);

        total_usage.input_tokens = call_usage.input_tokens;
        total_usage.output_tokens += call_usage.output_tokens;
        total_usage.reasoning_tokens += call_usage.reasoning_tokens;

        if (!completed) {
            return StreamResult{"", {}};
        }

        if (!stream_error.empty()) {
            throw std::runtime_error(stream_error);
        }

        current_response_id = response_id;

        if (pending_tool_calls.empty()) {
            return StreamResult{response_id, total_usage};
        }

        json tool_outputs = json::array();
        for (const auto& [call_id, name, args_str] : pending_tool_calls) {
            json args;
            try {
                args = json::parse(args_str);
            } catch (const json::exception&) {
                args = json::object();
            }

            verbose_log("MCP", "Executing tool: " + name);
            std::string result = on_tool_call(call_id, name, args);
            verbose_log("MCP", "Tool result: " + result.substr(0, 200) + (result.length() > 200 ? "..." : ""));

            tool_outputs.push_back({
                {"type", "function_call_output"},
                {"call_id", call_id},
                {"output", result}
            });
        }

        body = {
            {"model", model},
            {"input", tool_outputs},
            {"stream", true},
            {"tools", tools},
            {"previous_response_id", current_response_id}
        };

        if (!reasoning_effort.empty()) {
            body["reasoning"] = {{"effort", reasoning_effort}};
        }

        verbose_log("MCP", "Submitting tool results and continuing...");
    }
}

nlohmann::json OpenAIClient::compact_window(const std::string& model, const std::string& previous_response_id) {
    std::string url = std::string(OPENAI_API_BASE) + "/responses/compact";

    json body = {
        {"model", model},
        {"previous_response_id", previous_response_id}
    };

    std::string response_str = http_post_json(url, body);
    json j = json::parse(response_str);

    if (j.contains("error")) {
        throw std::runtime_error("Compact error: " + j["error"]["message"].get<std::string>());
    }

    if (!j.contains("output")) {
        throw std::runtime_error("Compact error: missing 'output' field in response");
    }

    return j["output"];
}

} // namespace rag
