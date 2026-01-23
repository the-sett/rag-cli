#include "openai_provider.hpp"
#include "../../verbose.hpp"
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <map>
#include <queue>
#include <memory>

namespace rag::providers::openai {

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

    size_t pos;
    while ((pos = ctx->buffer.find("\n")) != std::string::npos) {
        std::string line = ctx->buffer.substr(0, pos);
        ctx->buffer.erase(0, pos + 1);

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.substr(0, 6) == "data: ") {
            std::string data = line.substr(6);
            if (data != "[DONE]") {
                ctx->on_data(data);
            }
        }
    }

    return total_size;
}

OpenAIProvider::OpenAIProvider(const std::string& api_key, const std::string& api_base_url)
    : api_key_(api_key)
    , api_base_(api_base_url.empty() ? OPENAI_API_BASE : api_base_url) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

OpenAIProvider::~OpenAIProvider() {
    curl_global_cleanup();
}

std::string OpenAIProvider::http_get(const std::string& url) {
    rag::verbose_out("CURL", "GET " + url);

    CURL* curl = curl_easy_init();
    if (!curl) {
        rag::verbose_err("CURL", "Failed to initialize CURL");
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
    return response;
}

std::string OpenAIProvider::http_post_json(const std::string& url, const json& body) {
    std::string body_str = body.dump();
    rag::verbose_out("CURL", "POST " + url);
    rag::verbose_out("CURL", "Body: " + rag::format_json_compact(body_str));

    CURL* curl = curl_easy_init();
    if (!curl) {
        rag::verbose_err("CURL", "Failed to initialize CURL");
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
    return response;
}

std::string OpenAIProvider::http_post_multipart(const std::string& url,
                                                 const std::string& filepath,
                                                 const std::string& purpose,
                                                 const std::string& display_filename) {
    rag::verbose_out("CURL", "POST (multipart) " + url);
    rag::verbose_out("CURL", "File: " + filepath + " purpose: " + purpose);

    CURL* curl = curl_easy_init();
    if (!curl) {
        rag::verbose_err("CURL", "Failed to initialize CURL");
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response;

    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + api_key_;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_mime* mime = curl_mime_init(curl);

    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, filepath.c_str());

    if (!display_filename.empty()) {
        curl_mime_filename(part, display_filename.c_str());
    }

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "purpose");
    curl_mime_data(part, purpose.c_str(), CURL_ZERO_TERMINATED);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
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
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        rag::verbose_err("CURL", std::string("POST multipart failed: ") + curl_easy_strerror(res));
        throw std::runtime_error(std::string("HTTP POST multipart failed: ") + curl_easy_strerror(res));
    }

    rag::verbose_in("CURL", "HTTP " + std::to_string(http_code) + " - " + rag::truncate(response, 500));
    return response;
}

