#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rag {

// File paths
constexpr const char* SETTINGS_FILE = "settings.json";
constexpr const char* LOG_DIR = "chat_logs";

// Thinking level shortcuts
inline const std::unordered_map<char, std::string> THINKING_MAP = {
    {'l', "low"},
    {'m', "medium"},
    {'h', "high"}
};

// File extensions supported by OpenAI's file_search
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

// OpenAI API base URL
constexpr const char* OPENAI_API_BASE = "https://api.openai.com/v1";

} // namespace rag
