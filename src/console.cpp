#include "console.hpp"
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace rag {

Console::Console() : colors_enabled_(true) {
    enable_colors();
}

void Console::enable_colors() {
#ifdef _WIN32
    // Enable ANSI escape codes on Windows 10+
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
        }
    }
#endif
    // Check if output is a terminal
    const char* term = std::getenv("TERM");
    if (!term || std::string(term) == "dumb") {
        colors_enabled_ = false;
    }
}

void Console::print(const std::string& text) const {
    std::cout << text;
}

void Console::println(const std::string& text) const {
    std::cout << text << std::endl;
}

void Console::print_error(const std::string& text) const {
    if (colors_enabled_) {
        std::cout << ansi::RED << text << ansi::RESET << std::endl;
    } else {
        std::cout << text << std::endl;
    }
}

void Console::print_warning(const std::string& text) const {
    if (colors_enabled_) {
        std::cout << ansi::YELLOW << text << ansi::RESET << std::endl;
    } else {
        std::cout << text << std::endl;
    }
}

void Console::print_success(const std::string& text) const {
    if (colors_enabled_) {
        std::cout << ansi::GREEN << "✓" << ansi::RESET << " " << text << std::endl;
    } else {
        std::cout << "* " << text << std::endl;
    }
}

void Console::print_info(const std::string& text) const {
    if (colors_enabled_) {
        std::cout << ansi::CYAN << text << ansi::RESET << std::endl;
    } else {
        std::cout << text << std::endl;
    }
}

void Console::print_header(const std::string& text) const {
    if (colors_enabled_) {
        std::cout << ansi::BOLD << ansi::CYAN << text << ansi::RESET << std::endl;
    } else {
        std::cout << text << std::endl;
    }
}

void Console::print_bold(const std::string& text) const {
    if (colors_enabled_) {
        std::cout << ansi::BOLD << text << ansi::RESET;
    } else {
        std::cout << text;
    }
}

void Console::print_colored(const std::string& text, const char* color) const {
    if (colors_enabled_) {
        std::cout << color << text << ansi::RESET;
    } else {
        std::cout << text;
    }
}

void Console::start_status(const std::string& message) const {
    if (colors_enabled_) {
        // Return to start of line, print message, clear to end of line
        std::cout << "\r" << ansi::YELLOW << message << ansi::RESET << "\033[K" << std::flush;
    } else {
        // Dumb terminal: just print with newline
        std::cout << message << std::endl;
    }
}

void Console::clear_status() const {
    if (colors_enabled_) {
        // Move to beginning of line and clear it
        std::cout << "\r\033[K" << std::flush;
    }
    // On dumb terminals, nothing to clear (each status was on its own line)
}

std::string Console::prompt(const std::string& message, const std::string& default_value) const {
    if (!default_value.empty()) {
        std::cout << message << " [" << default_value << "]: ";
    } else {
        std::cout << message << ": ";
    }
    std::cout << std::flush;

    std::string input;
    std::getline(std::cin, input);

    if (input.empty() && !default_value.empty()) {
        return default_value;
    }
    return input;
}

void Console::print_raw(const std::string& text) const {
    std::cout << text;
}

void Console::flush() const {
    std::cout << std::flush;
}

} // namespace rag
