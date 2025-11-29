#include "markdown_renderer.hpp"
#include "terminal.hpp"
#include <cmark.h>
#include <sstream>
#include <cstring>
#include <vector>
#include <algorithm>

namespace rag {

namespace {
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
        } else {
            terminal_width_ = -1;  // Disable rewrite for non-TTY
        }
    } else {
        terminal_width_ = terminal_width;
    }
}

void MarkdownRenderer::feed(const std::string& delta) {
    buffer_ += delta;

    // In hybrid mode, output raw text immediately
    output_raw(delta);

    // Process complete blocks
    while (has_complete_block()) {
        std::string block = extract_complete_block();
        std::string formatted = render_markdown(block);

        // In hybrid mode, raw_output_ contains everything we've output.
        // After extraction, buffer_ contains remaining (unprocessed) content.
        // So the portion to rewrite = raw_output_.length() - buffer_.length()
        // (which equals the block we just extracted)
        size_t rewrite_len = raw_output_.length() - buffer_.length();

        // Replace raw output with formatted version
        rewrite_block(rewrite_len, formatted);
    }
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
}

void MarkdownRenderer::output_raw(const std::string& text) {
    if (terminal_width_ < 0) {
        // Rewrite disabled, don't output raw text
        return;
    }

    // Output the raw text immediately
    output_(text);

    // Track it for later rewrite
    raw_output_ += text;
}

void MarkdownRenderer::rewrite_block(size_t raw_len, const std::string& formatted) {
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

    // Check if buffer starts with a code fence or table row
    size_t first_newline = buffer_.find('\n');
    if (first_newline != std::string::npos) {
        std::string first_line = buffer_.substr(0, first_newline);
        size_t fence_len;
        std::string fence_chars;
        if (is_code_fence(first_line, fence_len, fence_chars)) {
            // Starting a code block - don't have a complete block yet
            return false;
        }

        // Check if buffer starts with a table row - scan to find end
        if (is_table_row(first_line)) {
            // Scan through the buffer to see if we have a complete table
            size_t pos = first_newline + 1;
            while (pos < buffer_.length()) {
                size_t next_newline = buffer_.find('\n', pos);
                if (next_newline == std::string::npos) {
                    // No complete line yet, wait for more
                    return false;
                }
                std::string line = buffer_.substr(pos, next_newline - pos);
                if (!is_table_row(line)) {
                    // Found non-table line, table is complete
                    return true;
                }
                pos = next_newline + 1;
            }
            // All lines so far are table rows, wait for more
            return false;
        }
    }

    // Look for paragraph breaks (blank line)
    size_t blank_line = buffer_.find("\n\n");
    if (blank_line != std::string::npos) {
        return true;
    }

    // Any complete line (ending with newline) can be rendered incrementally
    size_t newline = buffer_.find('\n');
    if (newline != std::string::npos) {
        return true;
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
                // Include the closing fence line
                std::string block = buffer_.substr(0, pos + 1);
                buffer_.erase(0, pos + 1);
                in_code_block_ = false;
                code_fence_length_ = 0;
                code_fence_chars_.clear();
                return block;
            }
            pos++;
        }
    }

    // Check for code fence or table start
    size_t first_newline = buffer_.find('\n');
    if (first_newline != std::string::npos) {
        std::string first_line = buffer_.substr(0, first_newline);
        size_t fence_len;
        std::string fence_chars;
        if (is_code_fence(first_line, fence_len, fence_chars)) {
            in_code_block_ = true;
            code_fence_length_ = fence_len;
            code_fence_chars_ = fence_chars;
            // Don't extract yet - wait for closing fence
            return "";
        }

        // Check for table - extract all consecutive table rows
        if (is_table_row(first_line)) {
            size_t last_table_row_end = first_newline;
            size_t pos = first_newline + 1;

            while (pos < buffer_.length()) {
                size_t next_newline = buffer_.find('\n', pos);
                if (next_newline == std::string::npos) {
                    // Incomplete line, shouldn't happen since has_complete_block passed
                    break;
                }
                std::string line = buffer_.substr(pos, next_newline - pos);
                if (is_table_row(line)) {
                    last_table_row_end = next_newline;
                    pos = next_newline + 1;
                } else {
                    // Non-table line - extract the table
                    break;
                }
            }

            std::string block = buffer_.substr(0, last_table_row_end + 1);
            buffer_.erase(0, last_table_row_end + 1);
            return block;
        }
    }

    // Check for blank line (paragraph separator)
    size_t blank_line = buffer_.find("\n\n");
    if (blank_line != std::string::npos) {
        std::string block = buffer_.substr(0, blank_line + 2);
        buffer_.erase(0, blank_line + 2);
        return block;
    }

    // Extract any complete line for incremental rendering
    size_t newline = buffer_.find('\n');
    if (newline != std::string::npos) {
        std::string block = buffer_.substr(0, newline + 1);
        buffer_.erase(0, newline + 1);
        return block;
    }

    return "";
}

