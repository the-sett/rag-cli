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
#include "providers/factory.hpp"
#include "providers/provider.hpp"

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

// Checks which API keys are available and returns list of available providers.
std::vector<Provider> get_available_providers() {
    std::vector<Provider> available;

    const char* openai_key = std::getenv("OPEN_AI_API_KEY");
    if (openai_key && std::string(openai_key).length() > 0) {
        available.push_back(Provider::OpenAI);
    }

    const char* gemini_key = std::getenv("GEMINI_API_KEY");
    if (gemini_key && std::string(gemini_key).length() > 0) {
        available.push_back(Provider::Gemini);
    }

    return available;
}

// Gets the API key for the given provider.
std::string get_api_key_for_provider(Provider provider) {
    switch (provider) {
        case Provider::OpenAI: {
            const char* key = std::getenv("OPEN_AI_API_KEY");
            return key ? std::string(key) : "";
        }
        case Provider::Gemini: {
            const char* key = std::getenv("GEMINI_API_KEY");
            return key ? std::string(key) : "";
        }
        default:
            return "";
    }
}

// Creates a provider instance for the given provider type.
std::unique_ptr<providers::IAIProvider> create_provider(Provider provider) {
    std::string api_key = get_api_key_for_provider(provider);
    if (api_key.empty()) {
        throw std::runtime_error("API key not set for provider");
    }

    providers::ProviderConfig config;
    config.type = (provider == Provider::OpenAI)
        ? providers::ProviderType::OpenAI
        : providers::ProviderType::Gemini;
    config.api_key = api_key;

    return providers::ProviderFactory::create(config);
}

// Prompts the user to select a provider.
Provider select_provider(const std::vector<Provider>& available, Console& console) {
    if (available.empty()) {
        console.print_error("No API keys found!");
        console.println("Set OPEN_AI_API_KEY or GEMINI_API_KEY environment variable.");
        std::exit(1);
    }

    // Always show provider selection, marking which have API keys available
    console.println();
    console.print_header("Available providers:");

    // Show all providers, indicating which have API keys
    std::vector<Provider> all_providers = {Provider::OpenAI, Provider::Gemini};
    for (size_t i = 0; i < all_providers.size(); ++i) {
        std::string name = (all_providers[i] == Provider::OpenAI) ? "OpenAI" : "Google Gemini";
        bool has_key = std::find(available.begin(), available.end(), all_providers[i]) != available.end();
        if (has_key) {
            console.println("  " + std::to_string(i + 1) + ". " + name);
        } else {
            console.println("  " + std::to_string(i + 1) + ". " + name + " (no API key set)");
        }
    }

    console.println();
    while (true) {
        std::string choice = console.prompt("Select provider number", "1");
        try {
            size_t idx = std::stoul(choice) - 1;
            if (idx < all_providers.size()) {
                Provider selected = all_providers[idx];
                bool has_key = std::find(available.begin(), available.end(), selected) != available.end();
                if (!has_key) {
                    std::string env_var = (selected == Provider::OpenAI) ? "OPEN_AI_API_KEY" : "GEMINI_API_KEY";
                    console.print_error("Error: " + env_var + " environment variable not set");
                    continue;
                }
                std::string name = (selected == Provider::OpenAI) ? "OpenAI" : "Google Gemini";
                console.print_success("Selected: " + name);
                return selected;
            }
        } catch (...) {
            // Invalid input, continue prompting.
        }
        console.print_error("Invalid choice, try again");
    }
}

