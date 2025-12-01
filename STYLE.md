# Code Style Guide

This document defines the coding conventions for the rag-cli project.

## Language Standard

- **C++17** is the target language standard
- Use modern C++ features where appropriate (std::optional, std::filesystem, etc.)
- Avoid C-style constructs unless interfacing with system APIs (e.g., CURL, termios)

## File Organization

### Header Files

- Use `#pragma once` for include guards
- Organize includes in this order:
  1. Standard library headers (`<string>`, `<vector>`, etc.)
  2. Third-party headers (`<nlohmann/json.hpp>`, `<curl/curl.h>`, etc.)
  3. Project headers (`"config.hpp"`, `"console.hpp"`, etc.)
- All code within `namespace rag { }`

### Source Files

- `.cpp` files for implementations, `.hpp` for headers
- Include corresponding header first in `.cpp` files
- Then system headers, then third-party headers, then project headers

### Naming Conventions

- **Files**: snake_case (e.g., `markdown_renderer.hpp`, `openai_client.cpp`)
- **Classes**: PascalCase (e.g., `MarkdownRenderer`, `OpenAIClient`, `ChatSession`)
- **Functions/Methods**: snake_case (e.g., `load_settings`, `stream_response`, `add_user_message`)
- **Member variables**: snake_case with trailing underscore (e.g., `api_key_`, `colors_enabled_`, `conversation_`)
- **Local variables**: snake_case
- **Constants**: UPPER_SNAKE_CASE for macros, PascalCase for constexpr (e.g., `OPENAI_API_BASE`, `SETTINGS_FILE`)

### Types

#### Standard Library Types

Use standard library types appropriately:
- `size_t` for sizes and indices
- `int64_t` for timestamps
- `std::string` for text
- `std::optional<T>` for values that may not exist
- `std::vector<T>`, `std::map<K,V>` for containers
- `std::function<void(const std::string&)>` for callbacks

### Structures

- Use `struct` for plain data types with public members
- PascalCase for struct names
- Add doc comments explaining the struct's purpose

```cpp
/**
 * Metadata for a single indexed file.
 *
 * Tracks the OpenAI file ID and modification timestamp to enable incremental
 * updates when files change.
 */
struct FileMetadata {
    std::string openai_file_id;  // OpenAI file ID for this file.
    int64_t last_modified;       // Unix timestamp of last modification.
};
```

### Classes

#### Member Ordering

1. Public interface first
2. Private implementation details last
3. Within each section:
   - Static methods
   - Constructors/destructors
   - Regular methods
   - Member variables

#### Access Specifiers

```cpp
class Console {
public:
    // Public interface

private:
    // Private implementation
};
```

### Comments

Comments should be proper English sentences ending with a period. Be accurate and concise. Only comment things that a code reader should really have brought to their attention.

#### What to Comment

- **Class/struct purpose**: Every class and struct should have a doc comment explaining its role.
- **Public method purpose**: Every public method should have a comment describing what it does.
- **Field purpose**: Fields in data structures should have inline comments explaining their role.
- **Global/constant purpose**: Explain what globals and constants represent.
- **Non-obvious code**: Add inline comments where logic is not self-evident.
- **Pre-conditions**: Mention when a method requires certain conditions.

#### Class/Struct Documentation

Use `/** */` multiline format with `*` on continuation lines:

```cpp
/**
 * Terminal output helper with color support.
 *
 * Provides styled output methods that automatically handle ANSI color codes
 * based on terminal capabilities. Falls back to plain text when colors are
 * not supported (e.g., when TERM=dumb or output is not a TTY).
 */
class Console {
```

#### Method Comments

Place a single-line `//` comment immediately before each public method:

```cpp
    // Creates a Console instance and detects color support.
    Console();

    // Prints text without a trailing newline.
    void print(const std::string& text) const;

    // Prints error message in red.
    void print_error(const std::string& text) const;

    // Prompts the user for input with an optional default value.
    std::string prompt(const std::string& message, const std::string& default_value = "") const;
```

#### Field Comments

Use inline `//` comments after field declarations:

```cpp
    std::string api_key_;                // OpenAI API key.
    std::vector<Message> conversation_;  // In-memory conversation history.
    std::string log_path_;               // Path to the log file.
    bool colors_enabled_;                // True if terminal supports ANSI colors.
```

#### Section Headings

Use `// ========== Section Name ==========` to visually delineate areas of code:

```cpp
    // ========== Basic Output ==========

    void print(const std::string& text) const;
    void println(const std::string& text = "") const;

    // ========== Colored Output ==========

    void print_error(const std::string& text) const;
    void print_warning(const std::string& text) const;
    void print_success(const std::string& text) const;

    // ========== HTTP Helpers ==========

    std::string http_get(const std::string& url);
    std::string http_post_json(const std::string& url, const nlohmann::json& body);
```

#### File-Level Section Separators

Use decorative separators for major file sections:

```cpp
// ============================================================================
// Signal Handling
// ============================================================================

static Console* g_console = nullptr;
static std::atomic<bool>* g_stop_spinner = nullptr;
```

### Code Formatting

#### Indentation

- **4 spaces** per indentation level (no tabs)
- Continuation lines aligned appropriately

#### Braces

- Opening brace on same line for functions, control structures, and classes
- Closing brace on its own line

```cpp
void function() {
    if (condition) {
        // code
    }
}
```

#### Line Length

