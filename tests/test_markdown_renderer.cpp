#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "markdown_renderer.hpp"
#include <string>
#include <vector>

using namespace rag;

// Helper to collect output from renderer
class OutputCollector {
public:
    std::string result;

    void operator()(const std::string& text) {
        result += text;
    }
};

// Helper to calculate display width (handling UTF-8 multi-byte chars)
// Assumes each UTF-8 code point is 1 display column (good for box-drawing chars)
size_t display_width(const std::string& text) {
    size_t width = 0;
    for (size_t i = 0; i < text.length(); ) {
        unsigned char c = text[i];
        if ((c & 0x80) == 0) {
            // ASCII
            width++;
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte UTF-8
            width++;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte UTF-8 (includes box-drawing chars)
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

// Helper to strip ANSI codes for easier comparison
std::string strip_ansi(const std::string& text) {
    std::string result;
    bool in_escape = false;
    for (char c : text) {
        if (c == '\033') {
            in_escape = true;
        } else if (in_escape) {
            // CSI sequences end with a letter (A-Z, a-z) or certain symbols
            // SGR ends with 'm', cursor movement with letters like A,B,C,D,G,H,J,K
            // OSC sequences end with BEL (\007) or ST (\033\\)
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                c == '\\' || c == '\007') {
                in_escape = false;
            }
        } else {
            result += c;
        }
    }
    return result;
}

// ============================================================================
// Basic text rendering
// ============================================================================

TEST_CASE("Plain text passes through", "[markdown]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("Hello world");
    renderer.finish();

    REQUIRE(strip_ansi(output.result).find("Hello world") != std::string::npos);
}

TEST_CASE("Multiple plain paragraphs", "[markdown]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("First paragraph.\n\nSecond paragraph.");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("First paragraph") != std::string::npos);
    REQUIRE(result.find("Second paragraph") != std::string::npos);
}

// ============================================================================
// Headings
// ============================================================================

TEST_CASE("ATX heading level 1", "[markdown][headings]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("# Main Title\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("# Main Title") != std::string::npos);
}

TEST_CASE("ATX heading level 2", "[markdown][headings]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("## Section\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("## Section") != std::string::npos);
}

TEST_CASE("Multiple heading levels", "[markdown][headings]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("# Title\n## Section\n### Subsection\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("# Title") != std::string::npos);
    REQUIRE(result.find("## Section") != std::string::npos);
    REQUIRE(result.find("### Subsection") != std::string::npos);
}

// ============================================================================
// Inline formatting
// ============================================================================

TEST_CASE("Bold text", "[markdown][inline]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("This is **bold** text.\n\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("bold") != std::string::npos);
    // Should not contain the asterisks
    REQUIRE(result.find("**") == std::string::npos);
}

TEST_CASE("Italic text", "[markdown][inline]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("This is *italic* text.\n\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("italic") != std::string::npos);
}

TEST_CASE("Inline code", "[markdown][inline]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("Use the `printf` function.\n\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("`printf`") != std::string::npos);
}

// ============================================================================
// Code blocks
// ============================================================================

TEST_CASE("Fenced code block with language", "[markdown][code]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("```cpp\nint main() {\n    return 0;\n}\n```\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("cpp") != std::string::npos);
    REQUIRE(result.find("int main()") != std::string::npos);
    REQUIRE(result.find("return 0") != std::string::npos);
}

TEST_CASE("Fenced code block without language", "[markdown][code]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("```\nsome code\n```\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("some code") != std::string::npos);
}

TEST_CASE("Code block with tildes", "[markdown][code]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("~~~python\nprint('hello')\n~~~\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("python") != std::string::npos);
    REQUIRE(result.find("print") != std::string::npos);
}

// ============================================================================
// Lists
// ============================================================================

TEST_CASE("Unordered list", "[markdown][lists]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("- Item 1\n- Item 2\n- Item 3\n\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("Item 1") != std::string::npos);
    REQUIRE(result.find("Item 2") != std::string::npos);
    REQUIRE(result.find("Item 3") != std::string::npos);
}

TEST_CASE("Ordered list", "[markdown][lists]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("1. First\n2. Second\n3. Third\n\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("First") != std::string::npos);
    REQUIRE(result.find("Second") != std::string::npos);
    REQUIRE(result.find("Third") != std::string::npos);
}

// ============================================================================
// Thematic breaks
// ============================================================================

TEST_CASE("Thematic break with dashes", "[markdown][hr]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("Above\n\n---\n\nBelow");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("Above") != std::string::npos);
    REQUIRE(result.find("Below") != std::string::npos);
    // Should contain the horizontal rule (rendered as dashes)
    REQUIRE(result.find("────") != std::string::npos);
}

TEST_CASE("Thematic break with asterisks", "[markdown][hr]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("Above\n\n***\n\nBelow");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("────") != std::string::npos);
}

TEST_CASE("Thematic break with underscores", "[markdown][hr]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("Above\n\n___\n\nBelow");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("────") != std::string::npos);
}

TEST_CASE("is_thematic_break helper function", "[markdown][hr]") {
    // Valid thematic breaks
    REQUIRE(MarkdownRenderer::is_thematic_break("---") == true);
    REQUIRE(MarkdownRenderer::is_thematic_break("***") == true);
    REQUIRE(MarkdownRenderer::is_thematic_break("___") == true);
    REQUIRE(MarkdownRenderer::is_thematic_break("----") == true);
    REQUIRE(MarkdownRenderer::is_thematic_break("- - -") == true);
    REQUIRE(MarkdownRenderer::is_thematic_break("* * *") == true);
    REQUIRE(MarkdownRenderer::is_thematic_break("  ---") == true);  // Up to 3 spaces
    REQUIRE(MarkdownRenderer::is_thematic_break("   ---") == true);

    // Invalid thematic breaks
    REQUIRE(MarkdownRenderer::is_thematic_break("--") == false);   // Too few
    REQUIRE(MarkdownRenderer::is_thematic_break("**") == false);
    REQUIRE(MarkdownRenderer::is_thematic_break("-*-") == false);  // Mixed
    REQUIRE(MarkdownRenderer::is_thematic_break("    ---") == false);  // Too many leading spaces
    REQUIRE(MarkdownRenderer::is_thematic_break("---x") == false);  // Invalid char
    REQUIRE(MarkdownRenderer::is_thematic_break("") == false);
}

// ============================================================================
// Blockquotes
// ============================================================================

TEST_CASE("Simple blockquote", "[markdown][blockquote]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("> This is a quote\n\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("This is a quote") != std::string::npos);
}

// ============================================================================
// Links
// ============================================================================

