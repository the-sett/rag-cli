#pragma once

/**
 * HTTP server for serving the Elm web interface.
 *
 * Serves static files from the www directory and provides
 * a simple web interface for the RAG CLI.
 */

#include <string>
#include <functional>

namespace rag {

/**
 * Simple HTTP server for serving static files.
 */
class HttpServer {
public:
    // Creates a server that will serve files from the given directory.
    explicit HttpServer(const std::string& www_dir);

    // Starts the server on the given address and port.
    // This call blocks until the server is stopped.
    // Returns true if server started successfully, false otherwise.
    bool start(const std::string& address, int port);

    // Stops the server.
    void stop();

    // Sets a callback to be called when the server starts.
    void on_start(std::function<void(const std::string&, int)> callback);

private:
    std::string www_dir_;
    std::function<void(const std::string&, int)> on_start_callback_;
    bool running_ = false;
};

} // namespace rag
