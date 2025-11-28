# RAG CLI: Python to C++ Conversion Plan

## 1. Overview

Convert the RAG CLI tool from Python to C++17/20, maintaining feature parity with console color highlighting, using CMake with Ninja and Clang as specified in the existing CMakePresets.json.

---

## 2. Project Structure (Proposed)

```
rag-cli/
├── CMakeLists.txt                 # Main build configuration
├── CMakePresets.json              # Existing presets (Ninja + Clang + lld)
├── src/
│   ├── main.cpp                   # Entry point, CLI parsing
│   ├── config.hpp                 # Configuration constants
│   ├── config.cpp
│   ├── settings.hpp               # Settings load/save (JSON)
│   ├── settings.cpp
│   ├── console.hpp                # Terminal colors/formatting (Rich equivalent)
│   ├── console.cpp
│   ├── file_resolver.hpp          # Glob pattern resolution
│   ├── file_resolver.cpp
│   ├── openai_client.hpp          # OpenAI API client (HTTP + JSON)
│   ├── openai_client.cpp
│   ├── vector_store.hpp           # Vector store management
│   ├── vector_store.cpp
│   ├── chat.hpp                   # Chat/conversation management
│   └── chat.cpp
├── include/                       # Third-party headers (if header-only)
└── extern/                        # Git submodules or vendored deps
```

---

## 3. Dependencies

### Required Libraries

| Library | Purpose | Integration Method |
|---------|---------|-------------------|
| **nlohmann/json** | JSON parsing/serialization | FetchContent or find_package |
| **libcurl** | HTTP requests to OpenAI API | find_package (system) |
| **fmt** | Text formatting (optional, can use std::format in C++20) | FetchContent |

### Console Coloring Strategy

For terminal colors, we have options:

1. **ANSI escape codes directly** (simplest, no dependency)
   - Works on Linux/macOS natively
   - Windows 10+ supports ANSI in modern terminals

2. **fmt library with terminal support**
   - `fmt::print(fg(fmt::color::red), "Error: {}", msg)`

**Recommendation:** Use ANSI escape codes directly for minimal dependencies, wrapped in a simple `Console` class that mirrors the Python `rich` functionality we actually use.

---

## 4. Component Mapping (Python → C++)

### 4.1 Configuration (`config.hpp/cpp`)

```cpp
// Constants
constexpr const char* SETTINGS_FILE = "settings.json";
constexpr const char* LOG_DIR = "chat_logs";

// Thinking level map
const std::unordered_map<char, std::string> THINKING_MAP = {
    {'l', "low"}, {'m', "medium"}, {'h', "high"}
};

// Supported file extensions
const std::vector<std::string> SUPPORTED_EXTENSIONS = {
    ".txt", ".md", ".pdf", /* ... full list ... */
};
```

### 4.2 Settings Management (`settings.hpp/cpp`)

```cpp
struct Settings {
    std::string model;
    std::string reasoning_effort;
    std::string vector_store_id;
    std::vector<std::string> file_patterns;
};

Settings load_settings();
void save_settings(const Settings& settings);
```

**Implementation:** Use nlohmann/json for JSON read/write.

### 4.3 Console Output (`console.hpp/cpp`)

```cpp
class Console {
public:
    void print(const std::string& text);
    void print_colored(const std::string& text, Color color, Style style = Style::Normal);
    void print_success(const std::string& text);   // Green checkmark
    void print_warning(const std::string& text);   // Yellow
    void print_error(const std::string& text);     // Red
    void print_info(const std::string& text);      // Cyan

    // Status spinner (simplified - just print status line)
    void start_status(const std::string& message);
    void end_status();

    // Prompt for input
    std::string prompt(const std::string& message, const std::string& default_value = "");
};
```

**ANSI Color Codes:**
```cpp
namespace ansi {
    constexpr const char* RESET = "\033[0m";
    constexpr const char* BOLD = "\033[1m";
    constexpr const char* RED = "\033[31m";
    constexpr const char* GREEN = "\033[32m";
    constexpr const char* YELLOW = "\033[33m";
    constexpr const char* CYAN = "\033[36m";
}
```

### 4.4 File Resolution (`file_resolver.hpp/cpp`)

```cpp
std::vector<std::string> resolve_file_patterns(
    const std::vector<std::string>& patterns,
    Console& console
);

bool is_supported_extension(const std::string& filepath);
```

**Implementation Options:**
1. Use `<filesystem>` (C++17) for directory traversal
2. Use `glob()` from POSIX or a cross-platform glob library
3. Implement simple glob matching manually with `std::filesystem`

**Recommendation:** Use `std::filesystem` with manual glob pattern matching (wildcard expansion).

### 4.5 OpenAI API Client (`openai_client.hpp/cpp`)

