#include "config.hpp"
#include "console.hpp"
#include "settings.hpp"
#include "file_resolver.hpp"
#include "openai_client.hpp"
#include "vector_store.hpp"
#include "chat.hpp"
#include "markdown_renderer.hpp"

#include <CLI/CLI.hpp>
#include <iostream>
#include <cstdlib>
#include <csignal>

using namespace rag;

// Global console for signal handler
static Console* g_console = nullptr;

void signal_handler(int) {
    if (g_console) {
        g_console->println();
        g_console->print_warning("Interrupted.");
    }
    std::exit(0);
}

// Select model interactively
std::string select_model(OpenAIClient& client, Console& console) {
    console.println();
    console.print_warning("Fetching available models...");

    std::vector<std::string> models;
    try {
        models = client.list_models();
    } catch (const std::exception& e) {
        console.print_error("Failed to fetch models: " + std::string(e.what()));
        std::exit(1);
    }

    if (models.empty()) {
        console.print_error("No chat models found!");
        std::exit(1);
    }

    console.println();
    console.print_header("Available models:");
    for (size_t i = 0; i < models.size(); ++i) {
        console.println("  " + std::to_string(i + 1) + ". " + models[i]);
    }

    console.println();
    while (true) {
        std::string choice = console.prompt("Select model number", "1");
        try {
            size_t idx = std::stoul(choice) - 1;
            if (idx < models.size()) {
                std::string selected = models[idx];
                console.print_success("Selected: " + selected);
                return selected;
            }
        } catch (...) {
            // Invalid input
        }
        console.print_error("Invalid choice, try again");
    }
}

// Select reasoning effort interactively
std::string select_reasoning_effort(Console& console) {
    console.println();
    console.print_header("Reasoning effort levels:");
    console.println("  1. low      - Minimal thinking - faster, cheaper");
    console.println("  2. medium   - Balanced thinking");
    console.println("  3. high     - Maximum thinking - slower, more thorough");

    console.println();
    while (true) {
        std::string choice = console.prompt("Select reasoning effort", "2");
        if (choice == "1") {
            console.print_success("Selected: low");
            return "low";
        } else if (choice == "2") {
            console.print_success("Selected: medium");
            return "medium";
        } else if (choice == "3") {
            console.print_success("Selected: high");
            return "high";
        }
        console.print_error("Invalid choice, try again");
    }
}

