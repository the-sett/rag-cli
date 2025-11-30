#include "markdown_renderer.hpp"
#include "terminal.hpp"
#include <cmark.h>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <algorithm>

namespace rag {

namespace {
    // Calculate display width of a UTF-8 string (counts code points, not bytes)
    // Assumes each code point is 1 display column (works for most text)
    // Skips ANSI escape sequences (zero display width)
    size_t display_width(const std::string& text) {
        size_t width = 0;
        bool in_escape = false;
        for (size_t i = 0; i < text.length(); ) {
            unsigned char c = text[i];

            // Handle ANSI escape sequences
            if (c == '\033') {
                in_escape = true;
                i++;
                continue;
            }
            if (in_escape) {
                // CSI sequences end with a letter (A-Z, a-z)
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                    in_escape = false;
                }
                i++;
                continue;
            }

            if ((c & 0x80) == 0) {
                // ASCII (1 byte)
                width++;
                i++;
            } else if ((c & 0xE0) == 0xC0) {
                // 2-byte UTF-8
                width++;
                i += 2;
            } else if ((c & 0xF0) == 0xE0) {
                // 3-byte UTF-8 (includes box-drawing, dashes, quotes)
                width++;
                i += 3;
            } else if ((c & 0xF8) == 0xF0) {
                // 4-byte UTF-8
                width++;
                i += 4;
            } else {
                // Invalid or continuation byte, skip
                i++;
            }
        }
        return width;
    }

    // Advance a byte index by n display characters, returning the new byte position
    // Skips ANSI escape sequences (zero display width)
    size_t advance_by_display_chars(const std::string& text, size_t byte_pos, size_t n_chars) {
        size_t chars_advanced = 0;
        bool in_escape = false;
        while (byte_pos < text.length() && chars_advanced < n_chars) {
            unsigned char c = text[byte_pos];

            // Handle ANSI escape sequences
            if (c == '\033') {
                in_escape = true;
                byte_pos++;
                continue;
            }
            if (in_escape) {
                // CSI sequences end with a letter (A-Z, a-z)
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                    in_escape = false;
                }
                byte_pos++;
                continue;
            }

            if ((c & 0x80) == 0) {
                byte_pos++;
            } else if ((c & 0xE0) == 0xC0) {
                byte_pos += 2;
            } else if ((c & 0xF0) == 0xE0) {
                byte_pos += 3;
            } else if ((c & 0xF8) == 0xF0) {
                byte_pos += 4;
            } else {
                byte_pos++;
            }
            chars_advanced++;
        }
        return byte_pos;
    }

    // ANSI escape codes
    constexpr const char* RESET = "\033[0m";
    constexpr const char* BOLD = "\033[1m";
    constexpr const char* ITALIC = "\033[3m";
    constexpr const char* UNDERLINE = "\033[4m";
    constexpr const char* DIM = "\033[2m";
    constexpr const char* CYAN = "\033[36m";
    constexpr const char* YELLOW = "\033[33m";
    constexpr const char* GREEN = "\033[32m";
    constexpr const char* BLUE = "\033[34m";
    constexpr const char* MAGENTA = "\033[35m";

    // Collect all text content from a node and its children
    std::string collect_text(cmark_node* node) {
        std::string result;
        cmark_iter* iter = cmark_iter_new(node);
        cmark_event_type ev_type;

        while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
            cmark_node* cur = cmark_iter_get_node(iter);
            if (ev_type == CMARK_EVENT_ENTER && cmark_node_get_type(cur) == CMARK_NODE_TEXT) {
                const char* text = cmark_node_get_literal(cur);
                if (text) {
                    result += text;
                }
            } else if (ev_type == CMARK_EVENT_ENTER && cmark_node_get_type(cur) == CMARK_NODE_SOFTBREAK) {
                result += " ";
            } else if (ev_type == CMARK_EVENT_ENTER && cmark_node_get_type(cur) == CMARK_NODE_LINEBREAK) {
                result += "\n";
            } else if (ev_type == CMARK_EVENT_ENTER && cmark_node_get_type(cur) == CMARK_NODE_CODE) {
                const char* code = cmark_node_get_literal(cur);
                if (code) {
                    result += code;
                }
            }
        }

        cmark_iter_free(iter);
        return result;
    }
}

MarkdownRenderer::MarkdownRenderer(OutputCallback output, bool colors_enabled, int terminal_width)
    : output_(std::move(output))
    , colors_enabled_(colors_enabled)
{
    if (terminal_width == 0) {
        // Auto-detect
        if (terminal::is_tty()) {
            terminal_width_ = terminal::get_width();
            terminal_height_ = terminal::get_height();
        } else {
            terminal_width_ = -1;  // Disable rewrite for non-TTY
            terminal_height_ = -1;
        }
    } else {
        terminal_width_ = terminal_width;
        // Auto-detect height even when width is specified
        if (terminal_width > 0) {
            // Always try to get height when width is explicitly set (for testing)
            terminal_height_ = terminal::get_height();
        } else {
            terminal_height_ = -1;
        }
    }

    // DEBUG
    static bool debug_enabled = (std::getenv("DEBUG_MARKDOWN") != nullptr);
    if (debug_enabled) {
        fprintf(stderr, "[DEBUG] MarkdownRenderer: terminal_width_=%d, terminal_height_=%d\n",
                terminal_width_, terminal_height_);
    }
}

void MarkdownRenderer::feed(const std::string& delta) {
    buffer_ += delta;

    // In hybrid mode, output raw text immediately
    output_raw(delta);

    // DEBUG
    static bool debug_enabled = (std::getenv("DEBUG_MARKDOWN") != nullptr);

    // Process complete blocks
    while (has_complete_block()) {
        std::string block = extract_complete_block();
        if (debug_enabled && !block.empty()) {
            std::string block_preview = block.substr(0, 40);
            if (block.length() > 40) block_preview += "...";
            // Replace newlines for display
            for (char& c : block_preview) if (c == '\n') c = '|';
            fprintf(stderr, "[DEBUG] Complete block extracted: \"%s\"\n", block_preview.c_str());
        }

        std::string formatted = render_markdown(block);

        // In hybrid mode, raw_output_ contains everything we've output.
        // After extraction, buffer_ contains remaining (unprocessed) content.
        // So the portion to rewrite = raw_output_.length() - buffer_.length()
        // (which equals the block we just extracted)
        size_t rewrite_len = raw_output_.length() - buffer_.length();

        if (debug_enabled) {
            fprintf(stderr, "[DEBUG] Calling rewrite_block: rewrite_len=%zu, raw_output_.length()=%zu, buffer_.length()=%zu\n",
                    rewrite_len, raw_output_.length(), buffer_.length());
        }

        // Replace raw output with formatted version
        rewrite_block(rewrite_len, formatted);
    }

    // After processing all complete blocks, check if the remaining incomplete
    // block has grown large enough to need buffering
    check_buffering_needed();
}