TEST_CASE("Inline link", "[markdown][links]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("Check [this link](https://example.com) out.\n\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("this link") != std::string::npos);
}

// ============================================================================
// Streaming behavior
// ============================================================================

TEST_CASE("Streaming chunks combine correctly", "[markdown][streaming]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    // Simulate streaming: text arrives in small chunks
    renderer.feed("# He");
    renderer.feed("llo ");
    renderer.feed("World");
    renderer.feed("\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("Hello World") != std::string::npos);
}

TEST_CASE("Code fence split across chunks", "[markdown][streaming]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    // Code fence arrives in chunks
    renderer.feed("``");
    renderer.feed("`cpp\nint x = ");
    renderer.feed("5;\n`");
    renderer.feed("``\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("int x = 5") != std::string::npos);
}

TEST_CASE("Paragraph boundary detection", "[markdown][streaming]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("First paragraph.");
    // At this point, no output yet (waiting for block to complete)

    renderer.feed("\n\n");
    // Now first paragraph should be output

    renderer.feed("Second paragraph.");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("First paragraph") != std::string::npos);
    REQUIRE(result.find("Second paragraph") != std::string::npos);
}

// ============================================================================
// Colors disabled
// ============================================================================

TEST_CASE("No ANSI codes when colors disabled", "[markdown][colors]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), false);  // colors disabled

    renderer.feed("# Heading\n\n**Bold** and *italic*\n\n");
    renderer.finish();

    // Should not contain escape sequences
    REQUIRE(output.result.find("\033[") == std::string::npos);
}

TEST_CASE("ANSI codes present when colors enabled", "[markdown][colors]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true);  // colors enabled

    renderer.feed("# Heading\n\n**Bold** text\n\n");
    renderer.finish();

    // Should contain escape sequences
    REQUIRE(output.result.find("\033[") != std::string::npos);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE("Empty input", "[markdown][edge]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.finish();

    REQUIRE(output.result.empty());
}

TEST_CASE("Only whitespace", "[markdown][edge]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("   \n\n   ");
    renderer.finish();

    // Should handle gracefully (may produce whitespace but not crash)
    SUCCEED();
}

TEST_CASE("Very long line", "[markdown][edge]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    std::string long_line(1000, 'x');
    renderer.feed(long_line + "\n\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find(std::string(100, 'x')) != std::string::npos);
}

TEST_CASE("Special characters in text", "[markdown][edge]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("Special chars: <>&\"'\n\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("<>&") != std::string::npos);
}

TEST_CASE("Unicode text", "[markdown][edge]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    renderer.feed("Unicode: 你好世界 émojis 🎉\n\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("你好世界") != std::string::npos);
}

// ============================================================================
// Code fence detection
// ============================================================================

TEST_CASE("Code fence must have at least 3 backticks", "[markdown][code]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    // Two backticks should not be treated as code fence
    renderer.feed("``not code``\n\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    // Should pass through as text, not as code block
    REQUIRE(result.find("not code") != std::string::npos);
}

TEST_CASE("Longer code fence requires matching close", "[markdown][code]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output));

    // 4 backticks open, need 4+ to close
    renderer.feed("````\ncode with ``` inside\n````\n");
    renderer.finish();

    std::string result = strip_ansi(output.result);
    REQUIRE(result.find("code with ``` inside") != std::string::npos);
}

// ============================================================================
// is_thematic_break helper - private but made static for testing
// ============================================================================

TEST_CASE("is_thematic_break with spaces between markers", "[markdown][hr]") {
    REQUIRE(MarkdownRenderer::is_thematic_break("- - -") == true);
    REQUIRE(MarkdownRenderer::is_thematic_break("*  *  *") == true);
    REQUIRE(MarkdownRenderer::is_thematic_break("_   _   _") == true);
    REQUIRE(MarkdownRenderer::is_thematic_break("-  -  -  -") == true);
}

// ============================================================================
// Terminal utilities
// ============================================================================

#include "terminal.hpp"

TEST_CASE("Terminal cursor sequences", "[terminal]") {
    REQUIRE(rag::terminal::cursor::up(3) == "\033[3A");
    REQUIRE(rag::terminal::cursor::down(2) == "\033[2B");
    REQUIRE(rag::terminal::cursor::column(1) == "\033[1G");
    REQUIRE(rag::terminal::cursor::save() == "\033[s");
    REQUIRE(rag::terminal::cursor::restore() == "\033[u");
}

TEST_CASE("Terminal cursor up with zero or negative", "[terminal]") {
    REQUIRE(rag::terminal::cursor::up(0) == "");
    REQUIRE(rag::terminal::cursor::up(-1) == "");
}

TEST_CASE("Terminal clear sequences", "[terminal]") {
    REQUIRE(rag::terminal::clear::to_end_of_line() == "\033[K");
    REQUIRE(rag::terminal::clear::to_end_of_screen() == "\033[J");
    REQUIRE(rag::terminal::clear::line() == "\033[2K");
}

TEST_CASE("Terminal count_lines with simple text", "[terminal]") {
    REQUIRE(rag::terminal::count_lines("", 80) == 0);
    REQUIRE(rag::terminal::count_lines("hello", 80) == 1);
    REQUIRE(rag::terminal::count_lines("hello\n", 80) == 1);
    REQUIRE(rag::terminal::count_lines("hello\nworld", 80) == 2);
    REQUIRE(rag::terminal::count_lines("hello\nworld\n", 80) == 2);
    REQUIRE(rag::terminal::count_lines("a\nb\nc", 80) == 3);
}

TEST_CASE("Terminal count_lines with line wrapping", "[terminal]") {
    // 10 chars on 5-wide terminal = 2 lines
    REQUIRE(rag::terminal::count_lines("1234567890", 5) == 2);
    // 15 chars on 5-wide terminal = 3 lines
    REQUIRE(rag::terminal::count_lines("123456789012345", 5) == 3);
}

TEST_CASE("Terminal count_lines ignores ANSI codes", "[terminal]") {
    // ANSI codes should not count towards line width
    std::string with_ansi = "\033[1mhello\033[0m";  // "hello" with bold
    REQUIRE(rag::terminal::count_lines(with_ansi, 80) == 1);

    // Even with lots of ANSI, only 5 visible chars
    std::string lots_of_ansi = "\033[31m\033[1m\033[4mhi\033[0m";
    REQUIRE(rag::terminal::count_lines(lots_of_ansi, 80) == 1);
}

TEST_CASE("Terminal display_width", "[terminal]") {
    REQUIRE(rag::terminal::display_width("hello") == 5);
    REQUIRE(rag::terminal::display_width("") == 0);
    REQUIRE(rag::terminal::display_width("\033[1mhello\033[0m") == 5);
}

// ============================================================================
// Hybrid streaming mode
// ============================================================================

TEST_CASE("Buffer-only mode when terminal_width is -1", "[markdown][streaming]") {
    std::vector<std::string> outputs;
    MarkdownRenderer renderer([&](const std::string& s) { outputs.push_back(s); }, true, -1);

    renderer.feed("# Hello\n\n");

    // In buffer-only mode, should have only the formatted output (no raw first)
    // The first output should be the formatted heading
    REQUIRE(outputs.size() >= 1);
    std::string combined;
    for (const auto& s : outputs) combined += s;
    REQUIRE(strip_ansi(combined).find("# Hello") != std::string::npos);
}

TEST_CASE("Hybrid mode outputs raw then rewrites", "[markdown][streaming]") {
    std::vector<std::string> outputs;
    MarkdownRenderer renderer([&](const std::string& s) { outputs.push_back(s); }, true, 80);

    renderer.feed("# He");
    // Should have output raw "# He"
    REQUIRE(outputs.size() >= 1);
    REQUIRE(outputs[0].find("# He") != std::string::npos);

    renderer.feed("llo\n\n");
    // Should have more outputs including rewrite sequence

    std::string combined;
    for (const auto& s : outputs) combined += s;

    // Should contain cursor control sequences for rewrite
    REQUIRE(combined.find("\r") != std::string::npos);  // Carriage return
    REQUIRE(combined.find("\033[J") != std::string::npos);  // Clear to end

    // Final content should have the formatted heading
    REQUIRE(strip_ansi(combined).find("Hello") != std::string::npos);
}

TEST_CASE("Hybrid mode handles multiple blocks", "[markdown][streaming]") {
    std::vector<std::string> outputs;
    MarkdownRenderer renderer([&](const std::string& s) { outputs.push_back(s); }, true, 80);

    renderer.feed("First paragraph.\n\n");
    renderer.feed("Second paragraph.\n\n");
    renderer.finish();

    // Check that both paragraphs appear in at least one of the outputs
    // (the rewrite sequence makes simple string concatenation unreliable)
    bool found_first = false;
    bool found_second = false;
    for (const auto& s : outputs) {
        std::string stripped = strip_ansi(s);
        if (stripped.find("First paragraph") != std::string::npos) found_first = true;
        if (stripped.find("Second paragraph") != std::string::npos) found_second = true;
    }
    REQUIRE(found_first);
    REQUIRE(found_second);
}

TEST_CASE("Hybrid mode with code block", "[markdown][streaming]") {
    std::vector<std::string> outputs;
    MarkdownRenderer renderer([&](const std::string& s) { outputs.push_back(s); }, true, 80);

    renderer.feed("```cpp\n");
    renderer.feed("int x = 5;\n");
    renderer.feed("```\n");
    renderer.finish();

    std::string combined;
    for (const auto& s : outputs) combined += s;

    std::string stripped = strip_ansi(combined);
    REQUIRE(stripped.find("int x = 5") != std::string::npos);
}

// ============================================================================
// Code block box rendering
// ============================================================================

TEST_CASE("Code block has proper box corners", "[markdown][code]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);  // Buffer-only mode

    renderer.feed("```\ntest\n```\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    // Should have all four corners
    REQUIRE(stripped.find("┌") != std::string::npos);
    REQUIRE(stripped.find("┐") != std::string::npos);
    REQUIRE(stripped.find("└") != std::string::npos);
    REQUIRE(stripped.find("┘") != std::string::npos);

    // Should have vertical borders on both sides
    // Count left and right borders (should be equal)
    size_t left_count = 0, right_count = 0;
    size_t pos = 0;
    while ((pos = stripped.find("│", pos)) != std::string::npos) {
        // Check if it's at start of content (after newline) or end (before newline)
        if (pos > 0 && stripped[pos-1] == '\n') {
            left_count++;
        }
        pos += 3;  // UTF-8 character is 3 bytes
    }
    REQUIRE(left_count >= 1);  // At least one line of content
}

TEST_CASE("Code block with language label", "[markdown][code]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("```python\nprint('hello')\n```\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    // Should have language in brackets
    REQUIRE(stripped.find("[python]") != std::string::npos);

    // Should still have proper corners
    REQUIRE(stripped.find("┌") != std::string::npos);
    REQUIRE(stripped.find("┐") != std::string::npos);
}

TEST_CASE("Code block adjusts to content width", "[markdown][code]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("```\nshort\nthis is a much longer line of code\n```\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    // Find the top and bottom borders
    size_t top_start = stripped.find("┌");
    size_t top_end = stripped.find("┐");
    size_t bottom_start = stripped.find("└");
    size_t bottom_end = stripped.find("┘");

    REQUIRE(top_start != std::string::npos);
    REQUIRE(top_end != std::string::npos);
    REQUIRE(bottom_start != std::string::npos);
    REQUIRE(bottom_end != std::string::npos);

    // Top and bottom should be same width (corners are 3 bytes each in UTF-8)
    size_t top_width = top_end - top_start;
    size_t bottom_width = bottom_end - bottom_start;
    REQUIRE(top_width == bottom_width);
}

// ============================================================================
// Rewrite length calculation
// ============================================================================

TEST_CASE("Rewrite calculates correct length with trailing content", "[markdown][streaming]") {
    std::vector<std::string> outputs;
    MarkdownRenderer renderer([&](const std::string& s) { outputs.push_back(s); }, true, 80);

    // Feed first block completely
    renderer.feed("First.\n\n");

    // Feed second block in parts
    renderer.feed("Second");
    renderer.feed(" paragraph.\n\n");

    renderer.finish();

    // Both paragraphs should appear
    bool found_first = false;
    bool found_second = false;
    for (const auto& s : outputs) {
        std::string stripped = strip_ansi(s);
        if (stripped.find("First") != std::string::npos) found_first = true;
        if (stripped.find("Second paragraph") != std::string::npos) found_second = true;
    }
    REQUIRE(found_first);
    REQUIRE(found_second);
}

TEST_CASE("Rewrite handles interleaved content correctly", "[markdown][streaming]") {
    std::vector<std::string> outputs;
    MarkdownRenderer renderer([&](const std::string& s) { outputs.push_back(s); }, true, 80);

    // Simulate realistic streaming where blocks interleave
    renderer.feed("# Title\n");
    renderer.feed("\nParagraph text here.\n\n");
    renderer.feed("More text.\n\n");

    renderer.finish();

    bool found_title = false;
    bool found_para = false;
    bool found_more = false;
    for (const auto& s : outputs) {
        std::string stripped = strip_ansi(s);
        if (stripped.find("Title") != std::string::npos) found_title = true;
        if (stripped.find("Paragraph text") != std::string::npos) found_para = true;
        if (stripped.find("More text") != std::string::npos) found_more = true;
    }
    REQUIRE(found_title);
    REQUIRE(found_para);
    REQUIRE(found_more);
}

// ============================================================================
// Table rendering
// ============================================================================

TEST_CASE("Simple table renders with box drawing", "[markdown][table]") {
    OutputCollector output;
    // Use buffer mode (-1) for easier output verification
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("| A | B |\n");
    renderer.feed("| --- | --- |\n");
    renderer.feed("| 1 | 2 |\n\n");

    renderer.finish();

    std::string stripped = strip_ansi(output.result);
    // Should contain box drawing characters
    REQUIRE(stripped.find("┌") != std::string::npos);
    REQUIRE(stripped.find("┐") != std::string::npos);
    REQUIRE(stripped.find("└") != std::string::npos);
    REQUIRE(stripped.find("┘") != std::string::npos);
    REQUIRE(stripped.find("│") != std::string::npos);
    // Should contain content
    REQUIRE(stripped.find("A") != std::string::npos);
    REQUIRE(stripped.find("B") != std::string::npos);
    REQUIRE(stripped.find("1") != std::string::npos);
    REQUIRE(stripped.find("2") != std::string::npos);
}

TEST_CASE("Table header is bold", "[markdown][table]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("| Header |\n");
    renderer.feed("| --- |\n");
    renderer.feed("| Data |\n\n");

    renderer.finish();

    // Header should have bold escape code before it
    REQUIRE(output.result.find("\033[1m") != std::string::npos);
}

TEST_CASE("Table with multiple columns", "[markdown][table]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("| Col1 | Col2 | Col3 |\n");
    renderer.feed("| --- | --- | --- |\n");
    renderer.feed("| A | B | C |\n\n");

    renderer.finish();

    std::string stripped = strip_ansi(output.result);
    REQUIRE(stripped.find("Col1") != std::string::npos);
    REQUIRE(stripped.find("Col2") != std::string::npos);
    REQUIRE(stripped.find("Col3") != std::string::npos);
    REQUIRE(stripped.find("A") != std::string::npos);
    REQUIRE(stripped.find("B") != std::string::npos);
    REQUIRE(stripped.find("C") != std::string::npos);
}

TEST_CASE("Table with word wrapping", "[markdown][table]") {
    OutputCollector output;
    // Narrow terminal width to force wrapping
    MarkdownRenderer renderer(std::ref(output), true, 40);

    renderer.feed("| Short | This is a very long cell that should wrap |\n");
    renderer.feed("| --- | --- |\n");
    renderer.feed("| X | Y |\n\n");

    renderer.finish();

    std::string stripped = strip_ansi(output.result);
    REQUIRE(stripped.find("Short") != std::string::npos);
    REQUIRE(stripped.find("X") != std::string::npos);
    REQUIRE(stripped.find("Y") != std::string::npos);
    // The long text should appear (possibly wrapped)
    REQUIRE(stripped.find("long") != std::string::npos);
}

TEST_CASE("Table header separator uses double lines", "[markdown][table]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("| Header |\n");
    renderer.feed("| --- |\n");
    renderer.feed("| Data |\n\n");

    renderer.finish();

    std::string stripped = strip_ansi(output.result);
    // Header separator should use ═ (double line)
    REQUIRE(stripped.find("═") != std::string::npos);
}

TEST_CASE("Table streaming waits for complete table", "[markdown][table][streaming]") {
    std::vector<std::string> outputs;
    MarkdownRenderer renderer([&](const std::string& s) { outputs.push_back(s); }, true, 80);

    // Feed table incrementally
    renderer.feed("| A | B |\n");
    size_t after_header = outputs.size();

    renderer.feed("| --- | --- |\n");
    size_t after_sep = outputs.size();

    renderer.feed("| 1 | 2 |\n");
    size_t after_row = outputs.size();

    // Table shouldn't be rendered yet (still accumulating)
    // It should only output raw text in streaming mode

    // End the table with a blank line
    renderer.feed("\n");

    renderer.finish();

    // Final output should contain box drawing (table was rendered)
    std::string all_output;
    for (const auto& s : outputs) {
        all_output += s;
    }
    std::string stripped = strip_ansi(all_output);
    REQUIRE(stripped.find("┌") != std::string::npos);
}

TEST_CASE("Table without header separator still renders", "[markdown][table]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    // No separator line - should still be detected as table rows
    renderer.feed("| A | B |\n");
    renderer.feed("| 1 | 2 |\n\n");

    renderer.finish();

    std::string stripped = strip_ansi(output.result);
    REQUIRE(stripped.find("│") != std::string::npos);
    REQUIRE(stripped.find("A") != std::string::npos);
    REQUIRE(stripped.find("B") != std::string::npos);
}

TEST_CASE("Empty cells handled correctly", "[markdown][table]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("| A | | C |\n");
    renderer.feed("| --- | --- | --- |\n");
    renderer.feed("| | X | |\n\n");

    renderer.finish();

    std::string stripped = strip_ansi(output.result);
    REQUIRE(stripped.find("A") != std::string::npos);
    REQUIRE(stripped.find("C") != std::string::npos);
    REQUIRE(stripped.find("X") != std::string::npos);
}

TEST_CASE("Table followed by text", "[markdown][table]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("| A | B |\n");
    renderer.feed("| --- | --- |\n");
    renderer.feed("| 1 | 2 |\n");
    renderer.feed("Some text after\n\n");

    renderer.finish();

    std::string stripped = strip_ansi(output.result);
    // Table should be rendered
    REQUIRE(stripped.find("┌") != std::string::npos);
    // Text after should appear
    REQUIRE(stripped.find("Some text after") != std::string::npos);
}

// ============================================================================
// List formatting improvements
// ============================================================================

TEST_CASE("List items use bullet dot character", "[markdown][lists]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("* Item 1\n* Item 2\n\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);
    // Should use ● bullet character
    REQUIRE(stripped.find("●") != std::string::npos);
    // Should NOT use * as bullet
    REQUIRE(stripped.find("* Item") == std::string::npos);
}

TEST_CASE("List items are indented by 2 spaces", "[markdown][lists]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("* Item\n\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);
    // Item should be indented (starts with spaces before bullet)
    REQUIRE(stripped.find("  ●") != std::string::npos);
}

TEST_CASE("List has blank line before and after", "[markdown][lists]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("Before text.\n\n* Item 1\n* Item 2\n\nAfter text.\n\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);
    // Should have structure: text, blank, list, blank, text
    // Check that "Before text" and first bullet are separated
    size_t before_pos = stripped.find("Before text.");
    size_t bullet_pos = stripped.find("●");
    REQUIRE(before_pos != std::string::npos);
    REQUIRE(bullet_pos != std::string::npos);
    // There should be at least one blank line (two newlines) between them
    std::string between = stripped.substr(before_pos, bullet_pos - before_pos);
    REQUIRE(between.find("\n\n") != std::string::npos);
}

TEST_CASE("No extra blank lines between list items", "[markdown][lists]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("* Item 1\n* Item 2\n* Item 3\n\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);
    // Find positions of each bullet
    size_t pos1 = stripped.find("● Item 1");
    size_t pos2 = stripped.find("● Item 2");
    size_t pos3 = stripped.find("● Item 3");
    REQUIRE(pos1 != std::string::npos);
    REQUIRE(pos2 != std::string::npos);
    REQUIRE(pos3 != std::string::npos);
    // Between items should be exactly one newline (no blank lines)
    std::string between1 = stripped.substr(pos1, pos2 - pos1);
    std::string between2 = stripped.substr(pos2, pos3 - pos2);
    REQUIRE(between1.find("\n\n") == std::string::npos);
    REQUIRE(between2.find("\n\n") == std::string::npos);
}

TEST_CASE("List streaming accumulates items", "[markdown][lists][streaming]") {
    std::vector<std::string> outputs;
    MarkdownRenderer renderer([&](const std::string& s) { outputs.push_back(s); }, true, 80);

    // Feed list items one at a time
    renderer.feed("* Item 1\n");
    renderer.feed("* Item 2\n");
    renderer.feed("* Item 3\n");
    // End the list with non-list content
    renderer.feed("Done.\n\n");
    renderer.finish();

    // Combine all output
    std::string all_output;
    for (const auto& s : outputs) {
        all_output += s;
    }
    std::string stripped = strip_ansi(all_output);

    // All items should be present
    REQUIRE(stripped.find("Item 1") != std::string::npos);
    REQUIRE(stripped.find("Item 2") != std::string::npos);
    REQUIRE(stripped.find("Item 3") != std::string::npos);
    REQUIRE(stripped.find("Done") != std::string::npos);
}

TEST_CASE("List item with code block", "[markdown][lists]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("1. Install\n   ```bash\n   apt install foo\n   ```\n\nDone.\n\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);
    // Should have the numbered item
    REQUIRE(stripped.find("1.") != std::string::npos);
    REQUIRE(stripped.find("Install") != std::string::npos);
    // Should have code block with box drawing
    REQUIRE(stripped.find("┌") != std::string::npos);
    REQUIRE(stripped.find("apt install foo") != std::string::npos);
    // Should have the text after
    REQUIRE(stripped.find("Done") != std::string::npos);
}

