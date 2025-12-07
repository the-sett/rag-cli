# crag - Chat with your RAG

A command-line tool for building and querying a knowledge base using OpenAI's vector store and file search capabilities. crag lets you index your documents, code, and data files, then have natural conversations with an AI that can search and retrieve relevant information from your indexed content.

**Key features:**
- Index documents, code, and data files with a single command
- Interactive chat with context-aware responses from your knowledge base
- Rich markdown rendering with syntax-highlighted code blocks and formatted tables
- Web interface with streaming responses and table of contents navigation
- Non-interactive mode for scripting and automation
- Support for OpenAI's reasoning models with configurable thinking levels
- Incremental re-indexing (only uploads changed files)

## Prerequisites

### Build Dependencies

- CMake 3.20 or higher
- Clang compiler with C++17 support
- Ninja build system
- lld linker

### Runtime Dependencies

- libcurl4
- An OpenAI API key with access to the Responses API

### Installing Dependencies (Debian/Ubuntu)

```bash
# Build dependencies
sudo apt install cmake ninja-build clang lld

# Runtime and development libraries
sudo apt install libcurl4-openssl-dev nlohmann-json3-dev libcli11-dev
```

## Building from Source

```bash
# Clone the repository
git clone https://github.com/the-sett/rag-cli.git
cd rag-cli

# Configure with CMake preset
cmake --preset ninja-clang-lld-linux

# Build
cmake --build --preset build
```

The executable will be at `build/crag`.

## Building a Debian Package

After building, create a `.deb` package:

```bash
cd build
cpack
```

This generates `crag_<version>_amd64.deb` which can be installed with:

```bash
sudo dpkg -i crag_0.1.0_amd64.deb
```

## Configuration

### OpenAI API Key

crag requires an OpenAI API key. Set it as an environment variable:

```bash
export OPEN_AI_API_KEY="sk-your-api-key-here"
```

To make this permanent, add it to your shell profile (`~/.bashrc`, `~/.zshrc`, etc.):

```bash
echo 'export OPEN_AI_API_KEY="sk-your-api-key-here"' >> ~/.bashrc
source ~/.bashrc
```

