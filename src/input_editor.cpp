#include "input_editor.hpp"
#include "terminal.hpp"

#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>
#include <cstring>
#include <chrono>
#include <iostream>

namespace rag {

namespace {
    constexpr const char* RESET = "\033[0m";
    constexpr const char* DIM = "\033[2m";
    constexpr const char* CYAN = "\033[36m";

    // Special key codes
    constexpr int KEY_ENTER = 13;
    constexpr int KEY_BACKSPACE = 127;
    constexpr int KEY_CTRL_C = 3;
    constexpr int KEY_CTRL_D = 4;
    constexpr int KEY_ESCAPE = 27;
}

// Pimpl for terminal state
struct InputEditor::TerminalState {
    struct termios original_termios;
    bool raw_mode_enabled = false;
};

InputEditor::InputEditor(OutputCallback output, bool colors_enabled)
    : output_(std::move(output))
    , colors_enabled_(colors_enabled)
    , terminal_state_(std::make_unique<TerminalState>())
{
}

InputEditor::~InputEditor() {
    disable_raw_mode();
}

std::string InputEditor::ansi(const char* code) const {
    return colors_enabled_ ? code : "";
}

int64_t InputEditor::current_time_ms() const {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return ms.count();
}

bool InputEditor::enable_raw_mode() {
    if (terminal_state_->raw_mode_enabled) {
        return true;
    }

    if (!terminal::is_tty()) {
        return false;
    }

    if (tcgetattr(STDIN_FILENO, &terminal_state_->original_termios) == -1) {
        return false;
    }

    struct termios raw = terminal_state_->original_termios;

    // Input modes: no break, no CR to NL, no parity check, no strip char,
    // no start/stop output control
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    // Output modes: disable post processing (we handle newlines ourselves)
    raw.c_oflag &= ~(OPOST);

    // Control modes: set 8 bit chars
    raw.c_cflag |= (CS8);

    // Local modes: echo off, canonical off, no extended functions,
    // no signal chars (^C, ^Z, etc) - actually keep ISIG for Ctrl+C
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);

    // Control chars: set return condition - read returns after 1 byte
    // with a timeout of 100ms (for checking double-Enter)
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;  // 100ms timeout

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return false;
    }

    terminal_state_->raw_mode_enabled = true;
    return true;
}

void InputEditor::disable_raw_mode() {
    if (terminal_state_ && terminal_state_->raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal_state_->original_termios);
        terminal_state_->raw_mode_enabled = false;
    }
}

int InputEditor::read_key() {
    char c;
    ssize_t nread = read(STDIN_FILENO, &c, 1);

    if (nread == -1) {
        return -1;  // Error
    }
    if (nread == 0) {
        return 0;   // Timeout, no key
    }

    // Handle escape sequences (arrow keys, etc.)
    if (c == KEY_ESCAPE) {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return KEY_ESCAPE;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return KEY_ESCAPE;

        if (seq[0] == '[') {
            // Arrow keys: [A=up, [B=down, [C=right, [D=left
            // For now, we ignore arrow keys (could add cursor movement later)
            return 0;
        }
        return KEY_ESCAPE;
    }

    return static_cast<unsigned char>(c);
}

void InputEditor::draw_separator() {
    int width = terminal::get_width();
    std::string line = ansi(DIM);
    for (int i = 0; i < width; i++) {
        line += "─";
    }
    line += ansi(RESET);
    line += "\r\n";
    output_(line);
}

void InputEditor::draw_prompt() {
    output_(ansi(CYAN) + "> " + ansi(RESET));
}

void InputEditor::redraw_current_line() {
    // Move to start of line, clear it, redraw prompt and text
    output_("\r");
    output_(terminal::clear::to_end_of_line());
    draw_prompt();
    if (cursor_line_ < lines_.size()) {
        output_(lines_[cursor_line_]);
    }
}

void InputEditor::move_to_new_line() {
    output_("\r\n");
    draw_prompt();
}