TEST_CASE("Code block in list is indented", "[markdown][lists]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("1. Step\n   ```\n   code\n   ```\n\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);
    // The code block border should be indented (have leading spaces)
    size_t box_pos = stripped.find("┌");
    REQUIRE(box_pos != std::string::npos);
    // Check there are spaces before the box character
    REQUIRE(box_pos > 0);
    bool has_indent = true;
    for (size_t i = box_pos - 1; i > 0 && stripped[i] != '\n'; i--) {
        if (stripped[i] != ' ') {
            has_indent = false;
            break;
        }
    }
    REQUIRE(has_indent);
}

TEST_CASE("Ordered list numbering", "[markdown][lists]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("1. First\n2. Second\n3. Third\n\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);
    REQUIRE(stripped.find("1.") != std::string::npos);
    REQUIRE(stripped.find("2.") != std::string::npos);
    REQUIRE(stripped.find("3.") != std::string::npos);
    REQUIRE(stripped.find("First") != std::string::npos);
    REQUIRE(stripped.find("Second") != std::string::npos);
    REQUIRE(stripped.find("Third") != std::string::npos);
}

// ============================================================================
// Incremental rendering
// ============================================================================

TEST_CASE("Line-by-line rendering produces same output as batch", "[markdown][streaming]") {
    // Batch rendering
    OutputCollector batch_output;
    MarkdownRenderer batch_renderer(std::ref(batch_output), true, -1);
    batch_renderer.feed("Hello world.\n\nGoodbye.\n\n");
    batch_renderer.finish();

    // Line-by-line rendering
    OutputCollector line_output;
    MarkdownRenderer line_renderer(std::ref(line_output), true, -1);
    line_renderer.feed("Hello world.\n");
    line_renderer.feed("\n");
    line_renderer.feed("Goodbye.\n");
    line_renderer.feed("\n");
    line_renderer.finish();

    REQUIRE(strip_ansi(batch_output.result) == strip_ansi(line_output.result));
}