std::string MarkdownRenderer::render_markdown(const std::string& markdown) {
    if (markdown.empty()) {
        return "";
    }

    // Check if this is a table (starts with |)
    size_t first_non_space = markdown.find_first_not_of(" \n");
    if (first_non_space != std::string::npos && markdown[first_non_space] == '|') {
        return render_table(markdown);
    }

    cmark_node* doc = cmark_parse_document(
        markdown.c_str(),
        markdown.length(),
        CMARK_OPT_DEFAULT
    );

    if (!doc) {
        return markdown;  // Fallback to raw text
    }

    std::string result;
    cmark_iter* iter = cmark_iter_new(doc);
    cmark_event_type ev_type;

    bool in_list = false;
    bool in_ordered_list = false;
    int list_item_number = 0;
    int list_indent = 0;

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
                    if (!in_list) {
                        result += "\n";
                    }
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
                    result += code_block(literal ? literal : "", info ? info : "");
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
                    in_list = true;
                    in_ordered_list = (cmark_node_get_list_type(cur) == CMARK_ORDERED_LIST);
                    list_item_number = cmark_node_get_list_start(cur);
                    list_indent = 0;
                } else {
                    in_list = false;
                    result += "\n";
                }
                break;
            }

            case CMARK_NODE_ITEM: {
                if (entering) {
                    // Get the text content of this list item
                    std::string item_text = collect_text(cur);
                    result += list_item(item_text, in_ordered_list, list_item_number, list_indent);
                    if (in_ordered_list) {
                        list_item_number++;
                    }
                    cmark_iter_reset(iter, cur, CMARK_EVENT_EXIT);
                }
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

    return "\n" + ansi(BOLD) + ansi(color) + prefix + text + ansi(RESET) + "\n";
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
    std::string prefix(indent * 2, ' ');
    if (ordered) {
        prefix += std::to_string(number) + ". ";
    } else {
        prefix += ansi(CYAN) + "•" + ansi(RESET) + " ";
    }
    return prefix + text + "\n";
}

std::string MarkdownRenderer::horizontal_rule() const {
    return ansi(DIM) + "────────────────────────────────────────" + ansi(RESET) + "\n";
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

    // Determine number of columns
    size_t num_cols = 0;
    for (const auto& row : rows) {
        num_cols = std::max(num_cols, row.size());
    }

    if (num_cols == 0) {
        return table_text;
    }

    // Calculate available width
    int available_width = terminal_width_ > 0 ? terminal_width_ : 80;

    // Reserve space for borders: │ col │ col │ = (num_cols + 1) pipes + 2 spaces per col
    int border_overhead = static_cast<int>(num_cols + 1 + num_cols * 2);
    int content_width = available_width - border_overhead;

    if (content_width < static_cast<int>(num_cols * 5)) {
        // Too narrow, fall back to simpler rendering
        content_width = static_cast<int>(num_cols * 10);
    }

    // Calculate column widths based on content
    std::vector<size_t> col_widths(num_cols, 0);
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size() && i < num_cols; i++) {
            col_widths[i] = std::max(col_widths[i], row[i].length());
        }
    }

    // Scale columns to fit available width
    size_t total_content = 0;
    for (size_t w : col_widths) {
        total_content += w;
    }

    if (total_content > static_cast<size_t>(content_width)) {
        // Need to scale down - distribute width proportionally but ensure minimum
        size_t min_width = 8;
        std::vector<size_t> new_widths(num_cols);

        for (size_t i = 0; i < num_cols; i++) {
            double ratio = static_cast<double>(col_widths[i]) / total_content;
            new_widths[i] = std::max(min_width, static_cast<size_t>(ratio * content_width));
        }
        col_widths = new_widths;
    }

    // Helper to wrap text into lines of max width
    auto wrap_text = [](const std::string& text, size_t width) -> std::vector<std::string> {
        std::vector<std::string> lines;
        if (text.empty() || width == 0) {
            lines.push_back("");
            return lines;
        }

        size_t pos = 0;
        while (pos < text.length()) {
            size_t remaining = text.length() - pos;
            if (remaining <= width) {
                lines.push_back(text.substr(pos));
                break;
            }

            // Find last space within width, or break at width
            size_t break_at = width;
            size_t last_space = text.rfind(' ', pos + width);
            if (last_space != std::string::npos && last_space > pos) {
                break_at = last_space - pos;
            }

            lines.push_back(text.substr(pos, break_at));
            pos += break_at;

            // Skip the space if we broke at one
            if (pos < text.length() && text[pos] == ' ') {
                pos++;
            }
        }

        if (lines.empty()) {
            lines.push_back("");
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

                // Pad to column width
                size_t padding = col_widths[col] - cell_line.length();
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
