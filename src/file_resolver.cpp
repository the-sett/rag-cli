#include "file_resolver.hpp"
#include "config.hpp"
#include "console.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unordered_set>
#include <regex>

namespace fs = std::filesystem;

namespace rag {

bool is_supported_extension(const std::string& filepath) {
    fs::path p(filepath);
    std::string ext = p.extension().string();
    // Convert to lowercase for case-insensitive comparison.
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return SUPPORTED_EXTENSIONS.count(ext) > 0;
}

bool is_text_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        return false;
    }

    // Read up to 8KB to check for binary content.
    constexpr size_t SAMPLE_SIZE = 8192;
    char buffer[SAMPLE_SIZE];
    file.read(buffer, SAMPLE_SIZE);
    std::streamsize bytes_read = file.gcount();

    if (bytes_read == 0) {
        // Empty file - treat as text.
        return true;
    }

    // Check for null bytes or other binary indicators.
    for (std::streamsize i = 0; i < bytes_read; ++i) {
        unsigned char c = static_cast<unsigned char>(buffer[i]);
        // Null bytes indicate binary file.
        if (c == 0) {
            return false;
        }
        // Allow common text characters: printable ASCII, tabs, newlines, carriage returns.
        // Also allow UTF-8 continuation bytes (0x80-0xBF following valid lead bytes).
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
            return false;
        }
    }

    return true;
}

// Returns true if file is supported: known extension OR text content.
static bool is_supported_file(const std::string& filepath) {
    if (is_supported_extension(filepath)) {
        return true;
    }
    // Fall back to text content detection for unknown extensions.
    return is_text_file(filepath);
}

// Converts a glob pattern to an equivalent regex pattern.
static std::string glob_to_regex(const std::string& glob) {
    std::string regex;
    regex.reserve(glob.size() * 2);

    size_t i = 0;
    while (i < glob.size()) {
        char c = glob[i];

        if (c == '*') {
            if (i + 1 < glob.size() && glob[i + 1] == '*') {
                // ** matches any path components
                if (i + 2 < glob.size() && glob[i + 2] == '/') {
                    regex += "(?:.*/)?";
                    i += 3;
                } else {
                    regex += ".*";
                    i += 2;
                }
            } else {
                // * matches any characters except /
                regex += "[^/]*";
                i++;
            }
        } else if (c == '?') {
            regex += "[^/]";
            i++;
        } else if (c == '[') {
            // Character class - pass through
            regex += '[';
            i++;
            while (i < glob.size() && glob[i] != ']') {
                if (glob[i] == '\\' && i + 1 < glob.size()) {
                    regex += '\\';
                    regex += glob[i + 1];
                    i += 2;
                } else {
                    regex += glob[i];
                    i++;
                }
            }
            if (i < glob.size()) {
                regex += ']';
                i++;
            }
        } else if (c == '.' || c == '(' || c == ')' || c == '{' || c == '}' ||
                   c == '+' || c == '|' || c == '^' || c == '$' || c == '\\') {
            // Escape regex special characters
            regex += '\\';
            regex += c;
            i++;
        } else {
            regex += c;
            i++;
        }
    }

    return "^" + regex + "$";
}

// Returns true if path matches the glob pattern.
static bool matches_glob(const std::string& path, const std::string& pattern) {
    std::string regex_pattern = glob_to_regex(pattern);
    try {
        std::regex re(regex_pattern);
        return std::regex_match(path, re);
    } catch (const std::regex_error&) {
        return false;
    }
}

// Returns true if pattern contains glob wildcard characters.
static bool is_glob_pattern(const std::string& pattern) {
    return pattern.find('*') != std::string::npos ||
           pattern.find('?') != std::string::npos ||
           pattern.find('[') != std::string::npos;
}

// Collects all supported files from a directory recursively.
static void collect_files_recursive(const fs::path& dir, std::vector<std::string>& files) {
    try {
        for (const auto& entry : fs::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && is_supported_file(entry.path().string())) {
                files.push_back(fs::absolute(entry.path()).string());
            }
        }
    } catch (const fs::filesystem_error&) {
        // Skip directories we can't access.
    }
}

std::vector<std::string> resolve_file_patterns(
    const std::vector<std::string>& patterns,
    Console& console
) {
    std::vector<std::string> files;
    std::unordered_set<std::string> seen;

    for (const auto& pattern : patterns) {
        if (!is_glob_pattern(pattern)) {
            // Literal path - could be file or directory
            fs::path p(pattern);

            if (!fs::exists(p)) {
                console.print_warning("Warning: File not found: " + pattern);
                continue;
            }

            if (fs::is_directory(p)) {
                // Directory - collect all supported files recursively
                collect_files_recursive(p, files);
            } else if (fs::is_regular_file(p)) {
                if (is_supported_file(pattern)) {
                    std::string abs_path = fs::absolute(p).string();
                    if (seen.find(abs_path) == seen.end()) {
                        seen.insert(abs_path);
                        files.push_back(abs_path);
                    }
                } else {
                    console.print_warning("Warning: Unsupported file type (binary): " + pattern);
                }
            }
        } else {
            // Glob pattern - need to expand it
            // Find the base directory (part before first glob character)
            fs::path pattern_path(pattern);
            fs::path base_dir = ".";
            std::string glob_part = pattern;

            // Find the first component with a glob character
            fs::path accumulated;
            for (const auto& component : pattern_path) {
                std::string comp_str = component.string();
                if (is_glob_pattern(comp_str)) {
                    break;
                }
                accumulated /= component;
            }

            if (!accumulated.empty() && fs::exists(accumulated) && fs::is_directory(accumulated)) {
                base_dir = accumulated;
                // Adjust glob_part to be relative to base_dir
                glob_part = fs::relative(pattern_path, accumulated.parent_path()).string();
                if (accumulated.parent_path().empty()) {
                    glob_part = pattern;
                }
            }

            // Collect all files from base directory and match against pattern
            bool found_match = false;
            try {
                for (const auto& entry : fs::recursive_directory_iterator(base_dir)) {
                    if (!entry.is_regular_file()) continue;

                    // Get path relative to current directory for matching
                    std::string rel_path;
                    if (base_dir == ".") {
                        rel_path = entry.path().string();
                        // Remove leading "./" if present
                        if (rel_path.substr(0, 2) == "./") {
                            rel_path = rel_path.substr(2);
                        }
                    } else {
                        rel_path = entry.path().string();
                    }

                    if (matches_glob(rel_path, pattern)) {
                        if (!is_supported_file(entry.path().string())) {
                            console.print_warning("Warning: Unsupported file type (binary): " + rel_path);
                            continue;
                        }

                        std::string abs_path = fs::absolute(entry.path()).string();
                        if (seen.find(abs_path) == seen.end()) {
                            seen.insert(abs_path);
                            files.push_back(abs_path);
                            found_match = true;
                        }
                    }
                }
            } catch (const fs::filesystem_error&) {
                // Skip directories we can't access
            }

            if (!found_match) {
                console.print_warning("Warning: No matches for pattern: " + pattern);
            }
        }
    }

    // Remove duplicates while preserving order (already handled by seen set)
    // But let's ensure the final list only contains unique entries
    std::vector<std::string> unique_files;
    std::unordered_set<std::string> final_seen;
    for (const auto& f : files) {
        if (final_seen.find(f) == final_seen.end()) {
            final_seen.insert(f);
            unique_files.push_back(f);
        }
    }

    return unique_files;
}

} // namespace rag
