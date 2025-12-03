#include "http_server.hpp"
#include "embedded_resources.hpp"
#include <httplib.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace rag {

HttpServer::HttpServer() : use_embedded_(true) {
    embedded_resources_ = std::make_unique<EmbeddedResources>();
}

HttpServer::HttpServer(const std::string& www_dir)
    : www_dir_(www_dir), use_embedded_(false) {}

HttpServer::~HttpServer() = default;

bool HttpServer::start(const std::string& address, int port) {
    httplib::Server svr;

    if (use_embedded_) {
        // Serve from embedded resources
        if (!embedded_resources_ || !embedded_resources_->is_valid()) {
            return false;
        }

        // Set up handler for all GET requests
        svr.Get(".*", [this](const httplib::Request& req, httplib::Response& res) {
            std::string path = req.path;

            // Handle root path
            if (path == "/") {
                path = "/index.html";
            }

            auto content = embedded_resources_->get_file(path);
            if (content) {
                std::string mime_type = EmbeddedResources::get_mime_type(path);
                res.set_content(
                    reinterpret_cast<const char*>(content->data()),
                    content->size(),
                    mime_type
                );
            } else {
                res.status = 404;
                res.set_content("Not Found", "text/plain");
            }
        });
    } else {
        // Serve from filesystem
        if (!fs::exists(www_dir_)) {
            return false;
        }

        svr.set_mount_point("/", www_dir_);
    }

    // Call the on_start callback before blocking
    if (on_start_callback_) {
        on_start_callback_(address, port);
    }

    running_ = true;

    // This blocks until server is stopped
    bool result = svr.listen(address, port);

    running_ = false;
    return result;
}

void HttpServer::stop() {
    running_ = false;
    // Note: httplib::Server::stop() would need to be called on the server instance
    // For now, Ctrl+C will handle shutdown via signal handler
}

void HttpServer::on_start(std::function<void(const std::string&, int)> callback) {
    on_start_callback_ = std::move(callback);
}

} // namespace rag
