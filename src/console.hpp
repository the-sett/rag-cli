#pragma once

#include <string>
#include <iostream>

namespace rag {

// ANSI escape codes for terminal colors
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

class Console {
public:
    Console();

    // Basic output
    void print(const std::string& text) const;
    void println(const std::string& text = "") const;

    // Colored output (matches rich library patterns)
    void print_error(const std::string& text) const;      // Red
    void print_warning(const std::string& text) const;    // Yellow
    void print_success(const std::string& text) const;    // Green with checkmark
    void print_info(const std::string& text) const;       // Cyan
    void print_header(const std::string& text) const;     // Bold cyan

    // Styled output
    void print_bold(const std::string& text) const;
    void print_colored(const std::string& text, const char* color) const;

    // Status messages (simplified spinner - just shows message)
    void start_status(const std::string& message) const;
    void clear_status() const;

    // Interactive prompts
    std::string prompt(const std::string& message, const std::string& default_value = "") const;

    // Raw output (no newline, for streaming)
    void print_raw(const std::string& text) const;
    void flush() const;

private:
    bool colors_enabled_;

    void enable_colors();
};

} // namespace rag
