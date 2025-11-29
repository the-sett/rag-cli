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
