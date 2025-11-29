#pragma once

#include <string>
#include <functional>

namespace rag {

/**
 * Markdown renderer that buffers streaming text and renders complete
 * CommonMark blocks with ANSI terminal formatting.
 *
 * Usage:
 *   MarkdownRenderer renderer([](const std::string& s) { std::cout << s; });
 *   renderer.feed(delta1);
 *   renderer.feed(delta2);
 *   // ... more streaming chunks ...
 *   renderer.finish();  // Flush remaining content
 */
class MarkdownRenderer {
public:
    using OutputCallback = std::function<void(const std::string&)>;

    /**
     * Create a markdown renderer.
     * @param output Callback invoked with formatted text chunks
     * @param colors_enabled Whether to emit ANSI color codes
     */
    explicit MarkdownRenderer(OutputCallback output, bool colors_enabled = true);

    /**
     * Feed a streaming text chunk.
     * Complete blocks will be rendered and output immediately.
     */
    void feed(const std::string& delta);

    /**
     * Finish rendering - flush any remaining buffered content.
     * Call this when the stream is complete.
     */
    void finish();

    /**
     * Check if a line is a CommonMark thematic break (---, ***, ___).
     */
    static bool is_thematic_break(const std::string& line);

private:
    OutputCallback output_;
    bool colors_enabled_;
    std::string buffer_;

    // Code block tracking
    bool in_code_block_ = false;
    std::string code_fence_chars_;  // ``` or ~~~
    size_t code_fence_length_ = 0;

    // Block detection and extraction
    bool has_complete_block() const;
    std::string extract_complete_block();
    bool is_code_fence(const std::string& line, size_t& fence_length, std::string& fence_chars) const;
    bool is_closing_fence(const std::string& line) const;

    // Rendering
    std::string render_markdown(const std::string& markdown);

    // ANSI formatting helpers
    std::string ansi(const char* code) const;
    std::string bold(const std::string& text) const;
    std::string italic(const std::string& text) const;
    std::string code(const std::string& text) const;
    std::string code_block(const std::string& text, const std::string& lang) const;
    std::string heading(const std::string& text, int level) const;
    std::string link(const std::string& text, const std::string& url) const;
    std::string blockquote_line(const std::string& text) const;
    std::string list_item(const std::string& text, bool ordered, int number, int indent) const;
    std::string horizontal_rule() const;
};

} // namespace rag
