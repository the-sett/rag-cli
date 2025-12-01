#include "http_server.hpp"
#include <httplib.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace rag {

HttpServer::HttpServer(const std::string& www_dir) : www_dir_(www_dir) {}

bool HttpServer::start(const std::string& address, int port) {
    httplib::Server svr;

    // Check if www directory exists
    if (!fs::exists(www_dir_)) {
        return false;
    }

    // Serve static files from www directory
    svr.set_mount_point("/", www_dir_);

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
