#include "http_server.hpp"
#include "embedded_resources.hpp"
#include "settings.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace rag {

HttpServer::HttpServer() : use_embedded_(true) {
    embedded_resources_ = std::make_unique<EmbeddedResources>();
}

HttpServer::HttpServer(const std::string& www_dir)
    : www_dir_(www_dir), use_embedded_(false) {}

HttpServer::~HttpServer() = default;

bool HttpServer::start(const std::string& address, int port) {
    httplib::Server svr;

    // REST API: Get chat list
    svr.Get("/api/chats", [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");

        if (!settings_) {
            res.status = 500;
            res.set_content(R"({"error": "Settings not available"})", "application/json");
            return;
        }

        json chats_json = json::array();
        for (const auto& chat : settings_->chats) {
            chats_json.push_back({
                {"id", chat.id},
                {"title", chat.title},
                {"created_at", chat.created_at}
            });
        }

        res.set_content(chats_json.dump(), "application/json");
    });

    // REST API: Get single chat info
    svr.Get(R"(/api/chats/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");

        if (!settings_) {
            res.status = 500;
            res.set_content(R"({"error": "Settings not available"})", "application/json");
            return;
        }

        std::string chat_id = req.matches[1];
        const ChatInfo* chat = find_chat(*settings_, chat_id);

        if (!chat) {
            res.status = 404;
            res.set_content(R"({"error": "Chat not found"})", "application/json");
            return;
        }

        json chat_json = {
            {"id", chat->id},
            {"title", chat->title},
            {"created_at", chat->created_at},
            {"openai_response_id", chat->openai_response_id}
        };

        res.set_content(chat_json.dump(), "application/json");
    });

    if (use_embedded_) {
        // Serve from embedded resources
        if (!embedded_resources_ || !embedded_resources_->is_valid()) {
            return false;
        }

        // Set up handler for all GET requests
        svr.Get(".*", [this](const httplib::Request& req, httplib::Response& res) {
            std::string path = req.path;

            // Skip API routes (already handled above)
            if (path.substr(0, 5) == "/api/") {
                return;
            }

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
                // For client-side routing (SPA), serve index.html for unknown paths
                // This allows the Elm app to handle routes like /chat, /intro, etc.
                auto index_content = embedded_resources_->get_file("/index.html");
                if (index_content) {
                    res.set_content(
                        reinterpret_cast<const char*>(index_content->data()),
                        index_content->size(),
                        "text/html"
                    );
                } else {
                    res.status = 404;
                    res.set_content("Not Found", "text/plain");
                }
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