bool OpenAIProvider::http_post_stream(const std::string& url,
                                       const json& body,
                                       std::function<void(const std::string&)> on_data,
                                       CancelCallback cancel_check) {
    std::string body_str = body.dump();
    rag::verbose_out("CURL", "POST (stream) " + url);
    rag::verbose_out("CURL", "Body: " + rag::format_json_compact(body_str, 1000));

    CURL* curl = curl_easy_init();
    if (!curl) {
        rag::verbose_err("CURL", "Failed to initialize CURL");
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

std::string OpenAIProvider::http_delete(const std::string& url) {
    rag::verbose_out("CURL", "DELETE " + url);

    CURL* curl = curl_easy_init();
    if (!curl) {
        rag::verbose_err("CURL", "Failed to initialize CURL");
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

    if (rag::is_verbose()) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        rag::verbose_err("CURL", std::string("DELETE failed: ") + curl_easy_strerror(res));
        throw std::runtime_error(std::string("HTTP DELETE failed: ") + curl_easy_strerror(res));
    }

    rag::verbose_in("CURL", "HTTP " + std::to_string(http_code) + " - " + rag::truncate(response, 500));
    return response;
}

// ========== IModelsService ==========

std::vector<ModelInfo> OpenAIProvider::list_models() {
    std::string url = api_base_ + "/models";
    std::string response = http_get(url);

    json j = json::parse(response);
    std::vector<ModelInfo> models;

    if (j.contains("data") && j["data"].is_array()) {
        for (const auto& model : j["data"]) {
            std::string id = model.value("id", "");
            // Filter to gpt-5* models only
            if (id.substr(0, 5) == "gpt-5") {
                ModelInfo info;
                info.id = id;
                info.display_name = id;
                info.max_context_tokens = 128000;  // Default
                info.supports_tools = true;
                info.supports_reasoning = false;
                models.push_back(info);
            }
        }
    }

    std::sort(models.begin(), models.end(), [](const ModelInfo& a, const ModelInfo& b) {
        return a.id < b.id;
    });

    return models;
}

std::optional<ModelInfo> OpenAIProvider::get_model_info(const std::string& model_id) {
    auto models = list_models();
    for (const auto& model : models) {
        if (model.id == model_id) {
            return model;
        }
    }
    return std::nullopt;
}

// ========== IFilesService ==========

std::string OpenAIProvider::upload_file(const std::string& filepath) {
    std::string url = api_base_ + "/files";
    std::string response = http_post_multipart(url, filepath, "assistants");

    json j = json::parse(response);

    if (j.contains("error")) {
        std::string error_msg = j["error"]["message"].get<std::string>();

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
    std::string display_filename;
    std::string response;
    CURL* easy;
    curl_mime* mime;
    curl_slist* headers;
    int retry_count;
    long http_code;
};

struct UploadQueueItem {
    size_t index;
    int retry_count;
    std::string display_filename;
};

std::vector<UploadResult> OpenAIProvider::upload_files_parallel(
    const std::vector<std::string>& filepaths,
    ProgressCallback on_progress,
    size_t max_parallel
) {
    if (filepaths.empty()) {
        return {};
    }

    const int MAX_RETRIES = 5;
    std::string url = api_base_ + "/files";
    std::vector<UploadResult> results;
    results.reserve(filepaths.size());

    for (const auto& fp : filepaths) {
        results.push_back({fp, "", ""});
    }

    CURLM* multi = curl_multi_init();
    if (!multi) {
        throw std::runtime_error("Failed to initialize CURL multi handle");
    }

    std::map<CURL*, std::pair<size_t, std::unique_ptr<MultiUploadContext>>> active;
    std::queue<UploadQueueItem> pending;
    for (size_t i = 0; i < filepaths.size(); ++i) {
        pending.push({i, 0, ""});
    }

    size_t completed = 0;
    size_t current_max_parallel = max_parallel;
    std::string api_key = api_key_;

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
        std::string auth_header = "Authorization: Bearer " + api_key;
        ctx->headers = curl_slist_append(ctx->headers, auth_header.c_str());

        ctx->mime = curl_mime_init(easy);

        curl_mimepart* part = curl_mime_addpart(ctx->mime);
        curl_mime_name(part, "file");
        curl_mime_filedata(part, filepath.c_str());

        if (!item.display_filename.empty()) {
            curl_mime_filename(part, item.display_filename.c_str());
        }

        part = curl_mime_addpart(ctx->mime);
        curl_mime_name(part, "purpose");
        curl_mime_data(part, "assistants", CURL_ZERO_TERMINATED);

        curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, ctx->headers);
        curl_easy_setopt(easy, CURLOPT_MIMEPOST, ctx->mime);
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, &ctx->response);

        if (rag::is_verbose()) {
            curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
        }

        curl_multi_add_handle(multi, easy);
        active[easy] = {item.index, std::move(ctx)};

        rag::verbose_log("CURL", "Started upload (attempt " + std::to_string(item.retry_count + 1) + "): " + filepath);
    };

    while (!pending.empty() && active.size() < current_max_parallel) {
        auto item = pending.front();
        pending.pop();
        start_upload(item);
    }

    int still_running = 1;
    while (still_running > 0 || !pending.empty()) {
        CURLMcode mc = curl_multi_perform(multi, &still_running);
        if (mc != CURLM_OK) {
            rag::verbose_err("CURL", std::string("curl_multi_perform failed: ") + curl_multi_strerror(mc));
            break;
        }

        int msgs_left;
        CURLMsg* msg;
        while ((msg = curl_multi_info_read(multi, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                CURL* easy = msg->easy_handle;
                auto it = active.find(easy);
                if (it == active.end()) continue;

                size_t idx = it->second.first;
                auto& ctx = it->second.second;

                curl_multi_remove_handle(multi, easy);
                curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &ctx->http_code);

                bool should_retry = false;
                bool use_txt_extension = false;
                std::string error_msg;

                CURLcode res = msg->data.result;
                if (res != CURLE_OK) {
                    error_msg = curl_easy_strerror(res);
                    should_retry = true;
                } else if (ctx->response.empty()) {
                    error_msg = "Empty response (HTTP " + std::to_string(ctx->http_code) + ")";
                    should_retry = (ctx->http_code >= 500 || ctx->http_code == 429);
                } else if (ctx->http_code == 429) {
                    error_msg = "Rate limited (HTTP 429)";
                    should_retry = true;
                    if (current_max_parallel > 1) {
                        current_max_parallel = current_max_parallel / 2;
                        rag::verbose_log("CURL", "Rate limited, reducing parallelism to " + std::to_string(current_max_parallel));
                    }
                } else if (ctx->http_code >= 500) {
                    error_msg = "Server error (HTTP " + std::to_string(ctx->http_code) + ")";
                    should_retry = true;
                } else if (ctx->http_code >= 400) {
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

                curl_mime_free(ctx->mime);
                curl_slist_free_all(ctx->headers);
                curl_easy_cleanup(easy);
                active.erase(it);

                if (results[idx].success()) {
                    completed++;
                    if (on_progress) {
                        on_progress(completed, filepaths.size());
                    }
                    rag::verbose_log("CURL", "Uploaded: " + filepath + " -> " + results[idx].file_id);
                } else if (use_txt_extension) {
                    std::string filename = std::filesystem::path(filepath).filename().string();
                    std::string txt_filename = filename + ".txt";
                    rag::verbose_log("CURL", "Retrying with .txt extension: " + filepath);
                    pending.push({idx, retry_count, txt_filename});
                } else if (should_retry && retry_count < MAX_RETRIES) {
                    rag::verbose_log("CURL", "Retrying (" + std::to_string(retry_count + 1) + "/" +
                               std::to_string(MAX_RETRIES) + "): " + filepath + " - " + error_msg);
                    pending.push({idx, retry_count + 1, display_filename});
                } else {
                    results[idx].error = error_msg;
                    completed++;
                    if (on_progress) {
                        on_progress(completed, filepaths.size());
                    }
                    rag::verbose_log("CURL", "Failed: " + filepath + " - " + error_msg);
                }

                while (!pending.empty() && active.size() < current_max_parallel) {
                    auto item = pending.front();
                    pending.pop();
                    start_upload(item);
                }
            }
        }

        while (!pending.empty() && active.size() < current_max_parallel) {
            auto item = pending.front();
            pending.pop();
            start_upload(item);
            curl_multi_perform(multi, &still_running);
        }

        if (still_running > 0) {
            curl_multi_wait(multi, nullptr, 0, 100, nullptr);
        }
    }

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

void OpenAIProvider::delete_file(const std::string& file_id) {
    std::string url = api_base_ + "/files/" + file_id;

    std::string response = http_delete(url);
    json j = json::parse(response);

    if (j.contains("error")) {
        throw std::runtime_error("Failed to delete file: " + j["error"]["message"].get<std::string>());
    }
}

struct MultiDeleteContext {
    std::string file_id;
    std::string response;
    CURL* easy;
    curl_slist* headers;
    int retry_count;
    long http_code;
    bool removing_from_store;
};

struct DeleteQueueItem {
    size_t index;
    int retry_count;
    bool removing_from_store;
};

std::vector<DeleteResult> OpenAIProvider::delete_files_parallel(
    const std::vector<std::string>& file_ids,
    const std::string& store_id,
    ProgressCallback on_progress,
    size_t max_parallel
) {
    if (file_ids.empty()) {
        return {};
    }

    const int MAX_RETRIES = 5;
    std::vector<DeleteResult> results;
    results.reserve(file_ids.size());

    for (const auto& fid : file_ids) {
        results.push_back({fid, ""});
    }

    CURLM* multi = curl_multi_init();
    if (!multi) {
        throw std::runtime_error("Failed to initialize CURL multi handle");
    }

    std::map<CURL*, std::pair<size_t, std::unique_ptr<MultiDeleteContext>>> active;
    std::queue<DeleteQueueItem> pending;

    bool has_store = !store_id.empty();
    for (size_t i = 0; i < file_ids.size(); ++i) {
        pending.push({i, 0, has_store});
    }

    size_t completed = 0;
    size_t current_max_parallel = max_parallel;
    std::string api_key = api_key_;
    std::string api_base = api_base_;

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
        std::string auth_header = "Authorization: Bearer " + api_key;
        ctx->headers = curl_slist_append(ctx->headers, auth_header.c_str());

        std::string url;
        if (item.removing_from_store) {
            url = api_base + "/vector_stores/" + store_id + "/files/" + file_id;
        } else {
            url = api_base + "/files/" + file_id;
        }

        curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "DELETE");
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, ctx->headers);
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, &ctx->response);

        if (rag::is_verbose()) {
            curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
        }

        curl_multi_add_handle(multi, easy);
        active[easy] = {item.index, std::move(ctx)};

        rag::verbose_log("CURL", "Started " + std::string(item.removing_from_store ? "remove from store" : "delete file") +
                   " (attempt " + std::to_string(item.retry_count + 1) + "): " + file_id);
    };

    while (!pending.empty() && active.size() < current_max_parallel) {
        auto item = pending.front();
        pending.pop();
        start_delete(item);
    }

    int still_running = 1;
    while (still_running > 0 || !pending.empty()) {
        CURLMcode mc = curl_multi_perform(multi, &still_running);
        if (mc != CURLM_OK) {
            rag::verbose_err("CURL", std::string("curl_multi_perform failed: ") + curl_multi_strerror(mc));
            break;
        }

        int msgs_left;
        CURLMsg* msg;
        while ((msg = curl_multi_info_read(multi, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                CURL* easy = msg->easy_handle;
                auto it = active.find(easy);
                if (it == active.end()) continue;

                size_t idx = it->second.first;
                auto& ctx = it->second.second;

                curl_multi_remove_handle(multi, easy);
                curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &ctx->http_code);

                bool should_retry = false;
                bool is_not_found = false;
                std::string error_msg;

                CURLcode res = msg->data.result;
                if (res != CURLE_OK) {
                    error_msg = curl_easy_strerror(res);
                    should_retry = true;
                } else if (ctx->http_code == 429) {
                    error_msg = "Rate limited (HTTP 429)";
                    should_retry = true;
                    if (current_max_parallel > 1) {
                        current_max_parallel = current_max_parallel / 2;
                        rag::verbose_log("CURL", "Rate limited, reducing parallelism to " + std::to_string(current_max_parallel));
                    }
                } else if (ctx->http_code >= 500) {
                    error_msg = "Server error (HTTP " + std::to_string(ctx->http_code) + ")";
                    should_retry = true;
                } else if (ctx->http_code == 404 || ctx->http_code == 400) {
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
                        is_not_found = true;
                    }
                } else if (ctx->http_code >= 400) {
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

                curl_slist_free_all(ctx->headers);
                curl_easy_cleanup(easy);
                active.erase(it);

                if (http_code == 200 || is_not_found) {
                    if (was_removing_from_store) {
                        pending.push({idx, 0, false});
                        rag::verbose_log("CURL", "Removed from store: " + file_id + (is_not_found ? " (was not found)" : ""));
                    } else {
                        completed++;
                        if (on_progress) {
                            on_progress(completed, file_ids.size());
                        }
                        rag::verbose_log("CURL", "Deleted file: " + file_id + (is_not_found ? " (was not found)" : ""));
                    }
                } else if (should_retry && retry_count < MAX_RETRIES) {
                    rag::verbose_log("CURL", "Retrying (" + std::to_string(retry_count + 1) + "/" +
                               std::to_string(MAX_RETRIES) + "): " + file_id + " - " + error_msg);
                    pending.push({idx, retry_count + 1, was_removing_from_store});
                } else {
                    results[idx].error = error_msg;
                    completed++;
                    if (on_progress) {
                        on_progress(completed, file_ids.size());
                    }
                    rag::verbose_log("CURL", "Failed to delete: " + file_id + " - " + error_msg);
                }

                while (!pending.empty() && active.size() < current_max_parallel) {
                    auto item = pending.front();
                    pending.pop();
                    start_delete(item);
                }
            }
        }

        while (!pending.empty() && active.size() < current_max_parallel) {
            auto item = pending.front();
            pending.pop();
            start_delete(item);
            curl_multi_perform(multi, &still_running);
        }

        if (still_running > 0) {
            curl_multi_wait(multi, nullptr, 0, 100, nullptr);
        }
    }

    for (auto& [easy, pair] : active) {
        auto& ctx = pair.second;
        curl_multi_remove_handle(multi, easy);
        curl_slist_free_all(ctx->headers);
        curl_easy_cleanup(easy);
    }

    curl_multi_cleanup(multi);

    return results;
}

