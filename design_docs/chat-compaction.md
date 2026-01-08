Below is a concrete design for adding **token‑aware, 90% threshold compaction** to crag, using a **static model context table + fallback**, and the **OpenAI `/responses/compact` endpoint**.

I’ll describe:

- What behavior we want.
- What new types/constants to add.
- Exact files and code changes (by role) with brief rationale.

This is partly extrapolated from OpenAI’s Responses/Compact docs (not from this repo), but it’s consistent with crag’s existing structure: constants in `config.hpp`, settings in `settings.hpp/cpp`, and the HTTP client in `openai_client.hpp/cpp`.

---

## 0. Desired behavior (high‑level)

For every model interaction:

1. Build an **API input window** (an array of messages/items) representing the current conversation.
2. Call `POST /responses` with that window and stream the answer (as crag already does).
3. At the end of the call, read `usage.input_tokens`.
4. Look up `max_context_window` for the active model from a **static table**, or use a **fallback default** if not present.
5. If `usage.input_tokens > 0.9 * max_context_window`, call `POST /responses/compact` with the current input window:
   - Receive a **compacted input window** (with user messages + encrypted compaction item).
   - Replace the stored input window with this compacted version.
6. On the next turn, send the **compacted window** as the `input` to `/responses`.

Note: user‑visible logging stays based on the text messages (`ChatSession::conversation_`), while the **API window** can contain the opaque compaction item and is not shown to users.

---

## 1. Static model limits with fallback

### 1.1 Add constants + lookup table (`config.hpp/cpp`)

**File:** `src/config.hpp`  

Add after the existing API configuration section:

```cpp
// ========== Model Token Limits ==========

/**
 * Default maximum context window to use for models that are not
 * explicitly listed in MODEL_MAX_CONTEXT_TOKENS.
 *
 * This should be kept in sync with OpenAI model documentation.
 */
constexpr int DEFAULT_MAX_CONTEXT_TOKENS = 128000;

/**
 * Static table of known models and their max context windows.
 *
 * This is used to compute how "full" the context window is and when
 * to trigger compaction.
 */
inline const std::unordered_map<std::string, int> MODEL_MAX_CONTEXT_TOKENS = {
    // NOTE: These values are examples – update to match OpenAI docs.
    { "gpt-4o-2024-08-06", 128000 },
    { "gpt-4o-mini",       128000 },
    { "gpt-4.1-mini",      128000 },
    { "gpt-4.1",           128000 },
    // Add other commonly used models here.
};

/**
 * Returns the max context window for a model, or DEFAULT_MAX_CONTEXT_TOKENS
 * if the model is not in the static table.
 */
inline int get_max_context_tokens_for_model(const std::string& model) {
    auto it = MODEL_MAX_CONTEXT_TOKENS.find(model);
    if (it != MODEL_MAX_CONTEXT_TOKENS.end()) {
        return it->second;
    }
    return DEFAULT_MAX_CONTEXT_TOKENS;
}
```

**Why:**  

- Keeps all global constants with existing API config and maps.
- Provides both a **specific per‑model** limit and a **fallback**.

You don’t need a new `.cpp` file; the table and helper are `inline` as with `THINKING_MAP`.

---

## 2. Capture token usage from Responses API

We need the OpenAI client to expose `usage.input_tokens` etc from each `/responses` call.

### 2.1 Add a usage struct & result type (`openai_client.hpp`)

**File:** `src/openai_client.hpp`  

Under the `Message` struct, add:

```cpp
/**
 * Token usage information for a single Responses API call.
 */
struct ResponseUsage {
    int input_tokens = 0;      // Tokens in the prompt window.
    int output_tokens = 0;     // Tokens generated in the answer.
    int reasoning_tokens = 0;  // For reasoning models; 0 otherwise.
};

/**
 * Result of a streaming Responses call.
 *
 * Includes the response ID for continuation and token usage.
 */
struct StreamResult {
    std::string response_id; // Response ID returned by the API.
    ResponseUsage usage;     // Token usage reported by the API.
};
```

### 2.2 Change `stream_response` signature to return `StreamResult`

Locate the existing declaration (partial view):

```cpp
// Streams a response from the model with file_search tool enabled.
// ...
std::string stream_response(
    const std::string& model,
    const std::vector<Message>& conversation,
    const std::string& vector_store_id,
    const std::string& reasoning_effort,
    const std::string& previous_response_id,
    std::function<void(const std::string&)> on_text,
    CancelCallback cancel_check = nullptr
);
```

