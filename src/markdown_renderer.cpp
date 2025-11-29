#include "markdown_renderer.hpp"
#include <cmark.h>
#include <sstream>
#include <cstring>

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

MarkdownRenderer::MarkdownRenderer(OutputCallback output, bool colors_enabled)
    : output_(std::move(output))
    , colors_enabled_(colors_enabled)
{}

void MarkdownRenderer::feed(const std::string& delta) {
    buffer_ += delta;

    // Process complete blocks
    while (has_complete_block()) {
        std::string block = extract_complete_block();
        std::string rendered = render_markdown(block);
        output_(rendered);
    }
}

void MarkdownRenderer::finish() {
    // Render any remaining content
    if (!buffer_.empty()) {
        std::string rendered = render_markdown(buffer_);
        output_(rendered);
        buffer_.clear();
    }
    in_code_block_ = false;
    code_fence_length_ = 0;
    code_fence_chars_.clear();
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

    // Check if buffer starts with a code fence
    size_t first_newline = buffer_.find('\n');
    if (first_newline != std::string::npos) {
        std::string first_line = buffer_.substr(0, first_newline);
        size_t fence_len;
        std::string fence_chars;
        if (is_code_fence(first_line, fence_len, fence_chars)) {
            // Starting a code block - don't have a complete block yet
            return false;
        }
    }

    // Look for paragraph breaks (blank line)
    size_t blank_line = buffer_.find("\n\n");
    if (blank_line != std::string::npos) {
        return true;
    }

    // Look for single complete lines that are self-contained blocks
    size_t newline = buffer_.find('\n');
    if (newline != std::string::npos) {
        std::string line = buffer_.substr(0, newline);

        // ATX headings
        if (!line.empty() && line[0] == '#') {
            return true;
        }

        // Thematic breaks (---, ***, ___)
        if (is_thematic_break(line)) {
            return true;
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

    // Check for code fence start
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
    }

    // Check for blank line (paragraph separator)
    size_t blank_line = buffer_.find("\n\n");
    if (blank_line != std::string::npos) {
        std::string block = buffer_.substr(0, blank_line + 2);
        buffer_.erase(0, blank_line + 2);
        return block;
    }

    // Check for self-contained single line blocks
    size_t newline = buffer_.find('\n');
    if (newline != std::string::npos) {
        std::string line = buffer_.substr(0, newline);

        // ATX headings
        if (!line.empty() && line[0] == '#') {
            std::string block = buffer_.substr(0, newline + 1);
            buffer_.erase(0, newline + 1);
            return block;
        }

        // Thematic breaks
        if (is_thematic_break(line)) {
            std::string block = buffer_.substr(0, newline + 1);
            buffer_.erase(0, newline + 1);
            return block;
        }
    }

    return "";
}

std::string MarkdownRenderer::render_markdown(const std::string& markdown) {
    if (markdown.empty()) {
        return "";
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

    // Top border with language label
    result += ansi(DIM) + "┌";
    if (!lang.empty()) {
        result += "─[" + std::string(ansi(RESET)) + ansi(YELLOW) + lang + ansi(RESET) + ansi(DIM) + "]";
    }
    result += "────────────────────────" + std::string(ansi(RESET)) + "\n";

    // Code content with left border
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        result += ansi(DIM) + "│" + ansi(RESET) + " " + ansi(GREEN) + line + ansi(RESET) + "\n";
    }

    // Bottom border
    result += ansi(DIM) + "└────────────────────────────" + std::string(ansi(RESET)) + "\n";

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

} // namespace rag