void InputEditor::handle_backspace() {
    if (cursor_col_ > 0) {
        // Delete character before cursor
        lines_[cursor_line_].erase(cursor_col_ - 1, 1);
        cursor_col_--;
        redraw_current_line();
    } else if (cursor_line_ > 0) {
        // At start of line - merge with previous line
        size_t prev_len = lines_[cursor_line_ - 1].length();
        lines_[cursor_line_ - 1] += lines_[cursor_line_];
        lines_.erase(lines_.begin() + cursor_line_);
        cursor_line_--;
        cursor_col_ = prev_len;

        // Move cursor up and redraw
        output_(terminal::cursor::up(1));
        redraw_current_line();
        // Clear the line below (the old current line)
        output_("\r\n");
        output_(terminal::clear::to_end_of_line());
        output_(terminal::cursor::up(1));
        // Reposition cursor
        output_("\r");
        output_(ansi(CYAN) + "> " + ansi(RESET));
        output_(lines_[cursor_line_].substr(0, cursor_col_));
    }
}

void InputEditor::handle_regular_char(char c) {
    // Insert character at cursor position
    if (cursor_line_ >= lines_.size()) {
        lines_.push_back("");
    }
    lines_[cursor_line_].insert(cursor_col_, 1, c);
    cursor_col_++;

    // Just output the character (simple case - cursor at end)
    // For mid-line editing, would need to redraw
    if (cursor_col_ == lines_[cursor_line_].length()) {
        output_(std::string(1, c));
    } else {
        redraw_current_line();
    }
}

std::string InputEditor::read_input() {
    // Check if we're in a TTY
    if (!terminal::is_tty()) {
        // Fall back to simple line reading for non-TTY
        std::string result;
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!result.empty()) result += "\n";
            result += line;
        }
        return result;
    }

    // Enable raw mode
    if (!enable_raw_mode()) {
        // Fall back to simple input
        std::string line;
        std::getline(std::cin, line);
        return line;
    }

    // Initialize state
    lines_.clear();
    lines_.push_back("");
    cursor_line_ = 0;
    cursor_col_ = 0;

    // Draw separator and initial prompt
    draw_separator();
    draw_prompt();

    int64_t last_enter_time = 0;
    bool should_submit = false;

    while (!should_submit) {
        int key = read_key();

        if (key == -1) {
            // Error
            break;
        }

        if (key == 0) {
            // Timeout, no key - continue waiting
            continue;
        }

        if (key == KEY_CTRL_C || key == KEY_CTRL_D) {
            // Cancel / EOF
            disable_raw_mode();
            output_("\r\n");
            return "";
        }

        if (key == KEY_ENTER) {
            int64_t now = current_time_ms();

            if (last_enter_time > 0 && (now - last_enter_time) < double_enter_timeout_ms_) {
                // Double Enter - submit!
                // Remove the empty line we just added from the first Enter
                if (!lines_.empty() && lines_.back().empty() && lines_.size() > 1) {
                    lines_.pop_back();
                }
                should_submit = true;
            } else {
                // Single Enter - add new line
                last_enter_time = now;

                // Split current line at cursor if needed
                std::string remainder;
                if (cursor_col_ < lines_[cursor_line_].length()) {
                    remainder = lines_[cursor_line_].substr(cursor_col_);
                    lines_[cursor_line_] = lines_[cursor_line_].substr(0, cursor_col_);
                }

                // Insert new line
                cursor_line_++;
                lines_.insert(lines_.begin() + cursor_line_, remainder);
                cursor_col_ = 0;

                move_to_new_line();
                if (!remainder.empty()) {
                    output_(remainder);
                    // Move cursor back to start
                    output_("\r");
                    draw_prompt();
                }
            }
        } else if (key == KEY_BACKSPACE) {
            last_enter_time = 0;  // Reset double-Enter detection
            handle_backspace();
        } else if (key >= 32 && key < 127) {
            last_enter_time = 0;  // Reset double-Enter detection
            handle_regular_char(static_cast<char>(key));
        }
        // Ignore other keys for now
    }

    disable_raw_mode();
    output_("\r\n");

    // Draw separator below the input area
    draw_separator();

    // Combine all lines
    std::string result;
    for (size_t i = 0; i < lines_.size(); i++) {
        if (i > 0) result += "\n";
        result += lines_[i];
    }

    return result;
}

} // namespace rag