// ========== IKnowledgeStore ==========

std::string OpenAIProvider::create_store(const std::string& name) {
    std::string url = api_base_ + "/vector_stores";
    json body = {{"name", name}};

    std::string response = http_post_json(url, body);
    json j = json::parse(response);

    if (j.contains("error")) {
        throw std::runtime_error("Vector store creation failed: " + j["error"]["message"].get<std::string>());
    }

    return j.value("id", "");
}

void OpenAIProvider::delete_store(const std::string& store_id) {
    std::string url = api_base_ + "/vector_stores/" + store_id;

    std::string response = http_delete(url);
    json j = json::parse(response);

    if (j.contains("error")) {
        throw std::runtime_error("Failed to delete vector store: " + j["error"]["message"].get<std::string>());
    }
}

std::string OpenAIProvider::add_files(const std::string& store_id, const std::vector<std::string>& file_ids) {
    std::string url = api_base_ + "/vector_stores/" + store_id + "/file_batches";
    json body = {{"file_ids", file_ids}};

    std::string response = http_post_json(url, body);
    json j = json::parse(response);

    if (j.contains("error")) {
        throw std::runtime_error("File batch creation failed: " + j["error"]["message"].get<std::string>());
    }

    return j.value("id", "");
}

void OpenAIProvider::add_file(const std::string& store_id, const std::string& file_id) {
    std::string url = api_base_ + "/vector_stores/" + store_id + "/files";
    json body = {{"file_id", file_id}};

    std::string response = http_post_json(url, body);
    json j = json::parse(response);

    if (j.contains("error")) {
        throw std::runtime_error("Failed to add file to vector store: " + j["error"]["message"].get<std::string>());
    }
}

