#pragma once

/**
 * Embedded resources handler.
 *
 * Provides access to web resources embedded in the executable
 * as a compressed zip archive.
 */

#include <string>
#include <optional>
#include <vector>
#include <unordered_map>

namespace rag {

/**
 * Handles extraction and serving of embedded web resources.
 */
class EmbeddedResources {
public:
    EmbeddedResources();
    ~EmbeddedResources();

    // Non-copyable
    EmbeddedResources(const EmbeddedResources&) = delete;
    EmbeddedResources& operator=(const EmbeddedResources&) = delete;

    // Returns true if resources were loaded successfully.
    bool is_valid() const { return valid_; }

    // Gets a file's content by path (e.g., "index.html" or "elm.js").
    // Returns nullopt if the file doesn't exist.
    std::optional<std::vector<uint8_t>> get_file(const std::string& path) const;

    // Gets the MIME type for a file based on its extension.
    static std::string get_mime_type(const std::string& path);

private:
    void load_archive();

    bool valid_ = false;
    void* archive_ = nullptr;  // mz_zip_archive*
};

} // namespace rag