void MarkdownRenderer::finish() {
    // Render any remaining content
    if (!buffer_.empty()) {
        std::string formatted = render_markdown(buffer_);
        rewrite_block(buffer_.length(), formatted);
        buffer_.clear();
    }

    // Reset state
    raw_output_.clear();
    in_code_block_ = false;
    code_fence_length_ = 0;
    code_fence_chars_.clear();
    code_fence_info_.clear();
    needs_blank_before_next_ = false;
    prev_was_list_item_ = false;
}

void MarkdownRenderer::output_raw(const std::string& text) {
    if (terminal_width_ < 0) {
        // Rewrite disabled, don't output raw text
        return;
    }

    // If already buffering a long block, just accumulate
    if (buffering_long_block_) {
        long_block_buffer_ += text;
        update_spinner();
        return;
    }

    // Output the raw text immediately (for responsive streaming)
    output_(text);
    raw_output_ += text;

    // Note: We don't check for buffering mode here anymore.
    // Buffering check happens in check_buffering_needed() which is called
    // from feed() AFTER all complete blocks have been extracted.
    // This ensures we only buffer for truly incomplete blocks.
}

void MarkdownRenderer::check_buffering_needed() {
    if (terminal_width_ < 0 || buffering_long_block_) {
        return;  // Rewrite disabled or already buffering
    }

    // Calculate how many lines the current incomplete block occupies.
    // raw_output_ contains only unrendered content (remainder from last rewrite).
    // current_block_start_ should be 0 after each rewrite_block() call.
    std::string current_block_text = raw_output_.substr(current_block_start_);
    current_block_lines_ = terminal::count_lines(current_block_text, terminal_width_);

    // Check if this block's height is approaching terminal height limit
    // Leave 1 line for the spinner at the bottom
    int height_limit = (terminal_height_ > 0) ? terminal_height_ - 1 : MAX_REWRITE_LINES;

    // DEBUG
    static bool debug_enabled = (std::getenv("DEBUG_MARKDOWN") != nullptr);
    if (debug_enabled) {
        fprintf(stderr, "[DEBUG] check_buffering_needed: current_block_start_=%zu, raw_output_.len=%zu, current_block_lines_=%d, height_limit=%d\n",
                current_block_start_, raw_output_.length(), current_block_lines_, height_limit);
    }

    if (current_block_lines_ >= height_limit && height_limit > 0) {
        if (debug_enabled) {
            fprintf(stderr, "[DEBUG] Switching to buffering mode!\n");
        }
        // Switch to buffering mode for subsequent content
        buffering_long_block_ = true;
        long_block_buffer_.clear();

        // Show spinner on a new line at the bottom
        output_("\n");
        update_spinner();
    }
}

void MarkdownRenderer::rewrite_block(size_t raw_len, const std::string& formatted) {
    // Handle long block buffering mode first
    if (buffering_long_block_) {
        finish_long_block_buffering(formatted);
        return;
    }

    if (terminal_width_ < 0 || raw_output_.empty()) {
        // Rewrite disabled or nothing to rewrite - just output formatted
        output_(formatted);
        return;
    }

    // Calculate how much of raw_output_ corresponds to this block
    std::string raw_portion = raw_output_.substr(0, raw_len);
    std::string remainder = raw_output_.substr(raw_len);

    // Calculate total lines currently displayed
    int total_lines = terminal::count_lines(raw_output_, terminal_width_);

    if (total_lines > MAX_REWRITE_LINES) {
        // Too many lines to safely rewrite, just append formatted
        // (Raw output stays visible, which is acceptable)
        output_(formatted);
        raw_output_ = remainder;
        return;
    }

    // Build rewrite sequence:
    // 1. Move to start of current line
    // 2. Move up to start of raw output
    // 3. Clear from cursor to end of screen
    // 4. Output formatted text
    // 5. Re-output remainder (belongs to next block)

    std::string seq;

    // Move to beginning of current line
    seq += "\r";

    // Calculate how many lines to move up
    // If raw_output_ ends with newline, cursor is on the NEXT line (empty),
    // so we need to move up total_lines. Otherwise, we're on the last line
    // of the content, so move up total_lines - 1.
    int lines_up = total_lines;
    if (!raw_output_.empty() && raw_output_.back() != '\n') {
        lines_up = total_lines - 1;
    }

    if (lines_up > 0) {
        seq += terminal::cursor::up(lines_up);
    }

    // Clear from cursor to end of screen
    seq += terminal::clear::to_end_of_screen();

    // Output the formatted version
    seq += formatted;

    // Re-output remainder (raw text for next block)
    seq += remainder;

    output_(seq);

    // Update raw_output_ to just the remainder
    raw_output_ = remainder;

    // Reset block tracking - the remainder is the start of the next block
    current_block_start_ = 0;  // Remainder is now at the start of raw_output_
    current_block_lines_ = terminal::count_lines(remainder, terminal_width_);

    // DEBUG
    static bool debug_enabled = (std::getenv("DEBUG_MARKDOWN") != nullptr);
    if (debug_enabled) {
        fprintf(stderr, "[DEBUG] rewrite_block done: remainder has %zu bytes, current_block_start_=0, current_block_lines_=%d\n",
                remainder.length(), current_block_lines_);
    }
}

void MarkdownRenderer::update_spinner() {
    // Spinner characters for animation
    static const char* spinner_frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    static const int num_frames = 10;

    // Count buffered lines
    int buffered_lines = terminal::count_lines(long_block_buffer_, terminal_width_);

    // Move to start of line, show spinner with line count
    std::string seq;
    seq += "\r";
    seq += ansi("\033[36m");  // Cyan
    seq += spinner_frames[spinner_state_ % num_frames];
    seq += " Buffering... ";
    seq += std::to_string(buffered_lines);
    seq += " lines";
    seq += ansi("\033[0m");
    seq += terminal::clear::to_end_of_line();

    output_(seq);
    spinner_state_++;
}

void MarkdownRenderer::finish_long_block_buffering(const std::string& formatted) {
    if (!buffering_long_block_) {
        return;
    }

    // We need to:
    // 1. Clear the spinner line (we're currently on it)
    // 2. Move up past all the visible raw output
    // 3. Clear everything from there down
    // 4. Output the formatted content

    std::string seq;

    // Clear the spinner line first
    seq += "\r";
    seq += terminal::clear::to_end_of_line();

    // Move up: current_block_lines_ of raw output + 1 for the spinner line we added
    int lines_to_go_up = current_block_lines_;
    if (lines_to_go_up > 0) {
        seq += terminal::cursor::up(lines_to_go_up);
    }

    // Clear from cursor to end of screen (removes all raw output + spinner line)
    seq += terminal::clear::to_end_of_screen();

    // Output the formatted content
    seq += formatted;

    output_(seq);

    // Reset buffering state
    buffering_long_block_ = false;
    long_block_buffer_.clear();
    current_block_lines_ = 0;
    raw_output_.clear();
    spinner_state_ = 0;
}

bool MarkdownRenderer::is_code_fence(const std::string& line, size_t& fence_length, std::string& fence_chars) const {
    // Match ``` or ~~~ at start of line (possibly with leading spaces)
    size_t start = line.find_first_not_of(' ');
    if (start == std::string::npos || start > 3) {
        return false;  // More than 3 spaces = code block, not fence
    }

    char fence_char = line[start];
    if (fence_char != '`' && fence_char != '~') {
        return false;
    }

    size_t count = 0;
    size_t i = start;
    while (i < line.length() && line[i] == fence_char) {
        count++;
        i++;
    }

    if (count >= 3) {
        fence_length = count;
        fence_chars = std::string(count, fence_char);
        return true;
    }

    return false;
}