// ============================================================================
// Fine-grained incremental rendering tests
// ============================================================================

// Helper to count how many times render callback was called (for buffer-only mode)
struct RenderCounter {
    std::vector<std::string> renders;
    void operator()(const std::string& s) {
        renders.push_back(s);
    }
};

TEST_CASE("Heading renders immediately after newline", "[markdown][incremental]") {
    RenderCounter counter;
    MarkdownRenderer renderer(std::ref(counter), true, -1);

    // Feed heading without trailing content
    renderer.feed("# Title\n");
    // Should have rendered immediately (1 render for the heading)
    REQUIRE(counter.renders.size() == 1);
    REQUIRE(strip_ansi(counter.renders[0]).find("Title") != std::string::npos);

    renderer.finish();
}

TEST_CASE("Thematic break renders immediately after newline", "[markdown][incremental]") {
    RenderCounter counter;
    MarkdownRenderer renderer(std::ref(counter), true, -1);

    renderer.feed("---\n");
    REQUIRE(counter.renders.size() == 1);
    // Should contain the horizontal rule
    REQUIRE(strip_ansi(counter.renders[0]).find("─") != std::string::npos);

    renderer.finish();
}

TEST_CASE("Each list item renders when next item starts", "[markdown][incremental]") {
    RenderCounter counter;
    MarkdownRenderer renderer(std::ref(counter), true, -1);

    // Feed first item - should NOT render yet (waiting for next item)
    renderer.feed("* Item 1\n");
    REQUIRE(counter.renders.size() == 0);

    // Feed second item - first item should render
    renderer.feed("* Item 2\n");
    REQUIRE(counter.renders.size() == 1);
    REQUIRE(strip_ansi(counter.renders[0]).find("Item 1") != std::string::npos);

    // Feed third item - second item should render
    renderer.feed("* Item 3\n");
    REQUIRE(counter.renders.size() == 2);
    REQUIRE(strip_ansi(counter.renders[1]).find("Item 2") != std::string::npos);

    // End list with non-list content
    renderer.feed("Done.\n\n");
    REQUIRE(counter.renders.size() >= 3);

    renderer.finish();

    // Verify all items present
    std::string all_output;
    for (const auto& s : counter.renders) {
        all_output += s;
    }
    std::string stripped = strip_ansi(all_output);
    REQUIRE(stripped.find("Item 1") != std::string::npos);
    REQUIRE(stripped.find("Item 2") != std::string::npos);
    REQUIRE(stripped.find("Item 3") != std::string::npos);
}