// Load or create settings
Settings load_or_create_settings(
    const std::vector<std::string>& files,
    bool reindex,
    bool non_interactive,
    OpenAIClient& client,
    Console& console
) {
    auto existing = load_settings();
    bool has_valid_settings = existing.has_value() && existing->is_valid();

    // If reindex is requested with existing valid settings, do incremental update
    if (reindex && has_valid_settings) {
        Settings settings = *existing;

        // Use new file patterns if provided, otherwise use stored patterns
        std::vector<std::string> patterns_to_use = files.empty() ? settings.file_patterns : files;

        if (patterns_to_use.empty()) {
            console.print_error("Error: No file patterns available for reindex");
            console.println("Provide file patterns or ensure .crag.json has stored patterns.");
            std::exit(1);
        }

        // Update patterns if new ones were provided
        if (!files.empty()) {
            settings.file_patterns = files;
        }

        // Resolve current files from patterns
        std::vector<std::string> current_files = resolve_file_patterns(patterns_to_use, console);

        if (current_files.empty()) {
            console.print_error("Error: No supported files found");
            std::exit(1);
        }

        // Compute diff
        FileDiff diff = compute_file_diff(current_files, settings.indexed_files);

        // Apply incremental updates
        update_vector_store(settings.vector_store_id, diff, client, console, settings.indexed_files);

        // Save updated settings
        save_settings(settings);
        return settings;
    }

    // No reindex requested and we have valid settings - just use them
    if (!reindex && has_valid_settings) {
        if (!non_interactive) {
            console.print_colored("Using model: ", ansi::GREEN);
            console.println(existing->model);
            if (!existing->reasoning_effort.empty()) {
                console.print_colored("Reasoning effort: ", ansi::GREEN);
                console.println(existing->reasoning_effort);
            }
            console.print_colored("Using vector store: ", ansi::GREEN);
            console.println(existing->vector_store_id);
            if (!existing->file_patterns.empty()) {
                console.print_colored("Indexed patterns: ", ansi::GREEN);
                std::string patterns;
                for (size_t i = 0; i < existing->file_patterns.size(); ++i) {
                    if (i > 0) patterns += ", ";
                    patterns += existing->file_patterns[i];
                }
                console.println(patterns);
            }
            console.print_colored("Indexed files: ", ansi::GREEN);
            console.println(std::to_string(existing->indexed_files.size()));
        }
        return *existing;
    }

    // First time setup - need file patterns
    if (files.empty()) {
        console.print_error("Error: No files specified for indexing");
        console.println();
        console.println("Usage: crag 'docs/*.md' 'src/**/*.py'");
        console.println("       crag --reindex 'knowledge/'");
        console.println();
        console.println("Examples:");
        console.println("  crag '*.md'                    # All markdown files in current dir");
        console.println("  crag 'docs/**/*.txt'           # All txt files in docs/ recursively");
        console.println("  crag README.md guide.md        # Specific files");
        console.println("  crag knowledge/                # All supported files in a directory");
        std::exit(1);
    }

    // First time - select model, reasoning, and create vector store
    Settings settings;
    settings.model = select_model(client, console);
    settings.reasoning_effort = select_reasoning_effort(console);
    settings.file_patterns = files;
    settings.vector_store_id = create_vector_store(files, client, console, settings.indexed_files);

    if (settings.vector_store_id.empty()) {
        std::exit(1);
    }

    save_settings(settings);
    return settings;
}