bool MarkdownRenderer::is_closing_fence(const std::string& line) const {
    if (!in_code_block_) {
        return false;
    }

    size_t start = line.find_first_not_of(' ');
    if (start == std::string::npos || start > 3) {
        return false;
    }

    char fence_char = code_fence_chars_[0];
    if (line[start] != fence_char) {
        return false;
    }

    size_t count = 0;
    size_t i = start;
    while (i < line.length() && line[i] == fence_char) {
        count++;
        i++;
    }

    // Closing fence must be at least as long, and line must be only whitespace after
    if (count >= code_fence_length_) {
        // Rest of line should be whitespace only
        while (i < line.length()) {
            if (line[i] != ' ' && line[i] != '\t') {
                return false;
            }
            i++;
        }
        return true;
    }

    return false;
}

bool MarkdownRenderer::is_thematic_break(const std::string& line) {
    // Thematic breaks: 3+ of the same character (-, *, _) with optional spaces
    // Up to 3 leading spaces allowed
    size_t start = 0;
    while (start < line.length() && start < 3 && line[start] == ' ') {
        start++;
    }

    if (start >= line.length()) {
        return false;
    }

    char marker = line[start];
    if (marker != '-' && marker != '*' && marker != '_') {
        return false;
    }

    int marker_count = 0;
    for (size_t i = start; i < line.length(); i++) {
        if (line[i] == marker) {
            marker_count++;
        } else if (line[i] != ' ') {
            return false;  // Invalid character
        }
    }

    return marker_count >= 3;
}

bool MarkdownRenderer::is_table_row(const std::string& line) const {
    // A table row starts and ends with | (after trimming whitespace)
    // and contains at least 2 pipes total
    size_t start = line.find_first_not_of(' ');
    if (start == std::string::npos || line[start] != '|') {
        return false;
    }

    size_t end = line.find_last_not_of(' ');
    if (end == std::string::npos || line[end] != '|') {
        return false;
    }

    // Must have at least 2 pipes (start and end can be the same for edge cases,
    // but realistically we need start != end for a valid row like "| x |")
    return start < end;
}

bool MarkdownRenderer::is_table_separator(const std::string& line) const {
    // Table separator line: | --- | --- | or |:---:|:---|
    // Contains |, -, and optionally : and spaces
    if (!is_table_row(line)) {
        return false;
    }

    // Check that content between pipes is only -, :, and spaces
    bool found_dash = false;
    for (size_t i = 0; i < line.length(); i++) {
        char c = line[i];
        if (c == '|' || c == ' ' || c == ':') {
            continue;
        } else if (c == '-') {
            found_dash = true;
        } else {
            return false;  // Invalid character for separator
        }
    }

    return found_dash;  // Must have at least one dash
}

bool MarkdownRenderer::is_list_item(const std::string& line) const {
    // List items start with *, -, + (unordered) or digits followed by . or ) (ordered)
    // Up to 3 leading spaces allowed
    size_t start = 0;
    while (start < line.length() && start < 3 && line[start] == ' ') {
        start++;
    }

    if (start >= line.length()) {
        return false;
    }

    char c = line[start];

    // Unordered list markers: *, -, +
    if (c == '*' || c == '-' || c == '+') {
        // Must be followed by space (or end of line for empty item)
        return start + 1 >= line.length() || line[start + 1] == ' ';
    }

    // Ordered list: digit(s) followed by . or )
    if (c >= '0' && c <= '9') {
        size_t i = start;
        while (i < line.length() && line[i] >= '0' && line[i] <= '9') {
            i++;
        }
        if (i < line.length() && (line[i] == '.' || line[i] == ')')) {
            // Must be followed by space (or end of line)
            return i + 1 >= line.length() || line[i + 1] == ' ';
        }
    }

    return false;
}

bool MarkdownRenderer::is_heading(const std::string& line) const {
    // ATX headings: 1-6 # characters followed by space or end of line
    // Up to 3 leading spaces allowed
    size_t start = 0;
    while (start < line.length() && start < 3 && line[start] == ' ') {
        start++;
    }

    if (start >= line.length() || line[start] != '#') {
        return false;
    }

    // Count # characters (1-6)
    size_t hash_count = 0;
    size_t i = start;
    while (i < line.length() && line[i] == '#' && hash_count < 7) {
        hash_count++;
        i++;
    }

    if (hash_count < 1 || hash_count > 6) {
        return false;
    }

    // Must be followed by space, tab, or end of line
    if (i >= line.length()) {
        return true;  // Just "###" is valid
    }

    return line[i] == ' ' || line[i] == '\t';
}

bool MarkdownRenderer::is_blockquote(const std::string& line) const {
    // Blockquote: > at start (up to 3 leading spaces allowed)
    size_t start = 0;
    while (start < line.length() && start < 3 && line[start] == ' ') {
        start++;
    }

    return start < line.length() && line[start] == '>';
}