TEST_CASE("Code block renders after closing fence", "[markdown][incremental]") {
    RenderCounter counter;
    MarkdownRenderer renderer(std::ref(counter), true, -1);

    // Feed opening fence - triggers state change, may output empty string
    renderer.feed("```cpp\n");
    size_t after_open = counter.renders.size();
    // Opening fence may trigger a render call (with empty output) to set state

    // Feed code - still waiting for closing fence
    renderer.feed("int x = 5;\n");
    REQUIRE(counter.renders.size() == after_open);

    // Feed closing fence - NOW the code block renders
    renderer.feed("```\n");
    REQUIRE(counter.renders.size() == after_open + 1);

    // Find the render that contains the code
    bool found_code = false;
    for (const auto& r : counter.renders) {
        if (strip_ansi(r).find("int x = 5") != std::string::npos) {
            found_code = true;
            break;
        }
    }
    REQUIRE(found_code);

    renderer.finish();
}

TEST_CASE("Paragraph renders after blank line", "[markdown][incremental]") {
    RenderCounter counter;
    MarkdownRenderer renderer(std::ref(counter), true, -1);

    // Feed paragraph text - should NOT render yet (waiting for blank line)
    renderer.feed("This is a paragraph.\n");
    REQUIRE(counter.renders.size() == 0);

    // Feed more text on same paragraph - still waiting
    renderer.feed("More text.\n");
    REQUIRE(counter.renders.size() == 0);

    // Feed blank line - NOW it renders
    renderer.feed("\n");
    REQUIRE(counter.renders.size() == 1);
    REQUIRE(strip_ansi(counter.renders[0]).find("This is a paragraph") != std::string::npos);

    renderer.finish();
}

TEST_CASE("Paragraph renders when followed by heading", "[markdown][incremental]") {
    RenderCounter counter;
    MarkdownRenderer renderer(std::ref(counter), true, -1);

    // Feed paragraph
    renderer.feed("Some text.\n");
    REQUIRE(counter.renders.size() == 0);

    // Feed heading - paragraph should render, then heading
    renderer.feed("# Heading\n");
    REQUIRE(counter.renders.size() == 2);
    REQUIRE(strip_ansi(counter.renders[0]).find("Some text") != std::string::npos);
    REQUIRE(strip_ansi(counter.renders[1]).find("Heading") != std::string::npos);

    renderer.finish();
}

TEST_CASE("Paragraph renders when followed by list", "[markdown][incremental]") {
    RenderCounter counter;
    MarkdownRenderer renderer(std::ref(counter), true, -1);

    // Feed paragraph
    renderer.feed("Introduction.\n");
    REQUIRE(counter.renders.size() == 0);

    // Feed list item - paragraph should render, list item waits
    renderer.feed("* Item\n");
    REQUIRE(counter.renders.size() == 1);
    REQUIRE(strip_ansi(counter.renders[0]).find("Introduction") != std::string::npos);

    // End list
    renderer.feed("\n");
    renderer.finish();
}

