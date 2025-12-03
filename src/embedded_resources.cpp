#include "embedded_resources.hpp"
#include "www_resources.hpp"
#include "third_party/miniz.h"
#include <algorithm>
#include <cstring>

namespace rag {

EmbeddedResources::EmbeddedResources() {
    load_archive();
}

EmbeddedResources::~EmbeddedResources() {
    if (archive_) {
        mz_zip_reader_end(static_cast<mz_zip_archive*>(archive_));
        delete static_cast<mz_zip_archive*>(archive_);
    }
}

void EmbeddedResources::load_archive() {
    auto* zip = new mz_zip_archive();
    std::memset(zip, 0, sizeof(mz_zip_archive));

    if (!mz_zip_reader_init_mem(zip,
                                 embedded::www_zip_data,
                                 embedded::www_zip_size,
                                 0)) {
        delete zip;
        return;
    }

    archive_ = zip;
    valid_ = true;
}

std::optional<std::vector<uint8_t>> EmbeddedResources::get_file(const std::string& path) const {
    if (!valid_ || !archive_) {
        return std::nullopt;
    }

    auto* zip = static_cast<mz_zip_archive*>(archive_);

    // Normalize path - remove leading slash if present
    std::string normalized_path = path;
    if (!normalized_path.empty() && normalized_path[0] == '/') {
        normalized_path = normalized_path.substr(1);
    }

    // Handle root path as index.html
    if (normalized_path.empty()) {
        normalized_path = "index.html";
    }

    int file_index = mz_zip_reader_locate_file(zip, normalized_path.c_str(), nullptr, 0);
    if (file_index < 0) {
        return std::nullopt;
    }

    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(zip, file_index, &file_stat)) {
        return std::nullopt;
    }

    std::vector<uint8_t> content(file_stat.m_uncomp_size);
    if (!mz_zip_reader_extract_to_mem(zip, file_index, content.data(), content.size(), 0)) {
        return std::nullopt;
    }

    return content;
}

std::string EmbeddedResources::get_mime_type(const std::string& path) {
    // Find the extension
    auto dot_pos = path.rfind('.');
    if (dot_pos == std::string::npos) {
        return "application/octet-stream";
    }

    std::string ext = path.substr(dot_pos);
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Common MIME types
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css") return "text/css";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".woff") return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ttf") return "font/ttf";
    if (ext == ".txt") return "text/plain";
    if (ext == ".xml") return "application/xml";

    return "application/octet-stream";
}

} // namespace rag