bool MarkdownRenderer::has_complete_block() const {
    if (buffer_.empty()) {
        return false;
    }

    // If we're in a code block, look for closing fence
    if (in_code_block_) {
        size_t pos = 0;
        while ((pos = buffer_.find('\n', pos)) != std::string::npos) {
            size_t line_start = (pos == 0) ? 0 : buffer_.rfind('\n', pos - 1);
            line_start = (line_start == std::string::npos) ? 0 : line_start + 1;

            std::string line = buffer_.substr(line_start, pos - line_start);
            if (is_closing_fence(line)) {
                return true;
            }
            pos++;
        }
        return false;
    }

    // Need at least one complete line to analyze
    size_t first_newline = buffer_.find('\n');
    if (first_newline == std::string::npos) {
        return false;
    }

    std::string first_line = buffer_.substr(0, first_newline);

    // Code fence: we have a "complete block" (the opening fence line) that needs to be processed
    // to set state, but extract will return empty since actual code isn't ready yet
    size_t fence_len;
    std::string fence_chars;
    if (is_code_fence(first_line, fence_len, fence_chars)) {
        return true;  // Need to process this to set in_code_block_ state
    }

    // Heading: complete after single newline - render immediately
    if (is_heading(first_line)) {
        return true;
    }

    // Thematic break: complete after single newline - render immediately
    if (is_thematic_break(first_line)) {
        return true;
    }

    // Table: accumulate all consecutive table rows, complete when non-table line appears
    if (is_table_row(first_line)) {
        size_t pos = first_newline + 1;
        while (pos < buffer_.length()) {
            size_t next_newline = buffer_.find('\n', pos);
            if (next_newline == std::string::npos) {
                return false;  // Wait for more input
            }
            std::string line = buffer_.substr(pos, next_newline - pos);
            if (!is_table_row(line)) {
                return true;  // Non-table line ends the table
            }
            pos = next_newline + 1;
        }
        return false;  // All lines are table rows, wait for terminator
    }

    // Blockquote: accumulate consecutive > lines, complete when non-blockquote line appears
    if (is_blockquote(first_line)) {
        size_t pos = first_newline + 1;
        while (pos < buffer_.length()) {
            size_t next_newline = buffer_.find('\n', pos);
            if (next_newline == std::string::npos) {
                return false;  // Wait for more input
            }
            std::string line = buffer_.substr(pos, next_newline - pos);
            if (!is_blockquote(line) && !line.empty()) {
                return true;  // Non-blockquote, non-empty line ends the blockquote
            }
            pos = next_newline + 1;
        }
        return false;  // All lines are blockquote, wait for terminator
    }

    // List item: complete when we see the NEXT list item or a non-list/non-continuation line
    // This allows rendering each item individually
    if (is_list_item(first_line)) {
        size_t pos = first_newline + 1;
        while (pos < buffer_.length()) {
            size_t next_newline = buffer_.find('\n', pos);
            if (next_newline == std::string::npos) {
                return false;  // Wait for more input
            }
            std::string line = buffer_.substr(pos, next_newline - pos);

            // If we see another list item, the FIRST item is complete
            if (is_list_item(line)) {
                return true;
            }

            // If we see a non-list, non-continuation line, the list item is complete
            // Continuation lines start with spaces (indented content like code blocks)
            // Empty lines can be part of loose lists
            bool is_continuation = !line.empty() && (line[0] == ' ' || line[0] == '\t');
            bool is_empty = line.empty();

            if (!is_continuation && !is_empty) {
                return true;  // Non-list content ends the item
            }

            pos = next_newline + 1;
        }
        return false;  // Wait for next item or terminator
    }

    // Paragraph: complete when we see a blank line OR the start of a different block type
    // Check for blank line
    if (buffer_.find("\n\n") != std::string::npos) {
        return true;
    }

    // Check if a different block type starts after the first line
    size_t pos = first_newline + 1;
    if (pos < buffer_.length()) {
        size_t next_newline = buffer_.find('\n', pos);
        if (next_newline != std::string::npos) {
            std::string next_line = buffer_.substr(pos, next_newline - pos);

            // If next line starts a new block type, current paragraph is complete
            if (is_heading(next_line) || is_thematic_break(next_line) ||
                is_list_item(next_line) || is_blockquote(next_line) ||
                is_table_row(next_line) || is_code_fence(next_line, fence_len, fence_chars)) {
                return true;
            }
        }
    }

    return false;
}

std::string MarkdownRenderer::extract_complete_block() {
    if (in_code_block_) {
        // Find the closing fence
        size_t pos = 0;
        while ((pos = buffer_.find('\n', pos)) != std::string::npos) {
            size_t line_start = (pos == 0) ? 0 : buffer_.rfind('\n', pos - 1);
            line_start = (line_start == std::string::npos) ? 0 : line_start + 1;

            std::string line = buffer_.substr(line_start, pos - line_start);
            if (is_closing_fence(line)) {
                // Reconstruct opening fence and include content up to closing fence
                std::string opening_fence = code_fence_chars_;
                if (!code_fence_info_.empty()) {
                    opening_fence += code_fence_info_;
                }
                opening_fence += "\n";

                std::string content = buffer_.substr(0, pos + 1);
                buffer_.erase(0, pos + 1);

                in_code_block_ = false;
                code_fence_length_ = 0;
                code_fence_chars_.clear();
                code_fence_info_.clear();

                return opening_fence + content;
            }
            pos++;
        }
        return "";
    }

    size_t first_newline = buffer_.find('\n');
    if (first_newline == std::string::npos) {
        return "";
    }

    std::string first_line = buffer_.substr(0, first_newline);

    // Code fence: consume the opening fence line, set state, wait for closing fence
    size_t fence_len;
    std::string fence_chars;
    if (is_code_fence(first_line, fence_len, fence_chars)) {
        in_code_block_ = true;
        code_fence_length_ = fence_len;
        code_fence_chars_ = fence_chars;
        // Extract info string (language) after the fence characters
        size_t fence_end = first_line.find(fence_chars[0]);
        while (fence_end < first_line.length() && first_line[fence_end] == fence_chars[0]) {
            fence_end++;
        }
        // Skip whitespace after fence
        while (fence_end < first_line.length() && (first_line[fence_end] == ' ' || first_line[fence_end] == '\t')) {
            fence_end++;
        }
        code_fence_info_ = (fence_end < first_line.length()) ? first_line.substr(fence_end) : "";
        // Remove the opening fence from buffer - we'll reconstruct it when extracting
        buffer_.erase(0, first_newline + 1);
        return "";
    }

    // Heading: extract single line
    if (is_heading(first_line)) {
        std::string block = buffer_.substr(0, first_newline + 1);
        buffer_.erase(0, first_newline + 1);
        return block;
    }

    // Thematic break: extract single line
    if (is_thematic_break(first_line)) {
        std::string block = buffer_.substr(0, first_newline + 1);
        buffer_.erase(0, first_newline + 1);
        return block;
    }

    // Table: extract all consecutive table rows
    if (is_table_row(first_line)) {
        size_t last_table_row_end = first_newline;
        size_t pos = first_newline + 1;

        while (pos < buffer_.length()) {
            size_t next_newline = buffer_.find('\n', pos);
            if (next_newline == std::string::npos) {
                break;
            }
            std::string line = buffer_.substr(pos, next_newline - pos);
            if (is_table_row(line)) {
                last_table_row_end = next_newline;
                pos = next_newline + 1;
            } else {
                break;
            }
        }

        std::string block = buffer_.substr(0, last_table_row_end + 1);
        buffer_.erase(0, last_table_row_end + 1);
        return block;
    }

    // Blockquote: extract all consecutive blockquote lines
    if (is_blockquote(first_line)) {
        size_t last_quote_end = first_newline;
        size_t pos = first_newline + 1;

        while (pos < buffer_.length()) {
            size_t next_newline = buffer_.find('\n', pos);
            if (next_newline == std::string::npos) {
                break;
            }
            std::string line = buffer_.substr(pos, next_newline - pos);
            // Include empty lines and continuation blockquote lines
            if (is_blockquote(line) || line.empty()) {
                last_quote_end = next_newline;
                pos = next_newline + 1;
            } else {
                break;
            }
        }

        std::string block = buffer_.substr(0, last_quote_end + 1);
        buffer_.erase(0, last_quote_end + 1);
        return block;
    }

    // List item: extract just the first item (until next list item or non-continuation)
    if (is_list_item(first_line)) {
        size_t item_end = first_newline;
        size_t pos = first_newline + 1;

        while (pos < buffer_.length()) {
            size_t next_newline = buffer_.find('\n', pos);
            if (next_newline == std::string::npos) {
                break;
            }
            std::string line = buffer_.substr(pos, next_newline - pos);

            // If we see another list item, stop - that's the next item
            if (is_list_item(line)) {
                break;
            }

            // Continuation lines (indented) or empty lines belong to this item
            bool is_continuation = !line.empty() && (line[0] == ' ' || line[0] == '\t');
            bool is_empty = line.empty();

            if (is_continuation || is_empty) {
                item_end = next_newline;
                pos = next_newline + 1;
            } else {
                break;  // Non-list content ends the item
            }
        }

        std::string block = buffer_.substr(0, item_end + 1);
        buffer_.erase(0, item_end + 1);
        return block;
    }

    // Paragraph: extract until blank line or start of different block type
    size_t blank_line = buffer_.find("\n\n");
    if (blank_line != std::string::npos) {
        // Extract up to and including the blank line
        std::string block = buffer_.substr(0, blank_line + 2);
        buffer_.erase(0, blank_line + 2);
        return block;
    }

    // Check if different block type starts after first line
    size_t pos = first_newline + 1;
    if (pos < buffer_.length()) {
        size_t next_newline = buffer_.find('\n', pos);
        if (next_newline != std::string::npos) {
            std::string next_line = buffer_.substr(pos, next_newline - pos);

            if (is_heading(next_line) || is_thematic_break(next_line) ||
                is_list_item(next_line) || is_blockquote(next_line) ||
                is_table_row(next_line) || is_code_fence(next_line, fence_len, fence_chars)) {
                // Extract just the first line (paragraph before new block)
                std::string block = buffer_.substr(0, first_newline + 1);
                buffer_.erase(0, first_newline + 1);
                return block;
            }
        }
    }

    return "";
}