void OpenAIProvider::remove_file(const std::string& store_id, const std::string& file_id) {
    std::string url = api_base_ + "/vector_stores/" + store_id + "/files/" + file_id;

    std::string response = http_delete(url);
    json j = json::parse(response);

    if (j.contains("error")) {
        throw std::runtime_error("Failed to remove file from vector store: " + j["error"]["message"].get<std::string>());
    }
}

std::string OpenAIProvider::get_operation_status(const std::string& store_id, const std::string& operation_id) {
    std::string url = api_base_ + "/vector_stores/" + store_id + "/file_batches/" + operation_id;
    std::string response = http_get(url);

    json j = json::parse(response);
    return j.value("status", "unknown");
}

// ========== IChatService ==========

StreamResult OpenAIProvider::stream_response(
    const ChatConfig& config,
    const std::vector<Message>& conversation,
    OnTextCallback on_text,
    CancelCallback cancel_check
) {
    std::string url = api_base_ + "/responses";

    json input = json::array();
    if (!config.previous_response_id.empty() && !conversation.empty()) {
        for (auto it = conversation.rbegin(); it != conversation.rend(); ++it) {
            if (it->role == "user") {
                input.push_back(it->to_json());
                break;
            }
        }
    } else {
        for (const auto& msg : conversation) {
            input.push_back(msg.to_json());
        }
    }

    json body = {
        {"model", config.model},
        {"input", input},
        {"stream", true}
    };

    if (!config.knowledge_store_id.empty()) {
        body["tools"] = json::array({
            {
                {"type", "file_search"},
                {"vector_store_ids", json::array({config.knowledge_store_id})}
            }
        });
    }

    if (!config.reasoning_effort.empty()) {
        body["reasoning"] = {{"effort", config.reasoning_effort}};
    }

    if (!config.previous_response_id.empty()) {
        body["previous_response_id"] = config.previous_response_id;
    }

    std::string response_id;
    ResponseUsage usage;
    std::string stream_error;

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
            // Ignore malformed JSON in stream
        }
    }, cancel_check);

    if (!completed) {
        return StreamResult{"", {}, true};
    }

    if (!stream_error.empty()) {
        throw std::runtime_error(stream_error);
    }

    return StreamResult{response_id, usage, false};
}