// Prompts the user to select a model from available models.
std::string select_model(providers::IAIProvider& provider, Console& console) {
    console.println();
    console.print_warning("Fetching available models...");

    std::vector<providers::ModelInfo> models;
    try {
        models = provider.models().list_models();
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
        std::string display = models[i].display_name.empty() ? models[i].id : models[i].display_name;
        console.println("  " + std::to_string(i + 1) + ". " + display);
    }

    console.println();
    while (true) {
        std::string choice = console.prompt("Select model number", "1");
        try {
            size_t idx = std::stoul(choice) - 1;
            if (idx < models.size()) {
                std::string selected = models[idx].id;
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
// Returns both the settings and a provider instance (created or based on existing settings).
std::pair<Settings, std::unique_ptr<providers::IAIProvider>> load_or_create_settings(
    const std::vector<std::string>& files,
    bool reindex,
    bool rebuild,
    bool non_interactive,
    Console& console
) {
    auto existing = load_settings();
    bool has_valid_settings = existing.has_value() && existing->is_valid();

    // If rebuild is requested with existing valid settings, delete everything and recreate.
    if (rebuild && has_valid_settings) {
        Settings settings = *existing;

        // Create provider based on existing settings
        auto provider = create_provider(settings.provider);

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
            *provider,
            console,
            settings.indexed_files
        );

        if (new_vector_store_id.empty()) {
            std::exit(1);
        }

        settings.vector_store_id = new_vector_store_id;

        // Save updated settings.
        save_settings(settings);
        return {settings, std::move(provider)};
    }

    // If reindex is requested with existing valid settings, do incremental update.
    if (reindex && has_valid_settings) {
        Settings settings = *existing;

        // Create provider based on existing settings
        auto provider = create_provider(settings.provider);

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
        update_vector_store(settings.vector_store_id, diff, *provider, console, settings.indexed_files);

        // Save updated settings.
        save_settings(settings);
        return {settings, std::move(provider)};
    }

    // No reindex requested and we have valid settings - just use them.
    if (!reindex && has_valid_settings) {
        Settings settings = *existing;

        // Create provider based on existing settings
        auto provider = create_provider(settings.provider);

        if (!non_interactive) {
            std::string provider_name = (settings.provider == Provider::OpenAI) ? "OpenAI" : "Google Gemini";
            console.print_colored("Provider: ", ansi::GREEN);
            console.println(provider_name);
            console.print_colored("Using model: ", ansi::GREEN);
            console.println(settings.model);
            if (!settings.reasoning_effort.empty()) {
                console.print_colored("Reasoning effort: ", ansi::GREEN);
                console.println(settings.reasoning_effort);
            }
            console.print_colored("Using vector store: ", ansi::GREEN);
            console.println(settings.vector_store_id);
            if (!settings.file_patterns.empty()) {
                console.print_colored("Indexed patterns: ", ansi::GREEN);
                std::string patterns;
                for (size_t i = 0; i < settings.file_patterns.size(); ++i) {
                    if (i > 0) patterns += ", ";
                    patterns += settings.file_patterns[i];
                }
                console.println(patterns);
            }
            console.print_colored("Indexed files: ", ansi::GREEN);
            console.println(std::to_string(settings.indexed_files.size()));
        }
        return {settings, std::move(provider)};
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

    // First time setup flow: provider → model → thinking level → vector store

    // Step 1: Select provider
    auto available_providers = get_available_providers();
    if (available_providers.empty()) {
        console.print_error("No API keys found!");
        console.println("Set OPEN_AI_API_KEY or GEMINI_API_KEY environment variable.");
        std::exit(1);
    }

    Provider selected_provider = select_provider(available_providers, console);
    auto provider = create_provider(selected_provider);

    // Step 2: Select model
    std::string selected_model = select_model(*provider, console);

    // Step 3: Select reasoning effort
    std::string reasoning_effort = select_reasoning_effort(console);

    // Step 4: Create knowledge store and upload files
    Settings settings;
    settings.provider = selected_provider;
    settings.model = selected_model;
    settings.reasoning_effort = reasoning_effort;
    settings.file_patterns = files;
    settings.vector_store_id = create_vector_store(files, *provider, console, settings.indexed_files);

    if (settings.vector_store_id.empty()) {
        std::exit(1);
    }

    save_settings(settings);
    return {settings, std::move(provider)};
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

        // Load settings (must have existing settings for server mode).
        auto existing = load_settings();
        if (!existing.has_value() || !existing->is_valid()) {
            console.print_error("Error: No valid settings found. Run 'crag <files>' first to create an index.");
            return 1;
        }

        Settings settings = *existing;

        // Check for API key for the configured provider.
        std::string api_key = get_api_key_for_provider(settings.provider);
        if (api_key.empty()) {
            std::string env_var = (settings.provider == Provider::OpenAI) ? "OPEN_AI_API_KEY" : "GEMINI_API_KEY";
            console.print_error("Error: " + env_var + " environment variable not set");
            return 1;
        }

        // Validate chats - remove entries for deleted chat files
        validate_chats(settings);
        save_settings(settings);

        std::string provider_name = (settings.provider == Provider::OpenAI) ? "OpenAI" : "Google Gemini";
        console.print_colored("Provider: ", ansi::GREEN);
        console.println(provider_name);
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

        // Create provider for file operations
        auto provider = create_provider(settings.provider);

        // Create OpenAI client (for backward compatibility with chat services).
        // TODO: Migrate WebSocketServer to use IAIProvider directly.
        OpenAIClient client(api_key);

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
        FileWatcher file_watcher(settings, *provider);
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
        // Load settings (must have existing settings for MCP mode).
        auto existing = load_settings();
        if (!existing.has_value() || !existing->is_valid()) {
            std::cerr << "Error: No valid settings found. Run 'crag <files>' first to create an index." << std::endl;
            return 1;
        }

        Settings settings = *existing;

        // Check for API key for the configured provider.
        std::string api_key = get_api_key_for_provider(settings.provider);
        if (api_key.empty()) {
            std::string env_var = (settings.provider == Provider::OpenAI) ? "OPEN_AI_API_KEY" : "GEMINI_API_KEY";
            std::cerr << "Error: " << env_var << " environment variable not set" << std::endl;
            return 1;
        }

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

        // Create provider for file operations
        auto provider = create_provider(settings.provider);

        // Create OpenAI client (for backward compatibility with chat services).
        // TODO: Migrate MCPServer to use IAIProvider directly.
        OpenAIClient client(api_key);

        // Create and run MCP server.
        std::cerr << "MCP: Starting crag MCP server" << std::endl;
        std::cerr << "MCP: Model: " << settings.model << std::endl;
        std::cerr << "MCP: Vector store: " << settings.vector_store_id << std::endl;

        // Create file watcher for automatic reindexing
        FileWatcher file_watcher(settings, *provider);
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

    // Check for at least one API key.
    auto available_providers = get_available_providers();
    if (available_providers.empty()) {
        console.print_error("Error: No API keys found");
        console.println("Set OPEN_AI_API_KEY or GEMINI_API_KEY environment variable.");
        return 1;
    }

    // Load or create settings (includes provider selection for first-time setup).
    auto settings_and_provider = load_or_create_settings(files, reindex, rebuild, non_interactive, console);
    Settings settings = std::move(settings_and_provider.first);
    auto provider = std::move(settings_and_provider.second);

    // Use the provider for chat operations
    // Note: provider was created by load_or_create_settings() and is the correct type

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
            // Build chat configuration
            providers::ChatConfig chat_config;
            chat_config.model = settings.model;
            chat_config.reasoning_effort = reasoning_effort;
            chat_config.knowledge_store_id = settings.vector_store_id;
            chat_config.previous_response_id = chat.get_openai_response_id();

            StreamResult result;
            if (render_markdown) {
                // Use markdown renderer for interactive mode.
                MarkdownRenderer renderer([&](const std::string& formatted) {
                    console.print_raw(formatted);
                    console.flush();
                });

                result = provider->chat().stream_response(
                    chat_config,
                    chat.get_api_window(),
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
                result = provider->chat().stream_response(
                    chat_config,
                    chat.get_api_window(),
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
            maybe_compact_chat_window(*provider, chat, settings.model, result.usage);
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