std::string MarkdownRenderer::render_markdown(const std::string& markdown) {
    if (markdown.empty()) {
        return "";
    }

    // Determine block type for blank line handling
    // Blocks needing blank BEFORE: list, table, blockquote, code block
    // Blocks needing blank AFTER: list, table, heading, blockquote, code block
    bool is_table_block = false;
    bool is_list_block = false;
    bool is_blockquote_block = false;
    bool is_code_block_type = false;
    bool is_heading_block = false;

    size_t first_non_space = markdown.find_first_not_of(" \n");
    if (first_non_space != std::string::npos) {
        std::string first_line = markdown.substr(first_non_space);
        size_t nl = first_line.find('\n');
        if (nl != std::string::npos) {
            first_line = first_line.substr(0, nl);
        }

        if (markdown[first_non_space] == '|') {
            is_table_block = true;
        } else if (is_list_item(first_line)) {
            is_list_block = true;
        } else if (is_blockquote(first_line)) {
            is_blockquote_block = true;
        } else if (is_heading(first_line)) {
            is_heading_block = true;
        } else {
            // Check for code fence
            size_t fence_len;
            std::string fence_chars;
            if (is_code_fence(first_line, fence_len, fence_chars)) {
                is_code_block_type = true;
            }
        }
    }

    // Determine if this block needs blank before/after
    // Special case: consecutive list items don't need blank lines between them
    bool is_continuing_list = is_list_block && prev_was_list_item_;

    bool needs_blank_before = (is_list_block || is_table_block || is_blockquote_block || is_code_block_type || is_heading_block) && !is_continuing_list;
    bool needs_blank_after = is_list_block || is_table_block || is_heading_block || is_blockquote_block || is_code_block_type;

    // Build result with proper blank line handling
    std::string result;

    // Add blank line before if needed (merging with previous block's blank-after)
    // But don't add if this is a continuation of a list sequence
    if ((needs_blank_before || needs_blank_before_next_) && !is_continuing_list) {
        result += "\n";
    }
    // Reset the flag - we've handled the blank line (either by adding one or not needing one)
    needs_blank_before_next_ = false;

    // Render the content
    if (is_table_block) {
        result += render_table(markdown);
    } else {
        // Use cmark for other block types
        cmark_node* doc = cmark_parse_document(
            markdown.c_str(),
            markdown.length(),
            CMARK_OPT_DEFAULT
        );

        if (!doc) {
            result += markdown;  // Fallback to raw text
        } else {
            cmark_iter* iter = cmark_iter_new(doc);
            cmark_event_type ev_type;

            bool in_list = false;
            std::vector<bool> list_ordered_stack;  // Track ordered/unordered for each nesting level
            std::vector<int> list_number_stack;    // Track item numbers for each nesting level
            std::string current_indent;  // Indent string for nested content

            while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
                cmark_node* cur = cmark_iter_get_node(iter);
                cmark_node_type type = cmark_node_get_type(cur);
                bool entering = (ev_type == CMARK_EVENT_ENTER);

                switch (type) {
                    case CMARK_NODE_DOCUMENT:
                        // Ignore document node
                        break;

                    case CMARK_NODE_HEADING: {
                        if (entering) {
                            int level = cmark_node_get_heading_level(cur);
                            std::string text = collect_text(cur);
                            result += heading(text, level);
                            // Skip children since we collected the text
                            cmark_iter_reset(iter, cur, CMARK_EVENT_EXIT);
                        }
                        break;
                    }

                    case CMARK_NODE_PARAGRAPH:
                        if (!entering) {
                            result += "\n";
                        }
                        break;

                    case CMARK_NODE_TEXT: {
                        if (entering) {
                            const char* text = cmark_node_get_literal(cur);
                            if (text) {
                                result += text;
                            }
                        }
                        break;
                    }

                    case CMARK_NODE_SOFTBREAK:
                        result += " ";
                        break;

                    case CMARK_NODE_LINEBREAK:
                        result += "\n";
                        break;

                    case CMARK_NODE_CODE: {
                        if (entering) {
                            const char* literal = cmark_node_get_literal(cur);
                            result += code(literal ? literal : "");
                        }
                        break;
                    }

                    case CMARK_NODE_CODE_BLOCK: {
                        if (entering) {
                            const char* info = cmark_node_get_fence_info(cur);
                            const char* literal = cmark_node_get_literal(cur);
                            std::string block = code_block(literal ? literal : "", info ? info : "");
                            // Apply current indent to each line of the code block
                            if (!current_indent.empty()) {
                                std::string indented;
                                std::istringstream stream(block);
                                std::string line;
                                while (std::getline(stream, line)) {
                                    indented += current_indent + line + "\n";
                                }
                                result += indented;
                            } else {
                                result += block;
                            }
                        }
                        break;
                    }

                    case CMARK_NODE_STRONG: {
                        if (entering) {
                            std::string text = collect_text(cur);
                            result += bold(text);
                            cmark_iter_reset(iter, cur, CMARK_EVENT_EXIT);
                        }
                        break;
                    }

                    case CMARK_NODE_EMPH: {
                        if (entering) {
                            std::string text = collect_text(cur);
                            result += italic(text);
                            cmark_iter_reset(iter, cur, CMARK_EVENT_EXIT);
                        }
                        break;
                    }

                    case CMARK_NODE_LINK: {
                        if (entering) {
                            std::string text = collect_text(cur);
                            const char* url = cmark_node_get_url(cur);
                            result += link(text, url ? url : "");
                            cmark_iter_reset(iter, cur, CMARK_EVENT_EXIT);
                        }
                        break;
                    }

                    case CMARK_NODE_IMAGE: {
                        if (entering) {
                            std::string alt = collect_text(cur);
                            const char* url = cmark_node_get_url(cur);
                            result += ansi(DIM) + "[image: " + alt + "]" + ansi(RESET);
                            if (url && strlen(url) > 0) {
                                result += " " + ansi(UNDERLINE) + ansi(BLUE) + url + ansi(RESET);
                            }
                            cmark_iter_reset(iter, cur, CMARK_EVENT_EXIT);
                        }
                        break;
                    }

                    case CMARK_NODE_LIST: {
                        if (entering) {
                            // No blank line here - handled at the block level
                            in_list = true;
                            // Push this list's type and starting number onto the stacks
                            bool is_ordered = (cmark_node_get_list_type(cur) == CMARK_ORDERED_LIST);
                            list_ordered_stack.push_back(is_ordered);
                            list_number_stack.push_back(cmark_node_get_list_start(cur));
                        } else {
                            // Pop this list's state
                            if (!list_ordered_stack.empty()) {
                                list_ordered_stack.pop_back();
                                list_number_stack.pop_back();
                            }
                            in_list = !list_ordered_stack.empty();
                            // No blank line here - handled at the block level
                        }
                        break;
                    }

                    case CMARK_NODE_ITEM: {
                        if (entering) {
                            // Output the list marker, content will follow from children
                            int indent_level = static_cast<int>(list_ordered_stack.size()) - 1;
                            if (indent_level < 0) indent_level = 0;
                            std::string prefix = "  " + std::string(indent_level * 2, ' ');

                            // Colors cycle based on nesting level
                            static const char* bullet_colors[] = {CYAN, YELLOW, GREEN, MAGENTA, BLUE};
                            const char* bullet_color = bullet_colors[indent_level % 5];

                            // Get current list's ordered state and item number
                            bool is_ordered = !list_ordered_stack.empty() && list_ordered_stack.back();
                            if (is_ordered) {
                                int item_num = list_number_stack.empty() ? 1 : list_number_stack.back();
                                prefix += ansi(bullet_color) + std::to_string(item_num) + "." + ansi(RESET) + " ";
                                // Increment the number for next item
                                if (!list_number_stack.empty()) {
                                    list_number_stack.back()++;
                                }
                            } else {
                                prefix += ansi(bullet_color) + "●" + ansi(RESET) + " ";
                            }
                            result += prefix;
                            // Set indent for nested content (code blocks, etc.)
                            // "  " base + indent_level + "   " for content alignment (past the marker)
                            current_indent = "  " + std::string(indent_level * 2, ' ') + "   ";
                        } else {
                            current_indent.clear();
                        }
                        // Don't skip children - let them be processed naturally
                        break;
                    }

                    case CMARK_NODE_BLOCK_QUOTE: {
                        if (entering) {
                            // Collect all text in the blockquote
                            std::string quote_text = collect_text(cur);
                            std::istringstream stream(quote_text);
                            std::string line;
                            while (std::getline(stream, line)) {
                                result += blockquote_line(line);
                            }
                            cmark_iter_reset(iter, cur, CMARK_EVENT_EXIT);
                        }
                        break;
                    }

                    case CMARK_NODE_THEMATIC_BREAK: {
                        if (entering) {
                            result += horizontal_rule();
                        }
                        break;
                    }

                    case CMARK_NODE_HTML_BLOCK:
                    case CMARK_NODE_HTML_INLINE: {
                        if (entering) {
                            const char* html = cmark_node_get_literal(cur);
                            if (html) {
                                result += ansi(DIM) + html + ansi(RESET);
                            }
                        }
                        break;
                    }

                    default:
                        // Other node types - just pass through
                        break;
                }
            }

            cmark_iter_free(iter);
            cmark_node_free(doc);
        }
    }

    // Set flag for next block if this block needs blank after
    if (needs_blank_after) {
        needs_blank_before_next_ = true;
    }

    // Special case: if we just transitioned OUT of a list, the blank-after was already
    // set by the last list item, so that's correct. But if we're still IN a list sequence,
    // we don't want blank lines between items, so we need to suppress the flag.
    // The flag will be set correctly when we finally exit the list.

    // Track list item sequences
    prev_was_list_item_ = is_list_block;

    return result;
}

