#include "terminal.hpp"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define isatty _isatty
#define STDOUT_FILENO 1
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include <cstdlib>

namespace rag {
namespace terminal {

int get_width() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }
#endif

    // Try COLUMNS environment variable
    const char* columns = std::getenv("COLUMNS");
    if (columns) {
        int width = std::atoi(columns);
        if (width > 0) {
            return width;
        }
    }

    // Default fallback
    return 80;
}

bool is_tty() {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

int display_width(const std::string& text) {
    int width = 0;
    bool in_escape = false;
    size_t i = 0;

    while (i < text.length()) {
        unsigned char c = static_cast<unsigned char>(text[i]);

        // Handle ANSI escape sequences (zero width)
        if (c == '\033') {
            in_escape = true;
            i++;
            continue;
        }

        if (in_escape) {
            // End of CSI sequence
            if (c == 'm' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                in_escape = false;
            }
            // Also handle OSC sequences (end with BEL or ST)
            if (c == '\007') {
                in_escape = false;
            }
            i++;
            continue;
        }

        // Handle UTF-8 multi-byte sequences
        // For simplicity, count each Unicode codepoint as width 1
        // (A more accurate implementation would use wcwidth)
        if ((c & 0x80) == 0) {
            // ASCII
            if (c != '\n' && c != '\r') {
                width++;
            }
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte UTF-8
            width++;
            i++;  // Skip continuation byte
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte UTF-8
            width++;
            i += 2;  // Skip continuation bytes
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte UTF-8
            width++;
            i += 3;  // Skip continuation bytes
        }

        i++;
    }

    return width;
}

int count_lines(const std::string& text, int terminal_width) {
    if (text.empty()) {
        return 0;
    }

    if (terminal_width <= 0) {
        terminal_width = 80;
    }

    int lines = 0;
    int current_line_width = 0;
    bool in_escape = false;
    size_t i = 0;

    while (i < text.length()) {
        unsigned char c = static_cast<unsigned char>(text[i]);

        // Handle ANSI escape sequences (zero width)
        if (c == '\033') {
            in_escape = true;
            i++;
            continue;
        }

        if (in_escape) {
            if (c == 'm' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '\007') {
                in_escape = false;
            }
            i++;
            continue;
        }

        if (c == '\n') {
            lines++;
            current_line_width = 0;
        } else if (c == '\r') {
            // Carriage return resets to start of line
            current_line_width = 0;
        } else {
            // Count character width
            int char_width = 1;

            // Handle UTF-8 multi-byte sequences
            if ((c & 0xE0) == 0xC0) {
                i++;  // Skip continuation byte
            } else if ((c & 0xF0) == 0xE0) {
                i += 2;  // Skip continuation bytes
            } else if ((c & 0xF8) == 0xF0) {
                i += 3;  // Skip continuation bytes
            }

            current_line_width += char_width;

            // Check for line wrap
            if (current_line_width >= terminal_width) {
                lines++;
                current_line_width = 0;
            }
        }

        i++;
    }

    // Count the last line if it has content
    if (current_line_width > 0) {
        lines++;
    }

    // Ensure at least 1 line if there was any content
    return lines > 0 ? lines : (text.empty() ? 0 : 1);
}

namespace cursor {

std::string up(int n) {
    if (n <= 0) return "";
    return "\033[" + std::to_string(n) + "A";
}

std::string down(int n) {
    if (n <= 0) return "";
    return "\033[" + std::to_string(n) + "B";
}

std::string column(int n) {
    if (n <= 0) n = 1;
    return "\033[" + std::to_string(n) + "G";
}

std::string save() {
    return "\033[s";
}

std::string restore() {
    return "\033[u";
}

} // namespace cursor

namespace clear {

std::string to_end_of_line() {
    return "\033[K";
}

std::string to_end_of_screen() {
    return "\033[J";
}

std::string line() {
    return "\033[2K";
}

} // namespace clear

} // namespace terminal
} // namespace rag
