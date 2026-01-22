#include "config.hpp"
#include "console.hpp"
#include "settings.hpp"
#include "file_resolver.hpp"
#include "openai_client.hpp"
#include "vector_store.hpp"
#include "chat.hpp"
#include "markdown_renderer.hpp"
#include "input_editor.hpp"
#include "terminal.hpp"
#include "http_server.hpp"
#include "websocket_server.hpp"
#include "mcp_server.hpp"
#include "file_watcher.hpp"
#include "verbose.hpp"

#include <CLI/CLI.hpp>
#include <iostream>
#include <cstdlib>
#include <csignal>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <unistd.h>
#include <termios.h>

using namespace rag;

// ========== Signal Handling ==========

static Console* g_console = nullptr;           // Global console for signal handler.
static std::atomic<bool>* g_stop_spinner = nullptr;  // Global flag to stop spinner thread.

// Handles SIGINT (Ctrl+C) for graceful shutdown.
void signal_handler(int) {
    // Restore terminal settings FIRST (before any output).
    terminal::restore_original_settings();

    // Stop the spinner thread.
    if (g_stop_spinner) {
        g_stop_spinner->store(true);
    }

    if (g_console) {
        // Clear any spinner remnants and reset terminal.
        g_console->print_raw("\r\033[K");  // Clear line.
        g_console->print_raw("\033[?25h"); // Show cursor (in case hidden).
        g_console->println();
        g_console->print_warning("Interrupted.");
    }
    std::exit(0);
}

// ========== Interactive Selection ==========

// Prompts the user to select a model from available GPT-5 models.
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
            // Invalid input, continue prompting.
        }
        console.print_error("Invalid choice, try again");
    }
}

// Prompts the user to select a reasoning effort level.
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

// ========== System Prompt ==========

std::string build_system_prompt() {
    return
        "You are a specialized assistant. "
        "Use ONLY the provided file knowledge when relevant. "
        "If the files do not contain the answer, you may reason normally but clearly "
        "state that you are extrapolating. "
        "You have access to tools that control the user interface: "
        "open_sidebar shows the navigation sidebar on the left, and "
        "close_sidebar hides it to give more space for the chat. "
        "Use these tools when the user asks to show or hide the sidebar, "
        "or when you think it would improve their experience.";
}

// ========== Settings Management ==========