You can obtain an API key from [OpenAI's platform](https://platform.openai.com/api-keys).

### Settings File

crag stores its configuration in a `.crag.json` file in the current working directory. This file is created automatically on first run and contains:

- `model` - The selected OpenAI model
- `reasoning_effort` - Thinking level (low/medium/high)
- `vector_store_id` - OpenAI vector store ID for your indexed files
- `file_patterns` - The file patterns that were indexed

On subsequent runs in the same directory, crag reads this file to restore your previous session's settings. This means:

- You don't need to re-select the model or thinking level each time
- Your indexed files remain available without re-uploading
- You can simply run `crag` to continue chatting with your knowledge base

To start fresh or index different files, use the `--reindex` flag or delete the `.crag.json` file.

This file should be added to `.gitignore` as it contains API-specific IDs.

## Usage

### Indexing Files

On first run, provide files or glob patterns to index:

```bash
# Index specific files
crag README.md docs/guide.md

# Index with glob patterns
crag 'docs/*.md'

# Index recursively
crag 'docs/**/*.md'

# Index multiple patterns
crag 'docs/**/*.md' 'src/**/*.py' '*.txt'

# Index a directory (all supported files)
crag knowledge/
```

You'll be prompted to select a model and reasoning effort level. These choices are saved to `.crag.json`.

### Interactive Chat

Once indexed, run without arguments to start chatting:

```bash
crag
```

Type your questions and the AI will search your indexed files for relevant context. Press Enter twice quickly to submit multi-line input. Type `quit` or `exit` to end the session.

### Re-indexing

To update your knowledge base with new or changed files:

```bash
crag --reindex 'docs/**/*.md'
```

This performs an incremental update, uploading only new or modified files and removing deleted ones from the vector store. Your model and thinking level settings are preserved.

### Non-interactive Mode

For scripting and automation, use non-interactive mode:

```bash
echo "What is garbage collection?" | crag -n
```

This reads the query from stdin, outputs the response to stdout, and exits.

### Web Interface

crag includes a built-in web interface for a richer chat experience:

```bash
crag --server
```

This starts an HTTP server (default port 8192) and WebSocket server (port 8193) serving an embedded web application. Open `http://localhost:8192` in your browser.

You can customise the server:

```bash
# Use a different port
crag --server --port 3000

# Bind to localhost only
crag --server --address 127.0.0.1

# Serve from local files during development
crag --server --www-dir ./build/www
```

The web interface features:
- Two-column layout with table of contents sidebar and chat area
- Streaming markdown rendering as responses arrive
- Incremental table of contents built from headings during streaming
- Click-to-navigate from TOC to content
- Scroll position tracking with current section highlighting
- User queries shown in TOC for easy navigation

### Command-Line Options

| Option | Description |
|--------|-------------|
| `FILE ...` | Files or glob patterns to index |
| `--reindex` | Incrementally update the index (add new, update changed, remove deleted) |
| `-t`, `--thinking` | Override thinking level: `l`=low, `m`=medium, `h`=high |
| `-n`, `--non-interactive` | Read query from stdin, write response to stdout, exit |
| `--plain` | Disable markdown rendering, output raw text |
| `-s`, `--server` | Run in server mode with web interface |
| `-p`, `--port` | Port for web server (default: 8192) |
| `--address` | Bind address for web server (default: 0.0.0.0) |
| `--www-dir` | Serve web files from directory instead of embedded resources (for development) |

### Glob Pattern Examples

| Pattern | Matches |
|---------|---------|
| `*.md` | All markdown files in current directory |
| `docs/*.md` | All markdown files in docs/ |
| `docs/**/*.md` | All markdown files in docs/ recursively |
| `src/**/*.py` | All Python files in src/ recursively |
| `*.{md,txt}` | All .md and .txt files (shell expansion) |

## Supported File Types

crag supports a wide range of file types:

- **Documents**: `.txt`, `.md`, `.pdf`, `.doc`, `.docx`, `.pptx`, `.html`
- **Data**: `.json`, `.xml`, `.csv`, `.yaml`, `.yml`
- **Code**: `.py`, `.js`, `.ts`, `.jsx`, `.tsx`, `.java`, `.c`, `.cpp`, `.h`, `.hpp`, `.cs`, `.go`, `.rs`, `.rb`, `.php`, `.swift`, `.kt`, `.scala`, `.r`, `.sh`, `.bash`, `.sql`, `.lua`, `.pl`, `.hs`, `.elm`, `.ex`, `.clj`, `.lisp`, `.ml`, `.fs`
- **Config**: `.toml`, `.ini`, `.cfg`, `.conf`, `.tex`, `.rst`, `.org`

## Project Structure

```
rag-cli/
├── src/
│   ├── main.cpp                 # Entry point and CLI handling
│   ├── config.hpp               # Constants and configuration
│   ├── console.hpp/cpp          # Terminal output with ANSI colors
│   ├── settings.hpp/cpp         # Settings file management
│   ├── file_resolver.hpp/cpp    # Glob pattern resolution
│   ├── openai_client.hpp/cpp    # OpenAI API client
│   ├── vector_store.hpp/cpp     # Vector store management
│   ├── chat.hpp/cpp             # Chat session and logging
│   ├── markdown_renderer.hpp/cpp # Streaming markdown rendering
│   ├── input_editor.hpp/cpp     # Rich terminal input editing
│   ├── terminal.hpp/cpp         # Terminal settings management
│   ├── web_server.hpp/cpp       # HTTP and WebSocket servers
│   └── elm/                     # Elm web application source
│       ├── Main.elm             # Main application
│       ├── Main/Style.elm       # CSS styles (Scottish Gov Design System)
│       └── Markdown/            # Streaming markdown rendering
├── www/
│   ├── index.html               # Web app HTML shell
│   ├── index.ts                 # TypeScript entry point
│   └── websockets.ts            # WebSocket port handling
├── tests/
│   └── test_markdown_renderer.cpp # Markdown renderer unit tests
├── CMakeLists.txt               # Build configuration
├── CMakePresets.json            # CMake presets for Ninja/Clang
├── LICENSE
└── README.md
```

## License

MIT
