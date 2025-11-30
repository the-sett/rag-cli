#pragma once

#include <string>

namespace rag {
namespace terminal {

/**
 * Get the terminal width in columns.
 * Returns 80 if width cannot be determined.
 */
int get_width();

/**
 * Get the terminal height in rows.
 * Returns 24 if height cannot be determined.
 */
int get_height();

/**
 * Check if stdout is a TTY (interactive terminal).
 */
bool is_tty();

/**
 * Count how many terminal lines a string occupies.
 * Accounts for newlines and line wrapping at terminal width.
 *
 * @param text The text to measure
 * @param terminal_width The terminal width for wrap calculation
 * @return Number of lines the text would occupy
 */
int count_lines(const std::string& text, int terminal_width);

/**
 * Calculate the display width of a string, accounting for
 * ANSI escape sequences (which have zero width) and
 * multi-byte UTF-8 characters.
 *
 * @param text The text to measure
 * @return Display width in columns
 */
int display_width(const std::string& text);

namespace cursor {
    /**
     * Move cursor up n lines.
     * ANSI: \033[nA
     */
    std::string up(int n);

    /**
     * Move cursor down n lines.
     * ANSI: \033[nB
     */
    std::string down(int n);

    /**
     * Move cursor to column n (1-based).
     * ANSI: \033[nG
     */
    std::string column(int n);

    /**
     * Save cursor position.
     * ANSI: \033[s
     */
    std::string save();

    /**
     * Restore cursor position.
     * ANSI: \033[u
     */
    std::string restore();
}

namespace clear {
    /**
     * Clear from cursor to end of line.
     * ANSI: \033[K
     */
    std::string to_end_of_line();

    /**
     * Clear from cursor to end of screen.
     * ANSI: \033[J
     */
    std::string to_end_of_screen();

    /**
     * Clear entire line.
     * ANSI: \033[2K
     */
    std::string line();
}

/**
 * Save the current terminal settings.
 * Call this at program startup before any raw mode changes.
 */
void save_original_settings();

/**
 * Restore terminal to original settings.
 * Safe to call from signal handlers.
 */
void restore_original_settings();

} // namespace terminal
} // namespace rag
