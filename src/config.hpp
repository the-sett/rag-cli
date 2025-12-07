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

// ========== Initial Prompt ==========

// Hidden prompt sent at session start to get the AI to introduce itself.
constexpr const char* INITIAL_PROMPT =
    "Briefly introduce yourself and list the main topics covered in the indexed files.";

} // namespace rag