TEST_CASE("Blockquote accumulates then renders", "[markdown][incremental]") {
    RenderCounter counter;
    MarkdownRenderer renderer(std::ref(counter), true, -1);

    // Feed blockquote lines - should accumulate
    renderer.feed("> Line 1\n");
    REQUIRE(counter.renders.size() == 0);

    renderer.feed("> Line 2\n");
    REQUIRE(counter.renders.size() == 0);

    // End blockquote with regular text
    renderer.feed("Regular text.\n\n");
    REQUIRE(counter.renders.size() >= 1);

    renderer.finish();

    std::string all_output;
    for (const auto& s : counter.renders) {
        all_output += s;
    }
    std::string stripped = strip_ansi(all_output);
    REQUIRE(stripped.find("Line 1") != std::string::npos);
    REQUIRE(stripped.find("Line 2") != std::string::npos);
}

TEST_CASE("Table accumulates rows then renders", "[markdown][incremental]") {
    RenderCounter counter;
    MarkdownRenderer renderer(std::ref(counter), true, -1);

    // Feed table header - should accumulate
    renderer.feed("| A | B |\n");
    REQUIRE(counter.renders.size() == 0);

    renderer.feed("|---|---|\n");
    REQUIRE(counter.renders.size() == 0);

    renderer.feed("| 1 | 2 |\n");
    REQUIRE(counter.renders.size() == 0);

    // End table with non-table content
    renderer.feed("Done.\n\n");
    REQUIRE(counter.renders.size() >= 1);

    renderer.finish();

    std::string all_output;
    for (const auto& s : counter.renders) {
        all_output += s;
    }
    std::string stripped = strip_ansi(all_output);
    REQUIRE(stripped.find("A") != std::string::npos);
    REQUIRE(stripped.find("B") != std::string::npos);
}

TEST_CASE("Multiple headings render individually", "[markdown][incremental]") {
    RenderCounter counter;
    MarkdownRenderer renderer(std::ref(counter), true, -1);

    renderer.feed("# First\n");
    REQUIRE(counter.renders.size() == 1);
    REQUIRE(strip_ansi(counter.renders[0]).find("First") != std::string::npos);

    renderer.feed("## Second\n");
    REQUIRE(counter.renders.size() == 2);
    REQUIRE(strip_ansi(counter.renders[1]).find("Second") != std::string::npos);

    renderer.feed("### Third\n");
    REQUIRE(counter.renders.size() == 3);
    REQUIRE(strip_ansi(counter.renders[2]).find("Third") != std::string::npos);

    renderer.finish();
}

TEST_CASE("Heading levels 1-6 all detected", "[markdown][incremental]") {
    for (int level = 1; level <= 6; level++) {
        RenderCounter counter;
        MarkdownRenderer renderer(std::ref(counter), true, -1);

        std::string heading = std::string(level, '#') + " Level " + std::to_string(level) + "\n";
        renderer.feed(heading);
        REQUIRE(counter.renders.size() == 1);

        renderer.finish();
    }
}

TEST_CASE("Seven hashes is not a heading", "[markdown][incremental]") {
    RenderCounter counter;
    MarkdownRenderer renderer(std::ref(counter), true, -1);

    // 7 # is not a valid heading, treated as paragraph
    renderer.feed("####### Not a heading\n");
    // Should not render immediately (paragraph waits for blank line)
    REQUIRE(counter.renders.size() == 0);

    renderer.feed("\n");
    REQUIRE(counter.renders.size() == 1);

    renderer.finish();
}

TEST_CASE("List item with code block renders together", "[markdown][incremental]") {
    RenderCounter counter;
    MarkdownRenderer renderer(std::ref(counter), true, -1);

    // Feed a list item that contains a code block
    renderer.feed("* Item with code:\n");
    REQUIRE(counter.renders.size() == 0);

    renderer.feed("  ```\n");
    REQUIRE(counter.renders.size() == 0);

    renderer.feed("  code here\n");
    REQUIRE(counter.renders.size() == 0);

    renderer.feed("  ```\n");
    REQUIRE(counter.renders.size() == 0);

    // Next list item triggers render of first
    renderer.feed("* Next item\n");
    REQUIRE(counter.renders.size() == 1);

    renderer.feed("\n");
    renderer.finish();

    std::string stripped = strip_ansi(counter.renders[0]);
    REQUIRE(stripped.find("Item with code") != std::string::npos);
    REQUIRE(stripped.find("code here") != std::string::npos);
}

// ============================================================================
// Blank line behavior tests
// ============================================================================

TEST_CASE("No double blank lines between heading and heading", "[markdown][spacing]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("# First\n## Second\n### Third\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    // Should never have two consecutive blank lines
    REQUIRE(stripped.find("\n\n\n") == std::string::npos);

    // All headings should be present
    REQUIRE(stripped.find("First") != std::string::npos);
    REQUIRE(stripped.find("Second") != std::string::npos);
    REQUIRE(stripped.find("Third") != std::string::npos);
}

TEST_CASE("No double blank lines between code block and list", "[markdown][spacing]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("```\ncode\n```\n* Item\n\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    // Should never have two consecutive blank lines
    REQUIRE(stripped.find("\n\n\n") == std::string::npos);

    // Both code and list should be present
    REQUIRE(stripped.find("code") != std::string::npos);
    REQUIRE(stripped.find("Item") != std::string::npos);
}

TEST_CASE("Table has blank line before and after", "[markdown][spacing]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("Before.\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\nAfter.\n\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    // Find positions
    size_t before_pos = stripped.find("Before.");
    size_t table_start = stripped.find("┌");  // Box drawing for table
    size_t table_end = stripped.rfind("┘");
    size_t after_pos = stripped.find("After.");

    REQUIRE(before_pos != std::string::npos);
    REQUIRE(table_start != std::string::npos);
    REQUIRE(table_end != std::string::npos);
    REQUIRE(after_pos != std::string::npos);

    // Check blank line before table
    std::string between_before = stripped.substr(before_pos, table_start - before_pos);
    REQUIRE(between_before.find("\n\n") != std::string::npos);

    // Check blank line after table
    std::string between_after = stripped.substr(table_end, after_pos - table_end);
    REQUIRE(between_after.find("\n\n") != std::string::npos);
}

TEST_CASE("Blockquote has blank line before and after", "[markdown][spacing]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("Before.\n\n> Quote line\n\nAfter.\n\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    size_t before_pos = stripped.find("Before.");
    size_t quote_pos = stripped.find("Quote");
    size_t after_pos = stripped.find("After.");

    REQUIRE(before_pos != std::string::npos);
    REQUIRE(quote_pos != std::string::npos);
    REQUIRE(after_pos != std::string::npos);

    // Check blank line before blockquote
    std::string between_before = stripped.substr(before_pos, quote_pos - before_pos);
    REQUIRE(between_before.find("\n\n") != std::string::npos);

    // Check blank line after blockquote
    std::string between_after = stripped.substr(quote_pos, after_pos - quote_pos);
    REQUIRE(between_after.find("\n\n") != std::string::npos);
}

