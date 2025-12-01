#pragma once

#include <string>
#include <iostream>

namespace rag {

// ========== ANSI Escape Codes ==========

// ANSI escape codes for terminal colors.
namespace ansi {
    constexpr const char* RESET = "\033[0m";
    constexpr const char* BOLD = "\033[1m";
    constexpr const char* RED = "\033[31m";
    constexpr const char* GREEN = "\033[32m";
    constexpr const char* YELLOW = "\033[33m";
    constexpr const char* BLUE = "\033[34m";
    constexpr const char* MAGENTA = "\033[35m";
    constexpr const char* CYAN = "\033[36m";
    constexpr const char* WHITE = "\033[37m";
}

/**
 * Terminal output helper with color support.
 *
 * Provides styled output methods that automatically handle ANSI color codes
 * based on terminal capabilities. Falls back to plain text when colors are
 * not supported (e.g., when TERM=dumb or output is not a TTY).
 */
class Console {
public:
    // Creates a Console instance and detects color support.
    Console();

    // ========== Basic Output ==========

    // Prints text without a trailing newline.
    void print(const std::string& text) const;

    // Prints text followed by a newline.
    void println(const std::string& text = "") const;

    // ========== Colored Output ==========

    // Prints error message in red.
    void print_error(const std::string& text) const;

    // Prints warning message in yellow.
    void print_warning(const std::string& text) const;

    // Prints success message in green with a checkmark prefix.
    void print_success(const std::string& text) const;

    // Prints informational message in cyan.
    void print_info(const std::string& text) const;

    // Prints header text in bold cyan.
    void print_header(const std::string& text) const;

    // ========== Styled Output ==========

    // Prints text in bold.
    void print_bold(const std::string& text) const;

    // Prints text with a specific ANSI color code.
    void print_colored(const std::string& text, const char* color) const;

    // ========== Status Messages ==========

    // Displays a status message (for progress indication).
    void start_status(const std::string& message) const;

    // Clears the current status line.
    void clear_status() const;

    // ========== Interactive Prompts ==========

    // Prompts the user for input with an optional default value.
    std::string prompt(const std::string& message, const std::string& default_value = "") const;

    // ========== Raw Output ==========

    // Prints text without newline or formatting (for streaming output).
    void print_raw(const std::string& text) const;

    // Flushes stdout.
    void flush() const;

private:
    bool colors_enabled_;  // True if terminal supports ANSI colors.

    // Detects and enables color support based on terminal capabilities.
    void enable_colors();
};

} // namespace rag
