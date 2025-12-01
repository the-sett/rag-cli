#pragma once

/**
 * File pattern resolution for glob-style input.
 *
 * Resolves user-provided file patterns (globs, directories, literal paths)
 * to a list of absolute file paths, filtering by supported extensions.
 */

#include <string>
#include <vector>

namespace rag {

class Console;

// Returns true if the file has an extension supported by OpenAI file_search.
bool is_supported_extension(const std::string& filepath);

// Returns true if the file appears to be a text file by examining its content.
// Checks for null bytes and control characters that indicate binary content.
bool is_text_file(const std::string& filepath);

/**
 * Resolves glob patterns to a list of absolute file paths.
 *
 * Supports:
 *   - Literal file paths (e.g., "README.md")
 *   - Single * wildcard (matches any characters in filename)
 *   - ** recursive wildcard (matches any directory depth)
 *   - Directory paths (walks all supported files recursively)
 *
 * Warnings are printed to console for missing files or unsupported types.
 */
std::vector<std::string> resolve_file_patterns(
    const std::vector<std::string>& patterns,
    Console& console
);

} // namespace rag
