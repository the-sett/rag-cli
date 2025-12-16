#include "openai_client.hpp"
#include "config.hpp"
#include "verbose.hpp"
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

std::string OpenAIClient::stream_response(
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

    // Track response ID and errors during streaming.
    std::string response_id;
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

    // If cancelled, return empty string to indicate cancellation
    if (!completed) {
        return "";
    }

    // Throw if we encountered an error during streaming.
    if (!stream_error.empty()) {
        throw std::runtime_error(stream_error);
    }

    return response_id;
}

std::string OpenAIClient::stream_response_with_tools(
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

    // Loop to handle tool calls - after each tool call, we submit results and continue
    while (true) {
        // Track response ID, errors, and tool calls during streaming.
        std::string response_id;
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

        // If cancelled, return empty string to indicate cancellation
        if (!completed) {
            return "";
        }

        // Throw if we encountered an error during streaming.
        if (!stream_error.empty()) {
            throw std::runtime_error(stream_error);
        }

        // If no tool calls, we're done
        if (pending_tool_calls.empty()) {
            return response_id;
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

} // namespace rag
