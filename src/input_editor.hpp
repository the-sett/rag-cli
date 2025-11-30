#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace rag {

/**
 * Multi-line input editor with raw terminal mode.
 *
 * Features:
 * - Multi-line text input with > prompt on each line
 * - Enter adds a newline, double-Enter (rapid) submits
 * - Horizontal separator line above input area
 * - Basic line editing (backspace, delete)
 */
class InputEditor {
public:
    using OutputCallback = std::function<void(const std::string&)>;

    /**
     * Create an input editor.
     * @param output Callback for output (typically writes to stdout)
     * @param colors_enabled Whether to use ANSI colors
     */
    explicit InputEditor(OutputCallback output, bool colors_enabled = true);
    ~InputEditor();

    /**
     * Read multi-line input from the user.
     * Shows a horizontal line, then prompts with > for each line.
     * Double-Enter submits the input.
     * @return The entered text, or empty string on EOF/error
     */
    std::string read_input();

    /**
     * Set the double-Enter timeout in milliseconds.
     * If two Enters are pressed within this time, input is submitted.
     * Default is 300ms.
     */
    void set_double_enter_timeout(int ms) { double_enter_timeout_ms_ = ms; }

private:
    OutputCallback output_;
    bool colors_enabled_;
    int double_enter_timeout_ms_ = 300;

    // Terminal state - using pimpl to avoid including termios.h in header
    struct TerminalState;
    std::unique_ptr<TerminalState> terminal_state_;

    // Input state
    std::vector<std::string> lines_;
    size_t cursor_line_ = 0;
    size_t cursor_col_ = 0;

    // Enable/disable raw terminal mode
    bool enable_raw_mode();
    void disable_raw_mode();

    // Input handling
    int read_key();
    void handle_key(int key);
    void handle_enter();
    void handle_backspace();
    void handle_regular_char(char c);

    // Display
    void draw_separator();
    void draw_prompt();
    void redraw_current_line();
    void move_to_new_line();

    // Helpers
    std::string ansi(const char* code) const;
    int64_t current_time_ms() const;
};

} // namespace rag