Change it to:

```cpp
// Streams a response from the model with file_search tool enabled.
// The on_text callback is invoked for each text delta received.
// If previous_response_id is provided, continues an existing conversation.
// The cancel_check callback is polled to allow cancellation (optional).
// Returns the new response ID and token usage for the call.
StreamResult stream_response(
    const std::string& model,
    const nlohmann::json& input,        // Full input window (array of items).
    const std::string& vector_store_id,
    const std::string& reasoning_effort,
    const std::string& previous_response_id,
    std::function<void(const std::string&)> on_text,
    CancelCallback cancel_check = nullptr
);
```

Key changes:

- `conversation` → generic `nlohmann::json input` (must be an array) so we can pass:
  - Simple `{role, content}` messages and
  - Compaction items and other non‑message items.
- Return type becomes `StreamResult` with usage.

You’ll need to similarly adjust the MCP‑tools‑enabled variant of `stream_response` (further down in the same header) to also return a `StreamResult` and accept `nlohmann::json input` instead of `std::vector<Message>`.

**Why:**  

- `/responses/compact` returns an **arbitrary item array**, not just `{role, content}`. We need to be able to pass that straight back in the next `/responses` call.
- We must surface `usage` to trigger compaction later.

### 2.3 Implement usage parsing in `openai_client.cpp`

**File:** `src/openai_client.cpp` (not shown here but listed in README)  

In the implementation of `OpenAIClient::stream_response`:

1. Build the `/responses` request body using the passed `input` JSON array:

   ```cpp
   nlohmann::json body = {
       { "model", model },
       { "input", input },
       { "temperature", 0.0 }, // or whatever you use now
       // include tools / file_search config / reasoning_effort as today
   };
   ```

2. Stream events from the Responses API as you already do for text deltas (using libcurl + SSE / streaming JSON).
3. Capture the **final JSON event** (e.g. `response.completed`) and parse:

   ```cpp
   ResponseUsage usage;
   if (event.contains("response") && event["response"].contains("usage")) {
       auto u = event["response"]["usage"];
       if (u.contains("input_tokens"))    usage.input_tokens = u["input_tokens"].get<int>();
       if (u.contains("output_tokens"))   usage.output_tokens = u["output_tokens"].get<int>();
       if (u.contains("reasoning_tokens")) usage.reasoning_tokens = u["reasoning_tokens"].get<int>();
   }

   std::string response_id;
   if (event.contains("response") && event["response"].contains("id")) {
       response_id = event["response"]["id"].get<std::string>();
   }
   ```

4. Return:

   ```cpp
   return StreamResult{ response_id, usage };
   ```

Keep existing verbose logging via `verbose_in/verbose_out` where helpful.

**Why:**  

- This is where we actually see the `usage` block from the API.
- We centralize all parsing here; callers only see a `StreamResult`.

---

## 3. Add a `/responses/compact` helper

We need a method that:

- Accepts the **current input window** (JSON array).
- Calls `/responses/compact`.
- Returns the **compacted input window** (JSON array) to use on the next turn.

### 3.1 New method in `OpenAIClient` interface

**File:** `openai_client.hpp`  

Add in the “Responses API” section, after the streaming methods:

```cpp
    // Compacts a long-running conversation window.
    // Sends the full input window to the /responses/compact endpoint and
    // returns the compacted window that should be used as input for the
    // next /responses call.
    //
    // The input must still fit within the model's max context size.
    nlohmann::json compact_window(
        const std::string& model,
        const nlohmann::json& input
    );
```

### 3.2 Implement `compact_window` in `openai_client.cpp`

**File:** `openai_client.cpp`  

Implementation sketch:

```cpp
nlohmann::json OpenAIClient::compact_window(
    const std::string& model,
    const nlohmann::json& input
) {
    nlohmann::json body = {
        { "model", model },
        { "input", input }
        // Optionally: { "instructions", "..."} if you also pass instructions to /responses
    };

    // Build HTTP POST to `${OPENAI_API_BASE}/responses/compact`
    // using the same internal HTTP helper you use for non-streaming calls.
    std::string url = std::string(OPENAI_API_BASE) + "/responses/compact";

    std::string response_str = http_post_json(url, body);  // whatever helper you have
    auto j = nlohmann::json::parse(response_str);

    if (j.contains("error")) {
        throw std::runtime_error("OpenAI compact error: " + j["error"]["message"].get<std::string>());
    }

    if (!j.contains("input")) {
        throw std::runtime_error("OpenAI compact error: missing 'input' field");
    }

    return j["input"];
}
```

