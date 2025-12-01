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

<<<<<<< Updated upstream
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
=======
// Check if a file appears to be a text file by examining its content
// Returns true if the file contains only valid text (no binary/null bytes)
bool is_text_file(const std::string& filepath);

// Resolve glob patterns to a list of absolute file paths
// Supports:
//   - Literal file paths
//   - Single * wildcard (matches any characters in filename)
//   - ** recursive wildcard (matches any directory depth)
//   - Directory paths (walks all supported files recursively)
>>>>>>> Stashed changes
std::vector<std::string> resolve_file_patterns(
    const std::vector<std::string>& patterns,
    Console& console
);

} // namespace rag