TEST_CASE("Code block has blank line before and after", "[markdown][spacing]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("Before.\n\n```\ncode\n```\n\nAfter.\n\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    size_t before_pos = stripped.find("Before.");
    size_t code_pos = stripped.find("code");
    size_t after_pos = stripped.find("After.");

    REQUIRE(before_pos != std::string::npos);
    REQUIRE(code_pos != std::string::npos);
    REQUIRE(after_pos != std::string::npos);

    // Check blank line before code block
    std::string between_before = stripped.substr(before_pos, code_pos - before_pos);
    REQUIRE(between_before.find("\n\n") != std::string::npos);

    // Check blank line after code block
    std::string between_after = stripped.substr(code_pos, after_pos - code_pos);
    REQUIRE(between_after.find("\n\n") != std::string::npos);
}

TEST_CASE("Heading has blank line before and after", "[markdown][spacing]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    renderer.feed("Before text.\n\n# Title\nAfter text.\n\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    size_t before_pos = stripped.find("Before");
    size_t title_pos = stripped.find("Title");
    size_t after_pos = stripped.find("After");

    REQUIRE(before_pos != std::string::npos);
    REQUIRE(title_pos != std::string::npos);
    REQUIRE(after_pos != std::string::npos);

    // Check blank line before heading
    std::string between_before = stripped.substr(before_pos, title_pos - before_pos);
    REQUIRE(between_before.find("\n\n") != std::string::npos);

    // Check blank line after heading
    std::string between_after = stripped.substr(title_pos, after_pos - title_pos);
    REQUIRE(between_after.find("\n\n") != std::string::npos);
}

TEST_CASE("Consecutive blocks merge blank lines", "[markdown][spacing]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    // Heading (needs blank after) followed by list (needs blank before)
    // Should result in only ONE blank line between them
    renderer.feed("# Title\n* Item\n\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    // Should never have three consecutive newlines (which would be two blank lines)
    REQUIRE(stripped.find("\n\n\n") == std::string::npos);

    // Both should be present with exactly one blank line between
    size_t title_pos = stripped.find("Title");
    size_t item_pos = stripped.find("Item");
    REQUIRE(title_pos != std::string::npos);
    REQUIRE(item_pos != std::string::npos);
}

TEST_CASE("Streaming list items share one blank line before", "[markdown][spacing]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    // Feed items incrementally
    renderer.feed("Intro.\n\n");
    renderer.feed("* Item 1\n");
    renderer.feed("* Item 2\n");
    renderer.feed("* Item 3\n");
    renderer.feed("\n");
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    // Count blank lines before the first bullet
    size_t intro_end = stripped.find("Intro.") + 6;
    size_t first_bullet = stripped.find("●");
    std::string gap = stripped.substr(intro_end, first_bullet - intro_end);

    // Should have exactly one blank line (two newlines)
    size_t newline_count = 0;
    for (char c : gap) {
        if (c == '\n') newline_count++;
    }
    // One after "Intro." and one blank = 2 newlines before items
    REQUIRE(newline_count == 2);
}

// =============================================================================
// Table Width Constraint Tests
// =============================================================================

TEST_CASE("Table respects specified terminal width", "[markdown][table][width]") {
    OutputCollector output;
    // Specify a narrow terminal width
    MarkdownRenderer renderer(std::ref(output), false, 60);

    std::string table = R"(| Column A | Column B | Column C |
|----------|----------|----------|
| Short    | Medium   | Longer   |
)";

    renderer.feed(table);
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    // Each line of the formatted table should be at most 60 display characters
    // (Only check lines with box-drawing chars - those are the rendered table)
    std::istringstream stream(stripped);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find("│") != std::string::npos ||
            line.find("┌") != std::string::npos ||
            line.find("└") != std::string::npos) {
            REQUIRE(display_width(line) <= 60);
        }
    }
}

TEST_CASE("Table columns scale down for narrow terminals", "[markdown][table][width]") {
    OutputCollector output;
    // Very narrow terminal
    MarkdownRenderer renderer(std::ref(output), false, 40);

    std::string table = R"(| Very Long Column Header One | Very Long Column Header Two |
|-----------------------------|------------------------------|
| Some long content here      | More long content over here  |
)";

    renderer.feed(table);
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    // Each line of the formatted table should respect the width constraint
    // (Only check lines with box-drawing chars - those are the rendered table)
    std::istringstream stream(stripped);
    std::string line;
    while (std::getline(stream, line)) {
        // Check if this is a table line (contains box-drawing characters)
        if (line.find("│") != std::string::npos ||
            line.find("┌") != std::string::npos ||
            line.find("└") != std::string::npos) {
            REQUIRE(display_width(line) <= 40);
        }
    }
}

TEST_CASE("Table with many columns may overflow when min width 8 is too wide", "[markdown][table][width]") {
    OutputCollector output;
    // Narrow terminal with many columns - overflow acceptable
    MarkdownRenderer renderer(std::ref(output), false, 60);

    // 10 columns at min width 8 = 80 chars of content, plus borders
    // This will overflow 60 chars, which is acceptable
    std::string table = R"(| A | B | C | D | E | F | G | H | I | J |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 0 |
)";

    renderer.feed(table);
    renderer.finish();

    // The table should still render (not crash or produce empty output)
    REQUIRE(!output.result.empty());
    // Should contain table border characters
    REQUIRE(output.result.find("│") != std::string::npos);
}

TEST_CASE("Table with few columns fits within terminal width", "[markdown][table][width]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), false, 80);

    std::string table = R"(| Name | Value |
|------|-------|
| foo  | 123   |
)";

    renderer.feed(table);
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    // Each line of the formatted table should fit within 80 chars
    // (Only check lines with box-drawing chars - those are the rendered table)
    std::istringstream stream(stripped);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find("│") != std::string::npos ||
            line.find("┌") != std::string::npos ||
            line.find("└") != std::string::npos) {
            REQUIRE(display_width(line) <= 80);
        }
    }
}

TEST_CASE("Table content is wrapped when columns are scaled", "[markdown][table][width]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), false, 50);

    // Content that needs wrapping when columns are scaled down
    std::string table = R"(| Description | Details |
|-------------|---------|
| This is a fairly long description that will need to wrap | And this has more details |
)";

    renderer.feed(table);
    renderer.finish();

    std::string stripped = strip_ansi(output.result);

    // Table should render and fit within width
    // (Only check lines with box-drawing chars - those are the rendered table)
    std::istringstream stream(stripped);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find("│") != std::string::npos ||
            line.find("┌") != std::string::npos ||
            line.find("└") != std::string::npos) {
            REQUIRE(display_width(line) <= 50);
        }
    }
}

// =============================================================================
// Table Vertical Alignment Tests
// =============================================================================

// Helper to check that all table lines have the same display width
void check_table_alignment(const std::string& output) {
    std::string stripped = strip_ansi(output);
    std::istringstream stream(stripped);
    std::string line;
    size_t expected_width = 0;
    int line_num = 0;

    while (std::getline(stream, line)) {
        // Only check lines that are part of the rendered table (have box chars)
        if (line.find("│") != std::string::npos ||
            line.find("┌") != std::string::npos ||
            line.find("└") != std::string::npos ||
            line.find("├") != std::string::npos) {

            size_t width = display_width(line);
            if (expected_width == 0) {
                expected_width = width;
            } else {
                INFO("Line " << line_num << ": \"" << line << "\"");
                INFO("Expected width: " << expected_width << ", actual: " << width);
                REQUIRE(width == expected_width);
            }
        }
        line_num++;
    }
}