int main(int argc, char* argv[]) {
    CLI::App app{"A RAG CLI tool using OpenAI's vector store and file search"};
    app.footer("\nExamples:\n"
               "  crag 'docs/*.md'              Index markdown files and start chat\n"
               "  crag 'src/**/*.py' '*.md'     Index multiple patterns\n"
               "  crag --reindex 'knowledge/'   Re-index a directory\n"
               "  crag                          Use existing index\n");

    std::vector<std::string> files;
    app.add_option("files", files, "Files or glob patterns to index (e.g., '*.md', 'docs/**/*.txt')");

    bool reindex = false;
    app.add_flag("--reindex", reindex, "Force re-upload + reindex files");

    bool strict = false;
    app.add_flag("--strict", strict, "Only answer if info is in files");


    char thinking = '\0';
    app.add_option("-t,--thinking", thinking, "Override thinking level: l=low, m=medium, h=high")
        ->check(CLI::IsMember({'l', 'm', 'h'}));

    bool non_interactive = false;
    app.add_flag("-n,--non-interactive", non_interactive,
                 "Non-interactive mode: read query from stdin, write response to stdout, exit");

    bool plain_output = false;
    app.add_flag("--plain", plain_output,
                 "Disable markdown rendering, output raw text");

    CLI11_PARSE(app, argc, argv);

    Console console;
    g_console = &console;

    // Set up signal handler for graceful Ctrl+C
    std::signal(SIGINT, signal_handler);

    // Check for API key
    const char* api_key = std::getenv("OPEN_AI_API_KEY");
    if (!api_key || std::string(api_key).empty()) {
        console.print_error("Error: OPEN_AI_API_KEY environment variable not set");
        return 1;
    }

    OpenAIClient client(api_key);

    // Load or create settings
    Settings settings = load_or_create_settings(files, reindex, non_interactive, client, console);

    // Determine reasoning effort
    std::string reasoning_effort = settings.reasoning_effort;
    if (thinking != '\0') {
        auto it = THINKING_MAP.find(thinking);
        if (it != THINKING_MAP.end()) {
            reasoning_effort = it->second;
            if (!non_interactive) {
                console.print_colored("Thinking level override: ", ansi::YELLOW);
                console.println(reasoning_effort);
            }
        }
    }

    // Build system prompt
    std::string system_prompt =
        "You are a specialized assistant. "
        "Use ONLY the provided file knowledge when relevant. ";

    if (strict) {
        system_prompt +=
            "If the answer is not explicitly contained in the files, "
            "respond with: 'The provided documents do not contain that information.'";
    } else {
        system_prompt +=
            "If the files do not contain the answer, you may reason normally but clearly "
            "state that you are extrapolating.";
    }

    // Create chat session
    ChatSession chat(system_prompt, LOG_DIR);

    // Determine if we should render markdown
    // Markdown rendering: enabled for interactive mode without --plain
    bool render_markdown = !non_interactive && !plain_output;

    // Process a single query
    auto process_query = [&](const std::string& user_input) {
        chat.add_user_message(user_input);

        std::string streamed_text;

        try {
            if (render_markdown) {
                // Use markdown renderer for interactive mode
                MarkdownRenderer renderer([&](const std::string& formatted) {
                    console.print_raw(formatted);
                    console.flush();
                });

                client.stream_response(
                    settings.model,
                    chat.get_conversation(),
                    settings.vector_store_id,
                    reasoning_effort,
                    [&](const std::string& delta) {
                        renderer.feed(delta);
                        streamed_text += delta;
                    }
                );

                renderer.finish();
            } else {
                // Raw output for non-interactive or --plain mode
                client.stream_response(
                    settings.model,
                    chat.get_conversation(),
                    settings.vector_store_id,
                    reasoning_effort,
                    [&](const std::string& delta) {
                        if (non_interactive) {
                            std::cout << delta << std::flush;
                        } else {
                            console.print_raw(delta);
                            console.flush();
                        }
                        streamed_text += delta;
                    }
                );
            }
        } catch (const std::exception& e) {
            console.println();
            console.print_error("Error: " + std::string(e.what()));
        }

        chat.add_assistant_message(streamed_text);
    };

    // Non-interactive mode
    if (non_interactive) {
        std::string user_input;
        std::getline(std::cin, user_input);

        // Read all remaining input if any
        std::string line;
        while (std::getline(std::cin, line)) {
            user_input += "\n" + line;
        }

        // Trim whitespace
        size_t start = user_input.find_first_not_of(" \t\n\r");
        size_t end = user_input.find_last_not_of(" \t\n\r");
        if (start == std::string::npos) {
            return 0;
        }
        user_input = user_input.substr(start, end - start + 1);

        if (user_input.empty()) {
            return 0;
        }

        process_query(user_input);
        std::cout << std::endl;
        return 0;
    }

    // Interactive chat loop
    console.println();
    console.print_header("=== RAG CLI Ready ===");
    console.println("Type 'quit' to exit.");
    console.println();

    while (true) {
        console.print("You: ");
        console.flush();

        std::string user_input;
        if (!std::getline(std::cin, user_input)) {
            break;
        }

        // Trim whitespace
        size_t start = user_input.find_first_not_of(" \t\n\r");
        size_t end = user_input.find_last_not_of(" \t\n\r");
        if (start != std::string::npos) {
            user_input = user_input.substr(start, end - start + 1);
        } else {
            user_input.clear();
        }

        if (user_input.empty()) {
            continue;
        }

        if (user_input == "quit" || user_input == "exit") {
            console.println("Goodbye.");
            break;
        }

        console.println();
        console.print_colored("Assistant:", ansi::BOLD);
        console.print_colored("", ansi::GREEN);
        console.println();

        process_query(user_input);

        console.println();
        console.println();
    }

    return 0;
}
