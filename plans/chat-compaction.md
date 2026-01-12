# Chat Compaction Implementation Plan

Based on `design_docs/chat-compaction.md` and current codebase analysis.

## Overview

Implement token-aware 90% threshold compaction using OpenAI's `/responses/compact` endpoint.

## Current State Analysis

| File | Current | Needed |
|------|---------|--------|
| `config.hpp` | API base, thinking map, extensions | Add model token limits |
| `openai_client.hpp` | `stream_response` returns `std::string` | Return `StreamResult` with usage |
| `openai_client.cpp` | Parses `response.id` only | Parse `usage` block too |
| `chat.hpp` | `conversation_` only | Add `api_window_` (JSON) |
| `chat.cpp` | Updates `conversation_` | Sync `api_window_`, add compaction |

## Implementation Steps

### Step 1: Add Model Token Limits (`config.hpp`)

Add after the API configuration section:

```cpp
constexpr int DEFAULT_MAX_CONTEXT_TOKENS = 128000;

inline const std::unordered_map<std::string, int> MODEL_MAX_CONTEXT_TOKENS = {
    { "gpt-4o-2024-08-06", 128000 },
    { "gpt-4o-mini",       128000 },
    { "gpt-4.1-mini",      1047576 },
    { "gpt-4.1",           1047576 },
    { "o3",                200000 },
    { "o4-mini",           200000 },
};

inline int get_max_context_tokens_for_model(const std::string& model) {
    auto it = MODEL_MAX_CONTEXT_TOKENS.find(model);
    return (it != MODEL_MAX_CONTEXT_TOKENS.end()) ? it->second : DEFAULT_MAX_CONTEXT_TOKENS;
}
```

### Step 2: Add Usage Structs (`openai_client.hpp`)

Add after `Message` struct:

```cpp
struct ResponseUsage {
    int input_tokens = 0;
    int output_tokens = 0;
    int reasoning_tokens = 0;
};

struct StreamResult {
    std::string response_id;
    ResponseUsage usage;
};
```

### Step 3: Modify `stream_response` Signatures (`openai_client.hpp`)

Change both `stream_response` and `stream_response_with_tools`:
- Return type: `std::string` -> `StreamResult`
- Add optional `nlohmann::json input` parameter for pre-built input (for compacted windows)

New signatures:
```cpp
StreamResult stream_response(
    const std::string& model,
    const std::vector<Message>& conversation,
    const std::string& vector_store_id,
    const std::string& reasoning_effort,
    const std::string& previous_response_id,
    std::function<void(const std::string&)> on_text,
    CancelCallback cancel_check = nullptr,
    const nlohmann::json& input_override = nlohmann::json()  // For compacted windows
);

StreamResult stream_response_with_tools(...same pattern...);
```

### Step 4: Parse Usage in `openai_client.cpp`

In the streaming event handler, capture `response.completed` event:

```cpp
} else if (event_type == "response.completed") {
    if (event.contains("response") && event["response"].contains("usage")) {
        auto u = event["response"]["usage"];
        if (u.contains("input_tokens")) usage.input_tokens = u["input_tokens"].get<int>();
        if (u.contains("output_tokens")) usage.output_tokens = u["output_tokens"].get<int>();
        // reasoning_tokens if present
    }
}
```

Return `StreamResult{response_id, usage}` instead of `response_id`.

### Step 5: Add `compact_window` Method (`openai_client.hpp/cpp`)

Declaration:
```cpp
nlohmann::json compact_window(const std::string& model, const nlohmann::json& input);
```

Implementation:
```cpp
nlohmann::json OpenAIClient::compact_window(const std::string& model, const nlohmann::json& input) {
    std::string url = std::string(OPENAI_API_BASE) + "/responses/compact";
    json body = {{"model", model}, {"input", input}};

    std::string response_str = http_post_json(url, body);
    auto j = json::parse(response_str);

    if (j.contains("error")) {
        throw std::runtime_error("Compact error: " + j["error"]["message"].get<std::string>());
    }
    if (!j.contains("input")) {
        throw std::runtime_error("Compact error: missing 'input' field");
    }
    return j["input"];
}
```

### Step 6: Add API Window to `ChatSession` (`chat.hpp/cpp`)

In `chat.hpp`:
```cpp
private:
    nlohmann::json api_window_;  // Current input window for API calls

public:
    const nlohmann::json& get_api_window() const { return api_window_; }
    void set_api_window(const nlohmann::json& window) { api_window_ = window; }
```

Update `add_user_message` and `add_assistant_message` to sync `api_window_`.

### Step 7: Add Compaction Helper (`chat.cpp`)

```cpp
static void maybe_compact_chat_window(
    OpenAIClient& client,
    ChatSession& session,
    const std::string& model,
    const ResponseUsage& usage
) {
    const int max_ctx = get_max_context_tokens_for_model(model);
    if (max_ctx <= 0) return;

    const double fullness = static_cast<double>(usage.input_tokens) / max_ctx;
    if (fullness <= 0.9) return;

    const nlohmann::json& window = session.get_api_window();
    if (!window.is_array()) return;

    nlohmann::json compacted = client.compact_window(model, window);
    session.set_api_window(compacted);
}
```

### Step 8: Update Call Sites

**main.cpp** (two calls):
```cpp
StreamResult result = client.stream_response(...);
chat.set_openai_response_id(result.response_id);
// After adding assistant message:
maybe_compact_chat_window(client, chat, settings.model, result.usage);
```

**websocket_server.cpp**:
```cpp
StreamResult result = client_.stream_response_with_tools(...);
session->set_openai_response_id(result.response_id);
maybe_compact_chat_window(client_, *session, model_, result.usage);
```

**mcp_server.cpp**:
```cpp
StreamResult result = client_.stream_response(...);
session->set_openai_response_id(result.response_id);
maybe_compact_chat_window(client_, *session, model_, result.usage);
```

## File Change Summary

| File | Changes |
|------|---------|
| `config.hpp` | +20 lines (model limits) |
| `openai_client.hpp` | +15 lines (structs), ~4 signature changes |
| `openai_client.cpp` | ~30 lines (usage parsing, compact_window impl) |
| `chat.hpp` | +5 lines (api_window_ member + accessors) |
| `chat.cpp` | ~40 lines (sync api_window_, compaction helper) |
| `main.cpp` | ~10 lines (handle StreamResult, call compaction) |
| `websocket_server.cpp` | ~5 lines |
| `mcp_server.cpp` | ~5 lines |

## Testing

1. Run existing tests to ensure no regression
2. Manual test with long conversation to trigger 90% threshold
3. Verify compacted window is used in subsequent calls
