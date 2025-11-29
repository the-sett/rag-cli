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
            if (c == 'm' || c == '\\') {
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
