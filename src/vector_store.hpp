#pragma once

#include <string>
#include <vector>

namespace rag {

class OpenAIClient;
class Console;

// Upload files and create a new vector store
// Returns the vector store ID
std::string create_vector_store(
    const std::vector<std::string>& file_patterns,
    OpenAIClient& client,
    Console& console
);

} // namespace rag