std::string MarkdownRenderer::ansi(const char* code) const {
    return colors_enabled_ ? code : "";
}

std::string MarkdownRenderer::bold(const std::string& text) const {
    return ansi(BOLD) + text + ansi(RESET);
}

std::string MarkdownRenderer::italic(const std::string& text) const {
    return ansi(ITALIC) + text + ansi(RESET);
}

std::string MarkdownRenderer::code(const std::string& text) const {
    return ansi(CYAN) + "`" + text + "`" + ansi(RESET);
}

std::string MarkdownRenderer::code_block(const std::string& text, const std::string& lang) const {
    std::string result;

    // Helper to repeat a UTF-8 string
    auto repeat = [](const std::string& s, size_t n) {
        std::string result;
        result.reserve(s.length() * n);
        for (size_t i = 0; i < n; i++) {
            result += s;
        }
        return result;
    };

    // Calculate the width needed for the box
    // Find the longest line in the code
    size_t max_line_len = 0;
    std::istringstream measure_stream(text);
    std::string measure_line;
    while (std::getline(measure_stream, measure_line)) {
        if (measure_line.length() > max_line_len) {
            max_line_len = measure_line.length();
        }
    }

    // Minimum box width, accounting for language label
    size_t label_len = lang.empty() ? 0 : lang.length() + 3;  // "[lang]" with brackets and dash
    size_t min_width = 40;
    size_t box_width = std::max({min_width, max_line_len + 4, label_len + 10});

    // Top border: ┌─[lang]─────────────────────────┐
    result += ansi(DIM) + "┌";
    size_t top_fill = box_width - 2;  // -2 for corners
    if (!lang.empty()) {
        result += "─[" + std::string(ansi(RESET)) + ansi(YELLOW) + lang + ansi(RESET) + ansi(DIM) + "]";
        top_fill -= (lang.length() + 3);  // "[lang]"
    }
    result += repeat("─", top_fill) + "┐" + std::string(ansi(RESET)) + "\n";

    // Code content with left and right borders
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        size_t padding = box_width - 4 - line.length();  // -4 for "│ " and " │"
        result += ansi(DIM) + "│" + ansi(RESET) + " " + ansi(GREEN) + line + ansi(RESET);
        result += std::string(padding, ' ') + ansi(DIM) + " │" + ansi(RESET) + "\n";
    }

    // Bottom border: └─────────────────────────────────┘
    result += ansi(DIM) + "└" + repeat("─", box_width - 2) + "┘" + std::string(ansi(RESET)) + "\n";

    return result;
}

std::string MarkdownRenderer::heading(const std::string& text, int level) const {
    std::string prefix;
    const char* color;

    switch (level) {
        case 1:
            color = GREEN;
            prefix = "# ";
            break;
        case 2:
            color = BLUE;
            prefix = "## ";
            break;
        case 3:
            color = CYAN;
            prefix = "### ";
            break;
        default:
            color = MAGENTA;
            prefix = std::string(level, '#') + " ";
            break;
    }

    return ansi(BOLD) + ansi(color) + prefix + text + ansi(RESET) + "\n";
}

