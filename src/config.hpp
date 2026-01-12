#pragma once

/**
 * Application configuration constants.
 *
 * Defines file paths, API settings, and supported file types for the crag CLI.
 */

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rag {

// ========== File Paths ==========

constexpr const char* SETTINGS_FILE = ".crag.json";  // Local settings file.
constexpr const char* LOG_DIR = "chat_logs";         // Directory for chat logs.

// ========== Thinking Level Shortcuts ==========

// Maps command-line shorthand to OpenAI reasoning effort levels.
inline const std::unordered_map<char, std::string> THINKING_MAP = {
    {'l', "low"},
    {'m', "medium"},
    {'h', "high"}
};

// ========== Supported File Extensions ==========

// File extensions supported by OpenAI's file_search tool.
inline const std::unordered_set<std::string> SUPPORTED_EXTENSIONS = {
    // Documents
    ".txt", ".md", ".pdf", ".doc", ".docx", ".pptx", ".html", ".htm",
    // Data formats
    ".json", ".xml", ".csv", ".tsv", ".yaml", ".yml",
    // Programming languages
    ".py", ".js", ".ts", ".jsx", ".tsx", ".java", ".c", ".cpp", ".h", ".hpp",
    ".cs", ".go", ".rs", ".rb", ".php", ".swift", ".kt", ".scala", ".r",
    ".sh", ".bash", ".zsh", ".ps1", ".bat", ".cmd", ".sql", ".lua", ".pl",
    ".hs", ".elm", ".ex", ".exs", ".clj", ".lisp", ".scm", ".ml", ".fs",
    // Config and markup
    ".toml", ".ini", ".cfg", ".conf", ".tex", ".rst", ".org", ".adoc"
};

// ========== API Configuration ==========

constexpr const char* OPENAI_API_BASE = "https://api.openai.com/v1";  // OpenAI API base URL.

// ========== Model Token Limits ==========

/**
 * Default maximum context window to use for models not in MODEL_MAX_CONTEXT_TOKENS.
 */
constexpr int DEFAULT_MAX_CONTEXT_TOKENS = 128000;

/**
 * Static table of known models and their max context windows.
 * Used to compute context fullness and trigger compaction.
 */
inline const std::unordered_map<std::string, int> MODEL_MAX_CONTEXT_TOKENS = {
    { "gpt-4o-2024-08-06", 128000 },
    { "gpt-4o-mini",       128000 },
    { "gpt-4.1-mini",      1047576 },
    { "gpt-4.1",           1047576 },
    { "o3",                200000 },
    { "o4-mini",           200000 },
};

/**
 * Returns the max context window for a model, or DEFAULT_MAX_CONTEXT_TOKENS
 * if the model is not in the static table.
 */
inline int get_max_context_tokens_for_model(const std::string& model) {
    auto it = MODEL_MAX_CONTEXT_TOKENS.find(model);
    return (it != MODEL_MAX_CONTEXT_TOKENS.end()) ? it->second : DEFAULT_MAX_CONTEXT_TOKENS;
}

} // namespace rag
