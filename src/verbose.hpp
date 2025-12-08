#pragma once

/**
 * Verbose logging utility for crag CLI.
 *
 * Provides debug output for API calls, WebSocket messages, and HTTP requests
 * when the -v/--verbose flag is enabled.
 */

#include <string>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <sstream>

namespace rag {

/**
 * Global verbose mode flag.
 */
inline bool g_verbose = false;

/**
 * Set verbose mode.
 */
inline void set_verbose(bool enabled) {
    g_verbose = enabled;
}

/**
 * Check if verbose mode is enabled.
 */
inline bool is_verbose() {
    return g_verbose;
}

/**
 * Get current timestamp as string.
 */
inline std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

/**
 * Log a verbose message with timestamp and category.
 */
inline void verbose_log(const std::string& category, const std::string& message) {
    if (!g_verbose) return;
    std::cerr << "\033[90m[" << timestamp() << "] \033[36m[" << category << "]\033[0m " << message << std::endl;
}

/**
 * Log outgoing data (requests).
 */
inline void verbose_out(const std::string& category, const std::string& message) {
    if (!g_verbose) return;
    std::cerr << "\033[90m[" << timestamp() << "] \033[33m[" << category << " >>>]\033[0m " << message << std::endl;
}

/**
 * Log incoming data (responses).
 */
inline void verbose_in(const std::string& category, const std::string& message) {
    if (!g_verbose) return;
    std::cerr << "\033[90m[" << timestamp() << "] \033[32m[" << category << " <<<]\033[0m " << message << std::endl;
}

/**
 * Log error messages (always shown in verbose mode).
 */
inline void verbose_err(const std::string& category, const std::string& message) {
    if (!g_verbose) return;
    std::cerr << "\033[90m[" << timestamp() << "] \033[31m[" << category << " ERR]\033[0m " << message << std::endl;
}

/**
 * Truncate long content for display.
 */
inline std::string truncate(const std::string& s, size_t max_len = 200) {
    if (s.length() <= max_len) return s;
    return s.substr(0, max_len) + "... (" + std::to_string(s.length()) + " bytes total)";
}

/**
 * Format JSON for compact display (single line, truncated).
 */
inline std::string format_json_compact(const std::string& json_str, size_t max_len = 500) {
    // Remove newlines and collapse whitespace for compact display
    std::string compact;
    compact.reserve(json_str.size());
    bool in_string = false;
    bool last_was_space = false;

    for (char c : json_str) {
        if (c == '"' && (compact.empty() || compact.back() != '\\')) {
            in_string = !in_string;
        }

        if (in_string) {
            compact += c;
            last_was_space = false;
        } else if (c == '\n' || c == '\r' || c == '\t') {
            if (!last_was_space) {
                compact += ' ';
                last_was_space = true;
            }
        } else if (c == ' ') {
            if (!last_was_space) {
                compact += c;
                last_was_space = true;
            }
        } else {
            compact += c;
            last_was_space = false;
        }
    }

    return truncate(compact, max_len);
}

} // namespace rag