StreamResult OpenAIProvider::stream_response(
    const ChatConfig& config,
    const nlohmann::json& input_window,
    OnTextCallback on_text,
    CancelCallback cancel_check
) {
    std::string url = api_base_ + "/responses";

    json input = json::array();
    if (!config.previous_response_id.empty() && input_window.is_array() && !input_window.empty()) {
        for (auto it = input_window.rbegin(); it != input_window.rend(); ++it) {
            if (it->contains("role") && (*it)["role"] == "user") {
                input.push_back(*it);
                break;
            }
        }
    } else {
        input = input_window;
    }

    json body = {
        {"model", config.model},
        {"input", input},
        {"stream", true}
    };

    if (!config.knowledge_store_id.empty()) {
        body["tools"] = json::array({
            {
                {"type", "file_search"},
                {"vector_store_ids", json::array({config.knowledge_store_id})}
            }
        });
    }

    if (!config.reasoning_effort.empty()) {
        body["reasoning"] = {{"effort", config.reasoning_effort}};
    }

    if (!config.previous_response_id.empty()) {
        body["previous_response_id"] = config.previous_response_id;
    }

    std::string response_id;
    ResponseUsage usage;
    std::string stream_error;

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
            // Ignore malformed JSON in stream
        }
    }, cancel_check);

    if (!completed) {
        return StreamResult{"", {}, true};
    }

    if (!stream_error.empty()) {
        throw std::runtime_error(stream_error);
    }

    return StreamResult{response_id, usage, false};
}