TEST_CASE("Table with en-dash aligns correctly", "[markdown][table][alignment]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), false, -1);

    // En-dash (–) is 3 bytes but 1 display column
    std::string table = R"(| Column A | Column B |
|----------|----------|
| Normal   | Text     |
| With–dash | Here    |
)";

    renderer.feed(table);
    renderer.finish();

    check_table_alignment(output.result);
}

TEST_CASE("Table with em-dash aligns correctly", "[markdown][table][alignment]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), false, -1);

    // Em-dash (—) is 3 bytes but 1 display column
    std::string table = R"(| Column A | Column B |
|----------|----------|
| Normal   | Text     |
| With—dash | Here    |
)";

    renderer.feed(table);
    renderer.finish();

    check_table_alignment(output.result);
}

TEST_CASE("Table with curly quotes aligns correctly", "[markdown][table][alignment]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), false, -1);

    // Curly quotes are 3 bytes but 1 display column each
    std::string table = R"(| Column A | Column B |
|----------|----------|
| "quoted" | Text     |
| Normal   | Here     |
)";

    renderer.feed(table);
    renderer.finish();

    check_table_alignment(output.result);
}

TEST_CASE("Table with mixed UTF-8 characters aligns correctly", "[markdown][table][alignment]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), false, -1);

    // Mix of multi-byte UTF-8: en-dash, em-dash, curly quotes, ellipsis
    std::string table = R"(| Asset | Location | Stage |
|-------|----------|-------|
| Project–One | México | "active" |
| Normal | USA | done… |
| Another—test | "quoted" | – |
)";

    renderer.feed(table);
    renderer.finish();

    check_table_alignment(output.result);
}

TEST_CASE("Table with only ASCII aligns correctly", "[markdown][table][alignment]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), false, -1);

    std::string table = R"(| Name | Value | Status |
|------|-------|--------|
| foo  | 123   | ok     |
| bar  | 456   | done   |
)";

    renderer.feed(table);
    renderer.finish();

    check_table_alignment(output.result);
}

TEST_CASE("Table with wrapped cells containing UTF-8 aligns correctly", "[markdown][table][alignment]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), false, 60);

    // Long content with UTF-8 that will wrap
    std::string table = R"(| Header | Details |
|--------|---------|
| Short | This is a longer description with an en–dash and "quotes" that should wrap |
)";

    renderer.feed(table);
    renderer.finish();

    check_table_alignment(output.result);
}

// =============================================================================
// Table Inline Formatting Tests
// =============================================================================

TEST_CASE("Table renders bold text in cells", "[markdown][table][inline]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    std::string table = R"(| Column A | Column B |
|----------|----------|
| **bold** | normal   |
)";

    renderer.feed(table);
    renderer.finish();

    std::string result = output.result;
    // Bold text should have ANSI bold code and not show **
    REQUIRE(result.find("**bold**") == std::string::npos);
    REQUIRE(result.find("\033[1m") != std::string::npos);  // Bold ANSI code
    REQUIRE(result.find("bold") != std::string::npos);

    check_table_alignment(result);
}

TEST_CASE("Table renders italic text in cells", "[markdown][table][inline]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    std::string table = R"(| Column A | Column B |
|----------|----------|
| *italic* | normal   |
)";

    renderer.feed(table);
    renderer.finish();

    std::string result = output.result;
    // Italic text should have ANSI italic code and not show *
    REQUIRE(result.find("*italic*") == std::string::npos);
    REQUIRE(result.find("\033[3m") != std::string::npos);  // Italic ANSI code
    REQUIRE(result.find("italic") != std::string::npos);

    check_table_alignment(result);
}

TEST_CASE("Table renders inline code in cells", "[markdown][table][inline]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    std::string table = R"(| Column A | Column B |
|----------|----------|
| `code`   | normal   |
)";

    renderer.feed(table);
    renderer.finish();

    std::string result = output.result;
    // Code should have ANSI cyan color and contain the word "code"
    REQUIRE(result.find("\033[36m") != std::string::npos);  // Cyan ANSI code
    REQUIRE(result.find("code") != std::string::npos);

    check_table_alignment(result);
}

TEST_CASE("Table renders mixed inline formatting", "[markdown][table][inline]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    std::string table = R"(| Name | Description |
|------|-------------|
| **Project** | A *really* good `thing` |
)";

    renderer.feed(table);
    renderer.finish();

    std::string result = output.result;
    // Should not have raw bold/italic markdown syntax (** and *)
    REQUIRE(result.find("**Project**") == std::string::npos);
    REQUIRE(result.find("*really*") == std::string::npos);
    // But should have the text with ANSI formatting
    REQUIRE(result.find("Project") != std::string::npos);
    REQUIRE(result.find("really") != std::string::npos);
    REQUIRE(result.find("thing") != std::string::npos);
    // Should have bold, italic, and cyan (code) ANSI codes
    REQUIRE(result.find("\033[1m") != std::string::npos);  // Bold
    REQUIRE(result.find("\033[3m") != std::string::npos);  // Italic
    REQUIRE(result.find("\033[36m") != std::string::npos); // Cyan (code)

    check_table_alignment(result);
}

TEST_CASE("Table with inline formatting maintains alignment", "[markdown][table][inline]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, -1);

    std::string table = R"(| Asset | Location | Status |
|-------|----------|--------|
| **Cerro del Gallo** | México | *active* |
| Normal text | USA | `done` |
| **Another–bold** | "quoted" | – |
)";

    renderer.feed(table);
    renderer.finish();

    check_table_alignment(output.result);
}

TEST_CASE("Table inline formatting continues across wrapped lines", "[markdown][table][inline][wrap]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, 50);  // Narrow to force wrapping

    std::string table = R"(| Asset | Description |
|-------|-------------|
| **El Castillo Mine** | **Former producing mine** now in reclamation |
)";

    renderer.feed(table);
    renderer.finish();

    std::string result = output.result;

    // The bold formatting should appear on both lines of "El Castillo" and "Mine"
    // Count occurrences of bold start code - should have at least 4:
    // "Asset" header, "Description" header, "El Castillo" (line 1), "Mine" (line 2)
    size_t bold_count = 0;
    size_t pos = 0;
    while ((pos = result.find("\033[1m", pos)) != std::string::npos) {
        bold_count++;
        pos++;
    }
    // Header row has 2 bold items, "El Castillo Mine" wraps to 2 lines (needs 2 bold starts),
    // "Former producing mine" is on one line (1 bold start)
    // So we expect at least 5 bold start codes
    INFO("Bold count: " << bold_count);
    REQUIRE(bold_count >= 5);

    check_table_alignment(result);
}

TEST_CASE("Table italic formatting continues across wrapped lines", "[markdown][table][inline][wrap]") {
    OutputCollector output;
    MarkdownRenderer renderer(std::ref(output), true, 40);  // Narrow to force wrapping

    std::string table = R"(| Status |
|--------|
| *This is a very long italic text that will wrap* |
)";

    renderer.feed(table);
    renderer.finish();

    std::string result = output.result;

    // Count italic codes - should appear on each wrapped line
    size_t italic_count = 0;
    size_t pos = 0;
    while ((pos = result.find("\033[3m", pos)) != std::string::npos) {
        italic_count++;
        pos++;
    }
    // The italic text should wrap to multiple lines, each needing italic start
    INFO("Italic count: " << italic_count);
    REQUIRE(italic_count >= 2);  // At least 2 lines of wrapped italic text

    check_table_alignment(result);
}