```cpp
class OpenAIClient {
public:
    explicit OpenAIClient(const std::string& api_key);

    // Models API
    std::vector<std::string> list_models();

    // Files API
    std::string upload_file(const std::string& filepath);

    // Vector Stores API
    std::string create_vector_store(const std::string& name);
    std::string create_file_batch(const std::string& vector_store_id,
                                   const std::vector<std::string>& file_ids);
    std::string get_batch_status(const std::string& vector_store_id,
                                  const std::string& batch_id);

    // Responses API (streaming)
    void stream_response(
        const std::string& model,
        const std::vector<Message>& conversation,
        const std::string& vector_store_id,
        const std::string& reasoning_effort,
        std::function<void(const std::string&)> on_text,
        std::function<void(const json&)> on_file_search_result
    );

private:
    std::string api_key_;
    // libcurl handle management
};
```

**HTTP Implementation:**
- Use libcurl for HTTP requests
- Set appropriate headers: `Authorization: Bearer <key>`, `Content-Type: application/json`
- Handle multipart/form-data for file uploads
- Implement SSE (Server-Sent Events) parsing for streaming responses

### 4.6 Vector Store Management (`vector_store.hpp/cpp`)

```cpp
std::string create_vector_store(
    const std::vector<std::string>& file_patterns,
    OpenAIClient& client,
    Console& console
);

Settings load_or_create_settings(
    const CliArgs& args,
    OpenAIClient& client,
    Console& console
);
```

### 4.7 Chat Management (`chat.hpp/cpp`)

```cpp
struct Message {
    std::string role;    // "system", "user", "assistant"
    std::string content;
};

class ChatSession {
public:
    ChatSession(const std::string& system_prompt, const std::string& log_path);

    void add_user_message(const std::string& content);
    void add_assistant_message(const std::string& content);

    const std::vector<Message>& get_conversation() const;

private:
    std::vector<Message> conversation_;
    std::string log_path_;
    void log(const std::string& role, const std::string& text);
};
```

### 4.8 CLI Argument Parsing (`main.cpp`)

Options:
1. **Manual parsing** - Simple, no dependencies
2. **CLI11** - Modern C++ argument parser (header-only)
3. **cxxopts** - Lightweight option parser (header-only)

**Recommendation:** Use CLI11 (header-only, easy FetchContent integration).

```cpp
struct CliArgs {
    std::vector<std::string> files;
    bool reindex = false;
    bool strict = false;
    bool debug = false;
    char thinking = '\0';  // 'l', 'm', 'h', or '\0' for unset
    bool non_interactive = false;
};

int main(int argc, char* argv[]);
```

---

## 5. CMake Configuration

### CMakeLists.txt Structure

```cmake
cmake_minimum_required(VERSION 3.20)
project(rag-cli VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
include(FetchContent)

# nlohmann/json
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# CLI11
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(cli11)

# Find system libcurl
find_package(CURL REQUIRED)

# Source files
set(SOURCES
    src/main.cpp
    src/config.cpp
    src/settings.cpp
    src/console.cpp
    src/file_resolver.cpp
    src/openai_client.cpp
    src/vector_store.cpp
    src/chat.cpp
)

add_executable(rag-cli ${SOURCES})

target_include_directories(rag-cli PRIVATE src)
target_link_libraries(rag-cli PRIVATE
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    CURL::libcurl
)

# Install
install(TARGETS rag-cli RUNTIME DESTINATION bin)
```

---

## 6. Implementation Order

### Phase 1: Core Infrastructure
1. [ ] Set up CMakeLists.txt with dependencies
2. [ ] Implement `config.hpp/cpp` (constants)
3. [ ] Implement `console.hpp/cpp` (ANSI colors, output formatting)
4. [ ] Implement `settings.hpp/cpp` (JSON load/save)

### Phase 2: File Handling
5. [ ] Implement `file_resolver.hpp/cpp` (glob patterns, directory walking)

### Phase 3: OpenAI Integration
6. [ ] Implement `openai_client.hpp/cpp`:
   - Basic HTTP with libcurl
   - Models API
   - Files API (upload)
   - Vector Stores API
   - Responses API with streaming (SSE parsing)

### Phase 4: Application Logic
7. [ ] Implement `vector_store.hpp/cpp` (upload + index flow)
8. [ ] Implement `chat.hpp/cpp` (conversation, logging)
9. [ ] Implement `main.cpp`:
   - CLI argument parsing
   - Interactive/non-interactive modes
   - Main application flow

### Phase 5: Testing & Polish
10. [ ] Test all features against Python version
11. [ ] Handle edge cases and error conditions
12. [ ] Add signal handling (Ctrl+C graceful exit)

---

## 7. Technical Challenges & Solutions

### 7.1 Streaming HTTP Responses (SSE)

OpenAI's streaming API uses Server-Sent Events. With libcurl:

```cpp
size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    // Parse SSE format: "data: {...}\n\n"
    // Extract JSON, call appropriate handler
}

curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
```

### 7.2 Glob Pattern Matching

C++17 `std::filesystem` doesn't have glob. Options:
1. Use POSIX `glob()` (Linux/macOS only)
2. Implement simple glob matching with `*` and `**` patterns
3. Use a library like `glob` from Boost (heavy dependency)

**Solution:** Implement simple pattern matching:
- `*` matches any characters in filename
- `**` matches any path components recursively
- Use `std::filesystem::recursive_directory_iterator`

### 7.3 Multipart Form Data Upload

For file uploads, libcurl needs multipart form:

```cpp
curl_mime* mime = curl_mime_init(curl);
curl_mimepart* part = curl_mime_addpart(mime);
curl_mime_name(part, "file");
curl_mime_filedata(part, filepath.c_str());
curl_mime_name(part, "purpose");
curl_mime_data(part, "assistants", CURL_ZERO_TERMINATED);
curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
```

### 7.4 Cross-Platform Console Colors

Windows historically didn't support ANSI. Modern Windows 10+ does. Add detection:

```cpp
#ifdef _WIN32
#include <windows.h>
void enable_ansi_on_windows() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#endif
```

---

## 8. API Endpoints Reference

### Models API
```
GET https://api.openai.com/v1/models
Headers: Authorization: Bearer <key>
```

### Files API
```
POST https://api.openai.com/v1/files
Headers: Authorization: Bearer <key>
Body: multipart/form-data with file and purpose="assistants"
```

### Vector Stores API
```
POST https://api.openai.com/v1/vector_stores
Headers: Authorization: Bearer <key>, Content-Type: application/json
Body: {"name": "cli-rag-store"}

POST https://api.openai.com/v1/vector_stores/{vs_id}/file_batches
Body: {"file_ids": ["file-xxx", ...]}

GET https://api.openai.com/v1/vector_stores/{vs_id}/file_batches/{batch_id}
```

### Responses API (Streaming)
```
POST https://api.openai.com/v1/responses
Headers: Authorization: Bearer <key>, Content-Type: application/json
Body: {
    "model": "gpt-5-...",
    "input": [...messages...],
    "stream": true,
    "tools": [{"type": "file_search", "vector_store_ids": ["vs_..."]}],
    "reasoning": {"effort": "medium"}
}
```

---

## 9. Feature Parity Checklist

| Feature | Python | C++ |
|---------|--------|-----|
| CLI argument parsing | ✓ argparse | CLI11 |
| JSON settings load/save | ✓ json | nlohmann/json |
| Console colors (yellow, green, red, cyan) | ✓ rich | ANSI codes |
| Status spinner | ✓ rich.Status | Simple status line |
| Interactive prompts | ✓ rich.Prompt | std::cin with prompt |
| Glob pattern resolution | ✓ glob.glob | std::filesystem + pattern matching |
| File upload | ✓ openai.files.create | libcurl multipart |
| Vector store creation | ✓ openai.vector_stores | libcurl + JSON |
| Batch indexing with polling | ✓ openai polling | libcurl + sleep loop |
| Streaming responses | ✓ openai streaming | libcurl SSE parsing |
| Chat logging to markdown | ✓ file write | std::ofstream |
| Ctrl+C handling | ✓ KeyboardInterrupt | signal() handler |
| Non-interactive mode | ✓ stdin read | std::cin/getline |

---

## 10. Build & Run Commands

```bash
# Configure
cmake --preset ninja-clang-lld-linux

# Build
cmake --build --preset build

# Run
./build/rag-cli 'docs/*.md'

# Debug build
cmake --preset ninja-clang-lld-linux-debug
cmake --build --preset debug
```

---

## 11. Estimated Complexity

| Component | Lines of Code (est.) | Complexity |
|-----------|---------------------|------------|
| config | ~50 | Low |
| console | ~100 | Low |
| settings | ~80 | Low |
| file_resolver | ~150 | Medium |
| openai_client | ~400 | High |
| vector_store | ~150 | Medium |
| chat | ~100 | Low |
| main | ~200 | Medium |
| **Total** | **~1230** | |

---

## 12. Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| SSE parsing complexity | Test thoroughly with real API responses |
| libcurl memory management | Use RAII wrappers for curl handles |
| Cross-platform glob | Test on Linux first, add Windows support later |
| API changes | Use OpenAI API version headers |

---

## Approval Requested

Please review this plan and confirm:
1. Is the proposed project structure acceptable?
2. Are the chosen dependencies (nlohmann/json, CLI11, libcurl) acceptable?
3. Should I proceed with the ANSI escape code approach for colors, or prefer a library?
4. Any other concerns or modifications before implementation begins?