StreamResult OpenAIProvider::stream_response_with_tools(
    const ChatConfig& config,
    const std::vector<Message>& conversation,
    OnTextCallback on_text,
    OnToolCallCallback on_tool_call,
    CancelCallback cancel_check
) {
    std::string url = api_base_ + "/responses";

    json input = json::array();
    if (!config.previous_response_id.empty() && !conversation.empty()) {
        for (auto it = conversation.rbegin(); it != conversation.rend(); ++it) {
            if (it->role == "user") {
                input.push_back(it->to_json());
                break;
            }
        }
    } else {
        for (const auto& msg : conversation) {
            input.push_back(msg.to_json());
        }
    }

    json tools = json::array();
    if (!config.knowledge_store_id.empty()) {
        tools.push_back({
            {"type", "file_search"},
            {"vector_store_ids", json::array({config.knowledge_store_id})}
        });
    }

    for (const auto& tool : config.additional_tools) {
        tools.push_back(tool);
    }

    json body = {
        {"model", config.model},
        {"input", input},
        {"stream", true},
        {"tools", tools}
    };

    if (!config.reasoning_effort.empty()) {
        body["reasoning"] = {{"effort", config.reasoning_effort}};
    }

    std::string current_response_id = config.previous_response_id;
    if (!current_response_id.empty()) {
        body["previous_response_id"] = current_response_id;
    }

    ResponseUsage total_usage;

    while (true) {
        std::string response_id;
        ResponseUsage call_usage;
        std::string stream_error;

        struct PendingToolCall {
            std::string call_id;
            std::string name;
            std::string arguments;
        };
        std::vector<PendingToolCall> pending_tool_calls;

        std::string current_call_id;
        std::string current_tool_name;
        std::string current_arguments;

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
                            rag::verbose_log("MCP", "Tool call started: " + current_tool_name + " (id: " + current_call_id + ")");
                        }
                    }
                } else if (event_type == "response.function_call_arguments.delta") {
                    std::string delta = event.value("delta", "");
                    current_arguments += delta;
                } else if (event_type == "response.function_call_arguments.done") {
                    std::string call_id = event.value("call_id", current_call_id);
                    std::string name = event.value("name", current_tool_name);
                    std::string args_str = event.value("arguments", current_arguments);

                    rag::verbose_log("MCP", "Tool call complete: " + name + " args: " + args_str);

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
                // Ignore malformed JSON in stream
            }
        }, cancel_check);

        total_usage.input_tokens += call_usage.input_tokens;
        total_usage.output_tokens += call_usage.output_tokens;
        total_usage.reasoning_tokens += call_usage.reasoning_tokens;

        if (!completed) {
            return StreamResult{"", {}, true};
        }

        if (!stream_error.empty()) {
            throw std::runtime_error(stream_error);
        }

        if (pending_tool_calls.empty()) {
            return StreamResult{response_id, total_usage, false};
        }

        json tool_outputs = json::array();
        for (const auto& tc : pending_tool_calls) {
            std::string result;
            if (on_tool_call) {
                try {
                    json args = json::parse(tc.arguments.empty() ? "{}" : tc.arguments);
                    result = on_tool_call(tc.call_id, tc.name, args);
                } catch (const json::exception& e) {
                    rag::verbose_err("MCP", "Failed to parse tool arguments: " + std::string(e.what()));
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

            rag::verbose_log("MCP", "Tool " + tc.name + " returned: " + result);
        }

        current_response_id = response_id;
        body = {
            {"model", config.model},
            {"input", tool_outputs},
            {"stream", true},
            {"tools", tools},
            {"previous_response_id", current_response_id}
        };

        if (!config.reasoning_effort.empty()) {
            body["reasoning"] = {{"effort", config.reasoning_effort}};
        }

        rag::verbose_log("MCP", "Submitting tool results and continuing...");
    }
}