// Loads existing settings or creates new ones through interactive setup.
Settings load_or_create_settings(
    const std::vector<std::string>& files,
    bool reindex,
    bool rebuild,
    bool non_interactive,
    OpenAIClient& client,
    Console& console
) {
    auto existing = load_settings();
    bool has_valid_settings = existing.has_value() && existing->is_valid();

    // If rebuild is requested with existing valid settings, delete everything and recreate.
    if (rebuild && has_valid_settings) {
        Settings settings = *existing;

        // Use new file patterns if provided, otherwise use stored patterns.
        std::vector<std::string> patterns_to_use = files.empty() ? settings.file_patterns : files;

        if (patterns_to_use.empty()) {
            console.print_error("Error: No file patterns available for rebuild");
            console.println("Provide file patterns or ensure .crag.json has stored patterns.");
            std::exit(1);
        }

        // Update patterns if new ones were provided.
        if (!files.empty()) {
            settings.file_patterns = files;
        }

        // Rebuild the vector store from scratch
        std::string new_vector_store_id = rebuild_vector_store(
            settings.vector_store_id,
            patterns_to_use,
            client,
            console,
            settings.indexed_files
        );

        if (new_vector_store_id.empty()) {
            std::exit(1);
        }

        settings.vector_store_id = new_vector_store_id;

        // Save updated settings.
        save_settings(settings);
        return settings;
    }

    // If reindex is requested with existing valid settings, do incremental update.
    if (reindex && has_valid_settings) {
        Settings settings = *existing;

        // Use new file patterns if provided, otherwise use stored patterns.
        std::vector<std::string> patterns_to_use = files.empty() ? settings.file_patterns : files;

        if (patterns_to_use.empty()) {
            console.print_error("Error: No file patterns available for reindex");
            console.println("Provide file patterns or ensure .crag.json has stored patterns.");
            std::exit(1);
        }

        // Update patterns if new ones were provided.
        if (!files.empty()) {
            settings.file_patterns = files;
        }

        // Resolve current files from patterns.
        std::vector<std::string> current_files = resolve_file_patterns(patterns_to_use, console);

        if (current_files.empty()) {
            console.print_error("Error: No supported files found");
            std::exit(1);
        }

        // Compute diff.
        FileDiff diff = compute_file_diff(current_files, settings.indexed_files);

        // Apply incremental updates.
        update_vector_store(settings.vector_store_id, diff, client, console, settings.indexed_files);

        // Save updated settings.
        save_settings(settings);
        return settings;
    }

    // No reindex requested and we have valid settings - just use them.
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

    // First time setup - need file patterns.
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

    // First time - select model, reasoning, and create vector store.
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

// ========== Main Entry Point ==========

int main(int argc, char* argv[]) {
    CLI::App app{"A RAG CLI tool using OpenAI's vector store and file search"};
    app.footer("\nExamples:\n"
               "  crag 'docs/*.md'              Index markdown files and start chat\n"
               "  crag 'src/**/*.py' '*.md'     Index multiple patterns\n"
               "  crag --reindex 'knowledge/'   Re-index a directory\n"
               "  crag --rebuild                Delete and rebuild vector store from scratch\n"
               "  crag                          Use existing index\n");

    std::vector<std::string> files;
    app.add_option("files", files, "Files or glob patterns to index (e.g., '*.md', 'docs/**/*.txt')");

    bool reindex = false;
    app.add_flag("--reindex", reindex, "Force re-upload + reindex files");

    bool rebuild = false;
    app.add_flag("--rebuild", rebuild, "Delete entire vector store and rebuild from scratch");



    char thinking = '\0';
    app.add_option("-t,--thinking", thinking, "Override thinking level: l=low, m=medium, h=high")
        ->check(CLI::IsMember({'l', 'm', 'h'}));

    bool non_interactive = false;
    app.add_flag("-n,--non-interactive", non_interactive,
                 "Non-interactive mode: read query from stdin, write response to stdout, exit");

    bool plain_output = false;
    app.add_flag("--plain", plain_output,
                 "Disable markdown rendering, output raw text");

    bool server_mode = false;
    app.add_flag("-s,--server", server_mode,
                 "Run in server mode with web interface");

    bool mcp_mode = false;
    app.add_flag("--mcp", mcp_mode,
                 "Run as MCP server (for Claude Code integration)");

    int server_port = 8192;
    app.add_option("-p,--port", server_port,
                   "Port for web server (default: 8192)")
        ->check(CLI::Range(1, 65535));

    std::string server_address = "0.0.0.0";
    app.add_option("--address", server_address,
                   "Bind address for web server (default: 0.0.0.0)");

    std::string www_dir;
    app.add_option("--www-dir", www_dir,
                   "Serve web files from directory instead of embedded resources (for development)");

    bool verbose = false;
    app.add_flag("-v,--verbose", verbose,
                 "Enable verbose output showing API calls, WebSocket messages, and HTTP requests");

    CLI11_PARSE(app, argc, argv);

    // Enable verbose mode globally
    set_verbose(verbose);

    // Save terminal settings before any raw mode changes.
    terminal::save_original_settings();

    Console console;
    g_console = &console;

    // Set up signal handler for graceful Ctrl+C.
    std::signal(SIGINT, signal_handler);

    // Server mode - start HTTP server and WebSocket server for web UI.
    if (server_mode) {
        console.println();
        console.print_header("=== CRAG Web Server ===");

        // Check for API key.
        const char* api_key_env = std::getenv("OPEN_AI_API_KEY");
        if (!api_key_env || std::string(api_key_env).empty()) {
            console.print_error("Error: OPEN_AI_API_KEY environment variable not set");
            return 1;
        }

        // Load settings (must have existing settings for server mode).
        auto existing = load_settings();
        if (!existing.has_value() || !existing->is_valid()) {
            console.print_error("Error: No valid settings found. Run 'crag <files>' first to create an index.");
            return 1;
        }

        Settings settings = *existing;

        // Validate chats - remove entries for deleted chat files
        validate_chats(settings);
        save_settings(settings);

        console.print_colored("Using model: ", ansi::GREEN);
        console.println(settings.model);
        console.print_colored("Vector store: ", ansi::GREEN);
        console.println(settings.vector_store_id);

        // Determine reasoning effort.
        std::string reasoning_effort = settings.reasoning_effort;
        if (thinking != '\0') {
            auto it = THINKING_MAP.find(thinking);
            if (it != THINKING_MAP.end()) {
                reasoning_effort = it->second;
            }
        }
        console.print_colored("Reasoning effort: ", ansi::GREEN);
        console.println(reasoning_effort);

        std::string system_prompt = build_system_prompt();

        // Create OpenAI client.
        OpenAIClient client(api_key_env);

        // Use embedded resources by default, or filesystem if --www-dir specified.
        std::unique_ptr<HttpServer> http_server;
        if (www_dir.empty()) {
            http_server = std::make_unique<HttpServer>();
            console.println("Serving web UI from embedded resources");
        } else {
            http_server = std::make_unique<HttpServer>(www_dir);
            console.println("Serving web UI from directory: " + www_dir);
        }

        // Pass settings and client to HTTP server for API endpoints
        http_server->set_settings(&settings);
        http_server->set_client(&client);

        // Create WebSocket server for chat.
        WebSocketServer ws_server(
            client,
            settings.model,
            settings.vector_store_id,
            reasoning_effort,
            system_prompt,
            LOG_DIR
        );

        // Pass settings to WebSocket server for persisting chat info
        ws_server.set_settings(&settings);

        ws_server.on_start([&console](const std::string& address, int port) {
            console.print_success("WebSocket server listening on ws://" +
                (address == "0.0.0.0" ? "localhost" : address) + ":" + std::to_string(port) + "/");
        });

        http_server->on_start([&console](const std::string& address, int port) {
            console.println();
            std::string display_addr = (address == "0.0.0.0") ? "localhost" : address;
            console.print_success("HTTP server running at http://" + display_addr + ":" + std::to_string(port));
            console.println("Press Ctrl+C to stop.");
            console.println();
        });

        // Create file watcher for automatic reindexing
        FileWatcher file_watcher(settings, client);
        file_watcher.on_reindex([&console, &ws_server](size_t added, size_t modified, size_t removed) {
            std::string msg = "Reindexed: ";
            if (added > 0) msg += std::to_string(added) + " added";
            if (modified > 0) {
                if (added > 0) msg += ", ";
                msg += std::to_string(modified) + " modified";
            }
            if (removed > 0) {
                if (added > 0 || modified > 0) msg += ", ";
                msg += std::to_string(removed) + " removed";
            }
            console.print_info("[FileWatcher] " + msg);

            // Notify all connected WebSocket clients
            ws_server.broadcast_reindex(added, modified, removed);
        });
        file_watcher.start();
        console.print_info("File watcher started (checking every 5 seconds)");

        // Start WebSocket server on same port (will handle /ws path).
        // Note: IXWebSocket server runs on its own port, so we use port+1 for WebSocket.
        int ws_port = server_port + 1;
        if (!ws_server.start(server_address, ws_port)) {
            console.print_error("Failed to start WebSocket server on " + server_address + ":" + std::to_string(ws_port));
            return 1;
        }

        // Start HTTP server (blocks until stopped).
        if (!http_server->start(server_address, server_port)) {
            console.print_error("Failed to start HTTP server on " + server_address + ":" + std::to_string(server_port));
            return 1;
        }

        // File watcher will be stopped automatically when it goes out of scope
        return 0;
    }

    // MCP server mode - run as MCP server for Claude Code integration.
    if (mcp_mode) {
        // Check for API key.
        const char* api_key_env = std::getenv("OPEN_AI_API_KEY");
        if (!api_key_env || std::string(api_key_env).empty()) {
            std::cerr << "Error: OPEN_AI_API_KEY environment variable not set" << std::endl;
            return 1;
        }

        // Load settings (must have existing settings for MCP mode).
        auto existing = load_settings();
        if (!existing.has_value() || !existing->is_valid()) {
            std::cerr << "Error: No valid settings found. Run 'crag <files>' first to create an index." << std::endl;
            return 1;
        }

        Settings settings = *existing;

        // Validate chats - remove entries for deleted chat files
        validate_chats(settings);
        save_settings(settings);

        // Determine reasoning effort.
        std::string reasoning_effort = settings.reasoning_effort;
        if (thinking != '\0') {
            auto it = THINKING_MAP.find(thinking);
            if (it != THINKING_MAP.end()) {
                reasoning_effort = it->second;
            }
        }

        std::string system_prompt = build_system_prompt();

        // Create OpenAI client.
        OpenAIClient client(api_key_env);

        // Create and run MCP server.
        std::cerr << "MCP: Starting crag MCP server" << std::endl;
        std::cerr << "MCP: Model: " << settings.model << std::endl;
        std::cerr << "MCP: Vector store: " << settings.vector_store_id << std::endl;

        // Create file watcher for automatic reindexing
        FileWatcher file_watcher(settings, client);
        file_watcher.on_reindex([](size_t added, size_t modified, size_t removed) {
            std::cerr << "[FileWatcher] Reindexed: "
                      << added << " added, "
                      << modified << " modified, "
                      << removed << " removed" << std::endl;
        });
        file_watcher.start();
        std::cerr << "MCP: File watcher started (checking every 5 seconds)" << std::endl;

        MCPServer mcp_server(
            client,
            settings,
            settings.model,
            settings.vector_store_id,
            reasoning_effort,
            system_prompt,
            LOG_DIR
        );

        mcp_server.run();  // Blocks until stdin closes

        // File watcher will be stopped automatically when it goes out of scope
        return 0;
    }

    // Check for API key.
    const char* api_key = std::getenv("OPEN_AI_API_KEY");
    if (!api_key || std::string(api_key).empty()) {
        console.print_error("Error: OPEN_AI_API_KEY environment variable not set");
        return 1;
    }

    OpenAIClient client(api_key);

    // Load or create settings.
    Settings settings = load_or_create_settings(files, reindex, rebuild, non_interactive, client, console);

    // Determine reasoning effort.
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

    std::string system_prompt = build_system_prompt();

    // Create chat session.
    ChatSession chat(system_prompt, LOG_DIR);

    // Markdown rendering: enabled for interactive mode without --plain.
    bool render_markdown = !non_interactive && !plain_output;

    // Process a single query. If hidden is true, the user message won't be logged.
    // Returns true if completed normally, false if cancelled.
    auto process_query = [&](const std::string& user_input, bool hidden = false) -> bool {
        if (hidden) {
            chat.add_hidden_user_message(user_input);
        } else {
            chat.add_user_message(user_input);
        }

        std::string streamed_text;
        std::atomic<bool> first_chunk{true};
        std::atomic<bool> stop_spinner{false};
        std::atomic<bool> cancel_requested{false};
        std::atomic<bool> streaming_started{false};

        // Set global pointer for signal handler.
        g_stop_spinner = &stop_spinner;

        // Animated spinner in background thread.
        std::thread spinner_thread;
        if (!non_interactive) {
            spinner_thread = std::thread([&]() {
                const char* frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
                int frame = 0;
                while (!stop_spinner.load()) {
                    std::string spinner_line = "\r";
                    spinner_line += ansi::CYAN;
                    spinner_line += frames[frame];
                    spinner_line += " Thinking... (press Esc to cancel)";
                    spinner_line += ansi::RESET;
                    console.print_raw(spinner_line);
                    console.flush();
                    frame = (frame + 1) % 10;
                    std::this_thread::sleep_for(std::chrono::milliseconds(80));
                }
            });
        }

        // Keyboard polling thread for escape key detection (interactive mode only)
        std::thread keyboard_thread;
        if (!non_interactive && terminal::is_tty()) {
            keyboard_thread = std::thread([&]() {
                // Set up raw mode for immediate key detection
                struct termios orig_termios, raw_termios;
                tcgetattr(STDIN_FILENO, &orig_termios);
                raw_termios = orig_termios;
                raw_termios.c_lflag &= ~(ECHO | ICANON);
                raw_termios.c_cc[VMIN] = 0;
                raw_termios.c_cc[VTIME] = 1;  // 100ms timeout
                tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_termios);

                while (!stop_spinner.load() && !cancel_requested.load()) {
                    char c;
                    ssize_t n = read(STDIN_FILENO, &c, 1);
                    if (n == 1 && c == 27) {  // ESC key
                        cancel_requested.store(true);
                        stop_spinner.store(true);
                    }
                }

                // Restore terminal settings
                tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
            });
        }

        // Cancel callback for streaming
        auto cancel_check = [&cancel_requested]() -> bool {
            return cancel_requested.load();
        };

        try {
            StreamResult result;
            if (render_markdown) {
                // Use markdown renderer for interactive mode.
                MarkdownRenderer renderer([&](const std::string& formatted) {
                    console.print_raw(formatted);
                    console.flush();
                });

                result = client.stream_response(
                    settings.model,
                    chat.get_api_window(),
                    settings.vector_store_id,
                    reasoning_effort,
                    chat.get_openai_response_id(),
                    [&](const std::string& delta) {
                        if (first_chunk.exchange(false)) {
                            // Stop spinner and clear the line before first output.
                            stop_spinner.store(true);
                            streaming_started.store(true);
                            if (spinner_thread.joinable()) {
                                spinner_thread.join();
                            }
                            console.print_raw("\r\033[K");
                        }
                        renderer.feed(delta);
                        streamed_text += delta;
                    },
                    cancel_check
                );

                renderer.finish();
            } else {
                // Raw output for non-interactive or --plain mode.
                result = client.stream_response(
                    settings.model,
                    chat.get_api_window(),
                    settings.vector_store_id,
                    reasoning_effort,
                    chat.get_openai_response_id(),
                    [&](const std::string& delta) {
                        if (first_chunk.exchange(false) && !non_interactive) {
                            // Stop spinner and clear the line before first output.
                            stop_spinner.store(true);
                            streaming_started.store(true);
                            if (spinner_thread.joinable()) {
                                spinner_thread.join();
                            }
                            console.print_raw("\r\033[K");
                        }
                        if (non_interactive) {
                            std::cout << delta << std::flush;
                        } else {
                            console.print_raw(delta);
                            console.flush();
                        }
                        streamed_text += delta;
                    },
                    cancel_check
                );
            }

            // Check if we were cancelled
            if (cancel_requested.load()) {
                // Stop threads and show cancellation message
                stop_spinner.store(true);
                if (spinner_thread.joinable()) {
                    spinner_thread.join();
                }
                if (keyboard_thread.joinable()) {
                    keyboard_thread.join();
                }
                console.print_raw("\r\033[K");
                console.println();
                console.print_warning("Cancelled.");
                g_stop_spinner = nullptr;
                return false;
            }

            // Store the response ID for conversation continuation.
            if (!result.response_id.empty()) {
                chat.set_openai_response_id(result.response_id);
            }

            // Check if we need to compact the conversation window
            maybe_compact_chat_window(client, chat, settings.model, result.usage);
        } catch (const std::exception& e) {
            // Stop spinner on error.
            stop_spinner.store(true);
            if (spinner_thread.joinable()) {
                spinner_thread.join();
            }
            if (keyboard_thread.joinable()) {
                keyboard_thread.join();
            }
            if (!non_interactive) {
                console.print_raw("\r\033[K");  // Clear spinner line.
            }
            console.println();
            console.print_error("Error: " + std::string(e.what()));
            g_stop_spinner = nullptr;
            return false;
        }

        // Ensure spinner thread is stopped.
        stop_spinner.store(true);
        if (spinner_thread.joinable()) {
            spinner_thread.join();
        }
        if (keyboard_thread.joinable()) {
            keyboard_thread.join();
        }

        // Clear global pointer now that spinner is done.
        g_stop_spinner = nullptr;

        chat.add_assistant_message(streamed_text);

        // Index chat in settings (same as web UI)
        if (chat.is_materialized()) {
            ChatInfo chat_info;
            chat_info.id = chat.get_chat_id();
            chat_info.log_file = chat.get_log_path();
            chat_info.json_file = chat.get_json_path();
            chat_info.openai_response_id = chat.get_openai_response_id();
            chat_info.created_at = chat.get_created_at();
            chat_info.title = chat.get_title();
            chat_info.agent_id = chat.get_agent_id();

            upsert_chat(settings, chat_info);
            save_settings(settings);
        }

        return true;
    };

    // Non-interactive mode.
    if (non_interactive) {
        std::string user_input;
        std::getline(std::cin, user_input);

        // Read all remaining input if any.
        std::string line;
        while (std::getline(std::cin, line)) {
            user_input += "\n" + line;
        }

        // Trim whitespace.
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

    // Interactive chat loop.
    console.println();
    console.print_header("=== RAG CLI Ready ===");
    console.println();
    console.println("Type 'quit' to exit. Press Enter twice quickly to submit.");
    console.println();

    // Create input editor.
    InputEditor input_editor([](const std::string& text) {
        std::cout << text;
        std::cout.flush();
    }, true);

    while (true) {
        std::string user_input = input_editor.read_input();

        // Trim whitespace.
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

        process_query(user_input);

        console.println();
    }

    return 0;
}
