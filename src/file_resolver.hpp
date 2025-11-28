#pragma once

#include <string>
#include <vector>

namespace rag {

class Console;

// Check if a file has a supported extension
bool is_supported_extension(const std::string& filepath);

// Resolve glob patterns to a list of absolute file paths
// Supports:
//   - Literal file paths
//   - Single * wildcard (matches any characters in filename)
//   - ** recursive wildcard (matches any directory depth)
//   - Directory paths (walks all supported files recursively)
std::vector<std::string> resolve_file_patterns(
    const std::vector<std::string>& patterns,
    Console& console
);

} // namespace rag