StreamResult OpenAIProvider::stream_response_with_tools(
    const ChatConfig& config,
    const nlohmann::json& input_window,
    OnTextCallback on_text,
    OnToolCallCallback on_tool_call,
    CancelCallback cancel_check
) {
    std::string url = api_base_ + "/responses";

    json input = json::array();
    if (!config.previous_response_id.empty() && input_window.is_array() && !input_window.empty()) {
        for (auto it = input_window.rbegin(); it != input_window.rend(); ++it) {
            if (it->contains("role") && (*it)["role"] == "user") {
                input.push_back(*it);
                break;
            }
        }
    } else {
        input = input_window;
    }

    json tools = json::array();
    if (!config.knowledge_store_id.empty()) {
        tools.push_back({
            {"type", "file_search"},
            {"vector_store_ids", json::array({config.knowledge_store_id})}
        });
    }

    for (const auto& tool : config.additional_tools) {
        tools.push_back(tool);
    }

    json body = {
        {"model", config.model},
        {"input", input},
        {"stream", true},
        {"tools", tools}
    };

    if (!config.reasoning_effort.empty()) {
        body["reasoning"] = {{"effort", config.reasoning_effort}};
    }

    std::string current_response_id = config.previous_response_id;

    ResponseUsage total_usage;

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
                        rag::verbose_log("MCP", "Response created with ID: " + response_id);
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
                            rag::verbose_log("MCP", "Tool call started: " + current_tool_name + " (id: " + current_call_id + ")");
                        }
                    }
                } else if (event_type == "response.function_call_arguments.delta") {
                    std::string delta = event.value("delta", "");
                    current_arguments += delta;
                } else if (event_type == "response.function_call_arguments.done") {
                    std::string call_id = event.value("call_id", current_call_id);
                    std::string name = event.value("name", current_tool_name);
                    std::string args_str = event.value("arguments", current_arguments);

                    rag::verbose_log("MCP", "Tool call complete: " + name + " args: " + args_str);
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
                // Ignore malformed JSON in stream
            }
        }, cancel_check);

        total_usage.input_tokens = call_usage.input_tokens;
        total_usage.output_tokens += call_usage.output_tokens;
        total_usage.reasoning_tokens += call_usage.reasoning_tokens;

        if (!completed) {
            return StreamResult{"", {}, true};
        }

        if (!stream_error.empty()) {
            throw std::runtime_error(stream_error);
        }

        current_response_id = response_id;

        if (pending_tool_calls.empty()) {
            return StreamResult{response_id, total_usage, false};
        }

        json tool_outputs = json::array();
        for (const auto& [call_id, name, args_str] : pending_tool_calls) {
            json args;
            try {
                args = json::parse(args_str);
            } catch (const json::exception&) {
                args = json::object();
            }

            rag::verbose_log("MCP", "Executing tool: " + name);
            std::string result = on_tool_call(call_id, name, args);
            rag::verbose_log("MCP", "Tool result: " + result.substr(0, 200) + (result.length() > 200 ? "..." : ""));

            tool_outputs.push_back({
                {"type", "function_call_output"},
                {"call_id", call_id},
                {"output", result}
            });
        }

        body = {
            {"model", config.model},
            {"input", tool_outputs},
            {"stream", true},
            {"tools", tools},
            {"previous_response_id", current_response_id}
        };

        if (!config.reasoning_effort.empty()) {
            body["reasoning"] = {{"effort", config.reasoning_effort}};
        }

        rag::verbose_log("MCP", "Submitting tool results and continuing...");
    }
}

std::optional<nlohmann::json> OpenAIProvider::compact_window(
    const std::string& model,
    const std::string& previous_response_id
) {
    std::string url = api_base_ + "/responses/compact";

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

} // namespace rag::providers::openai