std::string MarkdownRenderer::link(const std::string& text, const std::string& url) const {
    // Use OSC 8 hyperlinks for terminals that support them
    // Fallback gracefully in terminals that don't
    if (colors_enabled_) {
        return "\033]8;;" + url + "\033\\" +
               ansi(UNDERLINE) + ansi(BLUE) + text + ansi(RESET) +
               "\033]8;;\033\\";
    }
    return text + " <" + url + ">";
}

std::string MarkdownRenderer::blockquote_line(const std::string& text) const {
    return ansi(DIM) + "│ " + ansi(RESET) + ansi(ITALIC) + text + ansi(RESET) + "\n";
}

std::string MarkdownRenderer::list_item(const std::string& text, bool ordered, int number, int indent) const {
    // Colors cycle based on nesting level
    static const char* bullet_colors[] = {CYAN, YELLOW, GREEN, MAGENTA, BLUE};
    const char* bullet_color = bullet_colors[indent % 5];

    // Base indent of 2 spaces, plus additional indent for nested lists
    std::string prefix = "  " + std::string(indent * 2, ' ');
    if (ordered) {
        prefix += ansi(bullet_color) + std::to_string(number) + "." + ansi(RESET) + " ";
    } else {
        prefix += ansi(bullet_color) + "●" + ansi(RESET) + " ";
    }
    return prefix + text + "\n";
}

std::string MarkdownRenderer::horizontal_rule() const {
    return ansi(DIM) + "────────────────────────────────────────" + ansi(RESET) + "\n";
}

std::string MarkdownRenderer::render_inline(const std::string& text) const {
    // Parse text as markdown and render only inline elements
    cmark_node* doc = cmark_parse_document(text.c_str(), text.length(), CMARK_OPT_DEFAULT);
    if (!doc) {
        return text;
    }

    std::string result;
    cmark_iter* iter = cmark_iter_new(doc);
    cmark_event_type ev_type;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node* cur = cmark_iter_get_node(iter);
        bool entering = (ev_type == CMARK_EVENT_ENTER);
        cmark_node_type type = cmark_node_get_type(cur);

        switch (type) {
            case CMARK_NODE_TEXT: {
                if (entering) {
                    const char* literal = cmark_node_get_literal(cur);
                    if (literal) {
                        result += literal;
                    }
                }
                break;
            }

            case CMARK_NODE_SOFTBREAK:
                result += " ";
                break;

            case CMARK_NODE_LINEBREAK:
                result += " ";  // In table cells, convert to space
                break;

            case CMARK_NODE_CODE: {
                if (entering) {
                    const char* literal = cmark_node_get_literal(cur);
                    result += code(literal ? literal : "");
                }
                break;
            }

            case CMARK_NODE_STRONG: {
                if (entering) {
                    std::string inner = collect_text(cur);
                    result += bold(inner);
                    cmark_iter_reset(iter, cur, CMARK_EVENT_EXIT);
                }
                break;
            }

            case CMARK_NODE_EMPH: {
                if (entering) {
                    std::string inner = collect_text(cur);
                    result += italic(inner);
                    cmark_iter_reset(iter, cur, CMARK_EVENT_EXIT);
                }
                break;
            }

            case CMARK_NODE_LINK: {
                if (entering) {
                    std::string linktext = collect_text(cur);
                    const char* url = cmark_node_get_url(cur);
                    result += link(linktext, url ? url : "");
                    cmark_iter_reset(iter, cur, CMARK_EVENT_EXIT);
                }
                break;
            }

            // Skip block-level elements (document, paragraph wrappers)
            case CMARK_NODE_DOCUMENT:
            case CMARK_NODE_PARAGRAPH:
                break;

            default:
                break;
        }
    }

    cmark_iter_free(iter);
    cmark_node_free(doc);

    return result;
}