- Soft limit: 120 characters
- Hard limit: Avoid exceeding 140 characters
- Break long parameter lists across multiple lines

#### Spacing

- Space after keywords: `if (`, `while (`, `for (`
- Space around binary operators: `a + b`, `x = y`
- No space after unary operators: `!flag`, `++i`
- No space inside parentheses: `(expr)` not `( expr )`
- Space after commas: `foo(a, b, c)`

#### Initialization

Use constructor initialization lists:

```cpp
ChatSession::ChatSession(const std::string& system_prompt, const std::string& log_dir) :
    log_path_(generate_log_path(log_dir)) {
    // Constructor body
}
```

### Control Flow

#### Conditionals

Always use braces, even for single statements:

```cpp
if (condition) {
    statement();
}
```

Multi-line conditions align naturally:

```cpp
if (j.contains("error") &&
    j["error"].contains("message")) {
    // Handle error
}
```

#### Early Returns

Prefer early returns for error conditions:

```cpp
std::optional<Settings> load_settings() {
    if (!fs::exists(SETTINGS_FILE)) {
        return std::nullopt;
    }

    // Main logic
}
```

#### Switch Statements

```cpp
switch (choice) {
    case '1':
        return "low";
    case '2':
        return "medium";
    case '3':
        return "high";
    default:
        return "";
}
```

### Memory Management

#### Allocation

- Use RAII for resource management
- Use `std::unique_ptr<T>` for unique ownership
- Prefer stack allocation where possible

#### Thread Safety

- Use `std::mutex` and `std::lock_guard<std::mutex>` for synchronization
- Use `std::atomic<T>` for simple shared state
- Document threading requirements in comments

```cpp
std::atomic<bool> stop_spinner{false};
std::thread spinner_thread([&]() {
    while (!stop_spinner.load()) {
        // Animate spinner
    }
});
```

### Modern C++ Features

#### Auto

Use `auto` when type is obvious from context:

```cpp
auto it = THINKING_MAP.find(thinking);  // Iterator type obvious
auto settings = load_settings();         // Type in function name
```

Avoid `auto` when type clarity is important:

```cpp
std::string response = http_get(url);  // Not: auto response = http_get(url);
size_t count = files.size();           // Not: auto count = files.size();
```

#### Range-Based Loops

Prefer range-based for loops:

```cpp
for (const auto& msg : conversation) {
    input.push_back(msg.to_json());
}

for (const auto& pattern : patterns) {
    // Process pattern
}
```

#### Nullptr

Always use `nullptr`, never `NULL` or `0` for pointers:

```cpp
if (api_key == nullptr) { }
```

#### std::optional

Use `std::optional` for values that may not exist:

```cpp
std::optional<Settings> load_settings();

auto existing = load_settings();
if (existing.has_value() && existing->is_valid()) {
    // Use settings
}
```

### Const Correctness

- Mark methods `const` when they don't modify state
- Use `const &` for read-only parameters of non-trivial types
- Use `const` for local variables that won't change

```cpp
void print(const std::string& text) const;
const std::vector<Message>& get_conversation() const;
```

### Callbacks and Lambdas

Use `std::function` for callbacks:

```cpp
void stream_response(
    const std::string& model,
    const std::vector<Message>& conversation,
    std::function<void(const std::string&)> on_text
);
```

Use lambdas for inline callbacks:

```cpp
client.stream_response(model, conversation, [&](const std::string& delta) {
    renderer.feed(delta);
    streamed_text += delta;
});
```

### Preprocessor

#### Macros

- Minimize macro usage
- Use `constexpr` instead of `#define` for constants
- Use `inline` functions instead of function macros

Acceptable macro uses:
- Include guards (`#pragma once`)
- Platform-specific code

### Error Handling

- Use exceptions for API failures (`std::runtime_error`)
- Return `std::optional` or empty values for expected missing data
- Use early returns to handle error conditions

```cpp
if (j.contains("error")) {
    throw std::runtime_error("API error: " + j["error"]["message"].get<std::string>());
}
```

### Documentation

See the **Comments** section above for detailed guidance on writing comments. Key principles:

- All comments are proper English sentences ending with periods.
- Every class/struct has a `/** */` doc comment explaining its purpose.
- Every public method has a `//` comment describing what it does.
- Fields have inline `//` comments explaining their role.
- Use `// ========== Section ==========` headings to organize code.

### Testing Conventions

#### Unit Tests with Catch2

- Use descriptive test case names
- Group related tests with tags
- Use section separators for test organization

```cpp
TEST_CASE("Plain text passes through", "[markdown]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("Hello world");
    renderer.finish();

    REQUIRE(strip_ansi(output.result).find("Hello world") != std::string::npos);
}

TEST_CASE("ATX heading level 1", "[markdown][headings]") {
    // Test implementation
}
```

#### Test Organization

Use section separators in test files:

```cpp
// ============================================================================
// Basic text rendering
// ============================================================================

TEST_CASE("Plain text passes through", "[markdown]") { ... }

// ============================================================================
// Code blocks
// ============================================================================

TEST_CASE("Fenced code block with language", "[markdown][code]") { ... }
```

## Tools

### Recommended

- **clang-format**: Automatic code formatting (configuration to be added)
- **clang-tidy**: Static analysis and linting
- **cmake**: Build system
- **ninja**: Fast build tool

### Build Commands

```bash
mkdir build && cd build
cmake ..
ninja
./test_markdown_renderer  # Run tests
```