(Replace `http_post_json` with your existing internal helper used for non‑streaming OpenAI calls.)

**Why:**  

- Encapsulates `/responses/compact` so upper layers don’t think about HTTP details.
- Keeps usage of `OPENAI_API_BASE` consistent with `openai_client.cpp`.

---

## 4. Track the API input window per chat

We need to distinguish:

- **Human‑visible history** = `std::vector<Message> conversation_` (for logs & UI)
- **API input window** = `nlohmann::json input_window_` (can contain compaction item, tool calls, etc.)

### 4.1 Extend `ChatSession` to store the API window

**File:** `src/chat.hpp`  

Add include for JSON at the top:

```cpp
#include <nlohmann/json.hpp>
```

In the private section of `ChatSession` add:

```cpp
    nlohmann::json api_window_;  // Current input window used for OpenAI Responses API.
```

Add public accessors:

```cpp
    // Returns the current API input window (array of items) for this chat.
    const nlohmann::json& get_api_window() const { return api_window_; }

    // Replaces the API input window (used after compaction).
    void set_api_window(const nlohmann::json& window) { api_window_ = window; }
```

Initialize `api_window_` in the constructor (in `chat.cpp`):

- When the session is created, you probably add an initial system prompt and possibly an intro user message.
- Update constructor logic so that whenever you add the **first real user message**, you also build an initial `api_window_`:

  ```cpp
  // Pseudocode inside ChatSession::add_user_message or in constructor where first user message is added:
  if (api_window_.is_null() || !api_window_.is_array()) {
      api_window_ = nlohmann::json::array();
      // If you use a system prompt, include it as a system message item.
      if (!system_prompt_.empty()) {
          api_window_.push_back({
              { "role", "system" },
              { "content", system_prompt_ }
          });
      }
  }

  api_window_.push_back({
      { "role", "user" },
      { "content", content }
  });
  ```

Similarly, in `add_assistant_message`, append to both:

```cpp
void ChatSession::add_assistant_message(const std::string& content) {
    // existing logic: append to conversation_, log, etc.
    conversation_.push_back({ "assistant", content });
    log("assistant", content);

    // Keep API window in sync (for non-compaction cases).
    api_window_.push_back({
        { "role", "assistant" },
        { "content", content }
    });
}
```

**Why:**  

- `conversation_` stays simple for logs and UI.
- `api_window_` is the exact array you pass to `/responses` and `/responses/compact`, and can safely hold compaction items.

---

## 5. Decide when to compact (90% threshold)

We’ll centralize the logic in a small helper function that has:

- `model` (string)
- `usage.input_tokens` (int)
- Access to `ChatSession` (for the window).

This logic belongs in the part of the code that orchestrates **one turn** with OpenAI:

- In the CLI chat loop (`main.cpp` + `chat.cpp`).
- In server mode (`web_server.cpp`).
- In MCP server context (`mcp_server.cpp`).

I suggest putting a helper in `chat.cpp` where you already interact with `ChatSession` and `OpenAIClient`.

### 5.1 Add helper to maybe compact

**File:** `src/chat.cpp` (implementation of `ChatSession` and chat flow)  

Add a free or static helper:

```cpp
static void maybe_compact_chat_window(
    OpenAIClient& client,
    ChatSession& session,
    const std::string& model,
    const ResponseUsage& usage
) {
    // Compute fullness based on input tokens (prompt window size).
    const int max_ctx = get_max_context_tokens_for_model(model);
    const double fullness = max_ctx > 0
        ? static_cast<double>(usage.input_tokens) / static_cast<double>(max_ctx)
        : 0.0;

    if (fullness <= 0.9) {
        return; // Under 90% - no compaction needed.
    }

    const nlohmann::json& current_window = session.get_api_window();
    if (!current_window.is_array()) {
        return; // Defensive - nothing to compact.
    }

    // Call /responses/compact to shrink the window.
    nlohmann::json compacted = client.compact_window(model, current_window);

    // Replace the window in the session.
    session.set_api_window(compacted);
}
```

**Why:**  

- Single place to encapsulate the “>90% → compact” rule.
- Keeps compaction logic out of CLI/HTTP specifics.

### 5.2 Use `api_window_` + `maybe_compact_chat_window` when calling OpenAI

In all locations that currently do something like:

```cpp
std::vector<Message> conversation = session.get_conversation();
std::string response_id = client.stream_response(
    model,
    conversation,
    vector_store_id,
    reasoning_effort,
    session.get_openai_response_id(),
    on_text,
    cancel_check
);
session.set_openai_response_id(response_id);
```

Change to:

```cpp
nlohmann::json input_window = session.get_api_window();

StreamResult result = client.stream_response(
    model,
    input_window,
    vector_store_id,
    reasoning_effort,
    session.get_openai_response_id(),
    on_text,
    cancel_check
);

// Save response ID for continuation.
session.set_openai_response_id(result.response_id);

// Update api_window_ with the new assistant message that we just streamed.
// (You already have the streamed text in on_text; you’ll likely accumulate it
//  into a std::string 'assistant_text' during streaming.)
session.add_assistant_message(assistant_text);
// add_assistant_message will append to api_window_ as shown earlier.

// Now decide whether to compact.
maybe_compact_chat_window(client, session, model, result.usage);
```

You’ll need to:

- Accumulate the full assistant text during streaming (you already do something like this for logging/markdown rendering).
- Call `add_assistant_message` *after* streaming completes so both the logs and `api_window_` are updated.

Apply this pattern in:

- CLI path (non‑interactive & interactive).
- `web_server.cpp` where you handle WebSocket chat turns.
- `mcp_server.cpp` when you answer a chat tool query via OpenAI Responses.

---

## 6. Handling unknown models (fallback)

The helper `get_max_context_tokens_for_model` already returns a **fallback default** for unknown model IDs.

In `maybe_compact_chat_window`:

- If you are worried about mis‑configured values, you can add safety:

  ```cpp
  if (max_ctx <= 0) return;
  ```

- You may also optionally log when the fallback is used (using `verbose_log` from `verbose.hpp`) to help you spot missing entries.

---

## 7. Summary of code changes (by file)

**`config.hpp`**  
- Add:
  - `DEFAULT_MAX_CONTEXT_TOKENS`.
  - `MODEL_MAX_CONTEXT_TOKENS` map.
  - `get_max_context_tokens_for_model(model)` helper.

**`openai_client.hpp`**  
- Add:
  - `struct ResponseUsage`.
  - `struct StreamResult`.
- Change:
  - `stream_response` (both variants) to:
    - Take `nlohmann::json input`.
    - Return `StreamResult`.
- Add:
  - `nlohmann::json compact_window(const std::string& model, const nlohmann::json& input);`.

**`openai_client.cpp`** (implementation, inferred from README)  
- Update `stream_response`:
  - Build body with `"input": input`.
  - Parse streaming events to pick up `usage` and `response.id`.
  - Return `StreamResult`.
- Implement `compact_window`:
  - POST to `${OPENAI_API_BASE}/responses/compact`.
  - Parse JSON, return `j["input"]`.

**`chat.hpp`**  
- Add:
  - `#include <nlohmann/json.hpp>`.
  - `nlohmann::json api_window_;` member.
  - `get_api_window` / `set_api_window` accessors.
- Ensure:
  - `add_user_message` and `add_assistant_message` update `api_window_` in sync with `conversation_`.

**`chat.cpp`**  
- Initialize `api_window_` when first user message is added (include system prompt if you use one).
- Implement `add_assistant_message` so it appends to both `conversation_` and `api_window_`.
- Add helper `maybe_compact_chat_window(OpenAIClient&, ChatSession&, const std::string&, const ResponseUsage&)` with 90% threshold logic.
- Update the main “send query” flow to:
  - Use `session.get_api_window()` as `input` to `stream_response`.
  - Use returned `StreamResult` (ID + usage).
  - Call `maybe_compact_chat_window` after updating the window with the new assistant message.

**`main.cpp`, `web_server.cpp`, `mcp_server.cpp`** (wherever `stream_response` is called)   

- Update calls to `OpenAIClient::stream_response` to:
  - Pass `session.get_api_window()` (or other JSON input) instead of `std::vector<Message>`.
  - Use `StreamResult result` instead of `std::string response_id`.
  - Pass `result.usage` into `maybe_compact_chat_window`.

---

This design gives you:

- A **static model limit table plus fallback**.
- A **90% threshold** for compaction based on real token usage.
- A clear separation between:
  - human‑visible history (`conversation_`),
  - and the **opaque, compactable API window** (`api_window_`).
- A single place (`maybe_compact_chat_window`) to tweak thresholds or behavior later.