std::string MarkdownRenderer::render_table(const std::string& table_text) const {
    // Parse table into rows and cells
    std::vector<std::vector<std::string>> rows;
    std::istringstream stream(table_text);
    std::string line;
    int separator_row = -1;

    while (std::getline(stream, line)) {
        // Skip empty lines
        if (line.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }

        // Check if this is a separator row
        if (is_table_separator(line)) {
            separator_row = static_cast<int>(rows.size());
            continue;  // Skip separator row in output
        }

        // Parse cells from this row
        std::vector<std::string> cells;
        size_t pos = 0;

        // Skip leading |
        size_t start = line.find('|');
        if (start == std::string::npos) continue;
        pos = start + 1;

        while (pos < line.length()) {
            size_t next_pipe = line.find('|', pos);
            if (next_pipe == std::string::npos) {
                break;
            }

            // Extract cell content and trim whitespace
            std::string cell = line.substr(pos, next_pipe - pos);
            size_t cell_start = cell.find_first_not_of(' ');
            size_t cell_end = cell.find_last_not_of(' ');
            if (cell_start != std::string::npos && cell_end != std::string::npos) {
                cell = cell.substr(cell_start, cell_end - cell_start + 1);
            } else {
                cell = "";
            }
            cells.push_back(cell);
            pos = next_pipe + 1;
        }

        if (!cells.empty()) {
            rows.push_back(cells);
        }
    }

    if (rows.empty()) {
        return table_text;  // Fallback to raw
    }

    // Render inline markdown in each cell
    for (auto& row : rows) {
        for (auto& cell : row) {
            cell = render_inline(cell);
        }
    }

    // Determine number of columns
    size_t num_cols = 0;
    for (const auto& row : rows) {
        num_cols = std::max(num_cols, row.size());
    }

    if (num_cols == 0) {
        return table_text;
    }

    // Calculate available width - always use actual terminal width
    int available_width;
    if (terminal_width_ > 0) {
        available_width = terminal_width_;
    } else {
        // Auto-detect or buffer-only mode - get real terminal width
        available_width = terminal::get_width();
    }

    // Reserve space for borders: │ col │ col │ = (num_cols + 1) border chars + 2 spaces per col
    int border_overhead = static_cast<int>(num_cols + 1 + num_cols * 2);
    int content_width = available_width - border_overhead;

    if (content_width < static_cast<int>(num_cols)) {
        // Terminal too narrow for this table - use minimum viable width
        content_width = static_cast<int>(num_cols);
    }

    // Calculate column widths based on content
    std::vector<size_t> col_widths(num_cols, 0);
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size() && i < num_cols; i++) {
            col_widths[i] = std::max(col_widths[i], display_width(row[i]));
        }
    }

    // Calculate total content width needed
    size_t total_content = 0;
    for (size_t w : col_widths) {
        total_content += w;
    }

    // Scale columns to fit available width if needed
    if (total_content > static_cast<size_t>(content_width)) {
        // Minimum column width of 8, unless that would overflow
        size_t min_width = 8;
        size_t min_total = num_cols * min_width;

        if (min_total <= static_cast<size_t>(content_width)) {
            // Scale columns proportionally to fit exactly within content_width
            // Each column gets a share proportional to its original width
            std::vector<size_t> new_widths(num_cols);
            size_t remaining = static_cast<size_t>(content_width);

            for (size_t i = 0; i < num_cols; i++) {
                // Calculate proportional width, but ensure minimum of min_width
                double ratio = static_cast<double>(col_widths[i]) / total_content;
                size_t proportional = static_cast<size_t>(ratio * content_width + 0.5);
                new_widths[i] = std::max(min_width, proportional);
                remaining -= new_widths[i];
            }

            // Adjust if we went over (due to minimums or rounding)
            size_t new_total = 0;
            for (size_t w : new_widths) new_total += w;

            if (new_total > static_cast<size_t>(content_width)) {
                // Reduce the largest columns to fit
                while (new_total > static_cast<size_t>(content_width)) {
                    size_t max_idx = 0;
                    for (size_t i = 1; i < num_cols; i++) {
                        if (new_widths[i] > new_widths[max_idx]) {
                            max_idx = i;
                        }
                    }
                    if (new_widths[max_idx] > min_width) {
                        new_widths[max_idx]--;
                        new_total--;
                    } else {
                        break; // All at minimum, can't reduce further
                    }
                }
            }

            col_widths = new_widths;
        }
        // else: too many columns for min width of 8 - allow overflow (acceptable per user)
    }

    // Helper to wrap text into lines of max display width (UTF-8 and ANSI aware)
    // Preserves ANSI formatting across line breaks
    auto wrap_text = [](const std::string& text, size_t width) -> std::vector<std::string> {
        std::vector<std::string> lines;
        if (text.empty() || width == 0) {
            lines.push_back("");
            return lines;
        }

        // First pass: split into lines based on display width
        size_t byte_pos = 0;
        while (byte_pos < text.length()) {
            std::string remaining = text.substr(byte_pos);
            size_t remaining_display_width = display_width(remaining);

            if (remaining_display_width <= width) {
                lines.push_back(remaining);
                break;
            }

            // Find where to break
            size_t break_byte_pos = advance_by_display_chars(text, byte_pos, width);

            // Look for a space to break at
            size_t last_space = text.rfind(' ', break_byte_pos);
            if (last_space != std::string::npos && last_space > byte_pos) {
                break_byte_pos = last_space;
            }

            lines.push_back(text.substr(byte_pos, break_byte_pos - byte_pos));
            byte_pos = break_byte_pos;

            if (byte_pos < text.length() && text[byte_pos] == ' ') {
                byte_pos++;
            }
        }

        if (lines.empty()) {
            lines.push_back("");
            return lines;
        }

        // Second pass: track ANSI state and fix up continuation lines
        bool is_bold = false, is_italic = false, is_underline = false;
        bool is_dim = false, is_cyan = false, is_blue = false;

        for (size_t line_idx = 0; line_idx < lines.size(); line_idx++) {
            // Prepend active formatting to continuation lines
            if (line_idx > 0) {
                std::string prefix;
                if (is_bold) prefix += "\033[1m";
                if (is_italic) prefix += "\033[3m";
                if (is_underline) prefix += "\033[4m";
                if (is_dim) prefix += "\033[2m";
                if (is_cyan) prefix += "\033[36m";
                if (is_blue) prefix += "\033[34m";
                if (!prefix.empty()) {
                    lines[line_idx] = prefix + lines[line_idx];
                }
            }

            // Scan line to track formatting state for next line
            const std::string& ln = lines[line_idx];
            for (size_t i = 0; i < ln.length(); ) {
                if (ln[i] == '\033' && i + 1 < ln.length() && ln[i + 1] == '[') {
                    size_t j = i + 2;
                    while (j < ln.length() && !((ln[j] >= 'A' && ln[j] <= 'Z') || (ln[j] >= 'a' && ln[j] <= 'z'))) {
                        j++;
                    }
                    if (j < ln.length()) {
                        std::string code = ln.substr(i + 2, j - i - 2);
                        if (code == "0") {
                            is_bold = is_italic = is_underline = is_dim = is_cyan = is_blue = false;
                        } else if (code == "1") is_bold = true;
                        else if (code == "3") is_italic = true;
                        else if (code == "4") is_underline = true;
                        else if (code == "2") is_dim = true;
                        else if (code == "36") is_cyan = true;
                        else if (code == "34") is_blue = true;
                        i = j + 1;
                        continue;
                    }
                }
                i++;
            }
        }

        return lines;
    };

    // Helper to repeat a string
    auto repeat = [](const std::string& s, size_t n) {
        std::string result;
        result.reserve(s.length() * n);
        for (size_t i = 0; i < n; i++) {
            result += s;
        }
        return result;
    };

    // Build the rendered table
    std::string result;

    // Top border
    result += ansi(DIM) + "┌";
    for (size_t i = 0; i < num_cols; i++) {
        result += repeat("─", col_widths[i] + 2);
        result += (i < num_cols - 1) ? "┬" : "┐";
    }
    result += ansi(RESET) + "\n";

    // Render each row
    for (size_t row_idx = 0; row_idx < rows.size(); row_idx++) {
        const auto& row = rows[row_idx];
        bool is_header = (separator_row == 1 && row_idx == 0);

        // Wrap each cell's content
        std::vector<std::vector<std::string>> wrapped_cells(num_cols);
        size_t max_lines = 1;
        for (size_t i = 0; i < num_cols; i++) {
            std::string cell_content = (i < row.size()) ? row[i] : "";
            wrapped_cells[i] = wrap_text(cell_content, col_widths[i]);
            max_lines = std::max(max_lines, wrapped_cells[i].size());
        }

        // Render each line of the row
        for (size_t line_idx = 0; line_idx < max_lines; line_idx++) {
            result += ansi(DIM) + "│" + ansi(RESET);
            for (size_t col = 0; col < num_cols; col++) {
                std::string cell_line;
                if (line_idx < wrapped_cells[col].size()) {
                    cell_line = wrapped_cells[col][line_idx];
                }

                // Pad to column width (use display width for UTF-8)
                size_t cell_display_width = display_width(cell_line);
                size_t padding = (col_widths[col] > cell_display_width)
                    ? col_widths[col] - cell_display_width : 0;
                result += " ";
                if (is_header) {
                    result += ansi(BOLD);
                }
                result += cell_line;
                if (is_header) {
                    result += ansi(RESET);
                }
                result += std::string(padding, ' ');
                result += " " + ansi(DIM) + "│" + ansi(RESET);
            }
            result += "\n";
        }

        // Draw separator after header or between rows
        if (is_header || row_idx < rows.size() - 1) {
            result += ansi(DIM);
            result += (is_header) ? "├" : "├";
            for (size_t i = 0; i < num_cols; i++) {
                result += repeat(is_header ? "═" : "─", col_widths[i] + 2);
                result += (i < num_cols - 1) ? (is_header ? "╪" : "┼") : (is_header ? "┤" : "┤");
            }
            result += ansi(RESET) + "\n";
        }
    }

    // Bottom border
    result += ansi(DIM) + "└";
    for (size_t i = 0; i < num_cols; i++) {
        result += repeat("─", col_widths[i] + 2);
        result += (i < num_cols - 1) ? "┴" : "┘";
    }
    result += ansi(RESET) + "\n";

    return result;
}

} // namespace rag
