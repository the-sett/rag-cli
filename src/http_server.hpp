#pragma once

/**
 * HTTP server for serving the Elm web interface.
 *
 * Serves static files either from embedded resources (compiled into
 * the executable) or from a filesystem directory.
 */

#include <string>
#include <functional>
#include <memory>

namespace rag {

class EmbeddedResources;

/**
 * Simple HTTP server for serving static files.
 */
class HttpServer {
public:
    // Creates a server that serves from embedded resources.
    HttpServer();

    // Creates a server that will serve files from the given directory.
    explicit HttpServer(const std::string& www_dir);

    ~HttpServer();

    // Starts the server on the given address and port.
    // This call blocks until the server is stopped.
    // Returns true if server started successfully, false otherwise.
    bool start(const std::string& address, int port);

    // Stops the server.
    void stop();

    // Sets a callback to be called when the server starts.
    void on_start(std::function<void(const std::string&, int)> callback);

    // Returns true if using embedded resources, false if using filesystem.
    bool using_embedded() const { return use_embedded_; }

private:
    std::string www_dir_;
    bool use_embedded_ = false;
    std::unique_ptr<EmbeddedResources> embedded_resources_;
    std::function<void(const std::string&, int)> on_start_callback_;
    bool running_ = false;
};

} // namespace rag
