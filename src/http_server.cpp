#include "http_server.hpp"
#include "embedded_resources.hpp"
#include "openai_client.hpp"
#include "settings.hpp"
#include "verbose.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <chrono>

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
    svr.Get("/api/chats", [this](const httplib::Request& req, httplib::Response& res) {
        verbose_in("HTTP", "GET /api/chats");
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

        verbose_out("HTTP", "Response: " + std::to_string(chats_json.size()) + " chats");
        res.set_content(chats_json.dump(), "application/json");
    });

    // REST API: Get single chat info
    svr.Get(R"(/api/chats/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string chat_id = req.matches[1];
        verbose_in("HTTP", "GET /api/chats/" + chat_id);
        res.set_header("Content-Type", "application/json");

        if (!settings_) {
            res.status = 500;
            res.set_content(R"({"error": "Settings not available"})", "application/json");
            return;
        }

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

    // REST API: Delete a chat
    svr.Delete(R"(/api/chats/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string chat_id = req.matches[1];
        verbose_in("HTTP", "DELETE /api/chats/" + chat_id);
        res.set_header("Content-Type", "application/json");

        if (!settings_) {
            res.status = 500;
            res.set_content(R"({"error": "Settings not available"})", "application/json");
            return;
        }

        if (delete_chat(*settings_, chat_id)) {
            save_settings(*settings_);
            verbose_out("HTTP", "Deleted chat: " + chat_id);
            res.set_content(R"({"success": true})", "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"error": "Chat not found"})", "application/json");
        }
    });

    // REST API: Get agent list
    svr.Get("/api/agents", [this](const httplib::Request& req, httplib::Response& res) {
        verbose_in("HTTP", "GET /api/agents");
        res.set_header("Content-Type", "application/json");

        if (!settings_) {
            res.status = 500;
            res.set_content(R"({"error": "Settings not available"})", "application/json");
            return;
        }

        json agents_json = json::array();
        for (const auto& agent : settings_->agents) {
            agents_json.push_back({
                {"id", agent.id},
                {"name", agent.name},
                {"instructions", agent.instructions},
                {"created_at", agent.created_at}
            });
        }

        verbose_out("HTTP", "Response: " + std::to_string(agents_json.size()) + " agents");
        res.set_content(agents_json.dump(), "application/json");
    });

    // REST API: Create or update an agent
    svr.Post("/api/agents", [this](const httplib::Request& req, httplib::Response& res) {
        verbose_in("HTTP", "POST /api/agents: " + truncate(req.body, 200));
        res.set_header("Content-Type", "application/json");

        if (!settings_) {
            res.status = 500;
            res.set_content(R"({"error": "Settings not available"})", "application/json");
            return;
        }

        try {
            json body = json::parse(req.body);

            AgentInfo agent;
            agent.name = body.value("name", "");
            agent.instructions = body.value("instructions", "");

            if (agent.name.empty() || agent.instructions.empty()) {
                res.status = 400;
                res.set_content(R"({"error": "Name and instructions are required"})", "application/json");
                return;
            }

            // Check if updating existing agent
            if (body.contains("id") && !body["id"].get<std::string>().empty()) {
                agent.id = body["id"].get<std::string>();
                // Keep existing created_at if updating
                const AgentInfo* existing = find_agent(*settings_, agent.id);
                if (existing) {
                    agent.created_at = existing->created_at;
                } else {
                    res.status = 404;
                    res.set_content(R"({"error": "Agent not found"})", "application/json");
                    return;
                }
            } else {
                // Generate new ID and timestamp
                auto now = std::chrono::system_clock::now();
                auto time_t_now = std::chrono::system_clock::to_time_t(now);
                std::tm tm_now;
                localtime_r(&time_t_now, &tm_now);

                char id_buf[64];
                strftime(id_buf, sizeof(id_buf), "agent_%Y%m%d_%H%M%S", &tm_now);
                agent.id = id_buf;

                char time_buf[64];
                strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S", &tm_now);
                agent.created_at = time_buf;
            }

            upsert_agent(*settings_, agent);
            save_settings(*settings_);

            json response = {
                {"id", agent.id},
                {"name", agent.name},
                {"instructions", agent.instructions},
                {"created_at", agent.created_at}
            };

            verbose_out("HTTP", "Created/updated agent: " + agent.id);
            res.set_content(response.dump(), "application/json");

        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(R"({"error": "Invalid JSON"})", "application/json");
        }
    });

    // REST API: Get app settings (submit shortcut, etc.)
    svr.Get("/api/settings", [this](const httplib::Request& req, httplib::Response& res) {
        verbose_in("HTTP", "GET /api/settings");
        res.set_header("Content-Type", "application/json");

        if (!settings_) {
            res.status = 500;
            res.set_content(R"({"error": "Settings not available"})", "application/json");
            return;
        }

        json settings_json = {
            {"submit_shortcut", submit_shortcut_to_string(settings_->submit_shortcut)},
            {"model", settings_->model},
            {"reasoning_effort", settings_->reasoning_effort}
        };

        verbose_out("HTTP", "Response: " + settings_json.dump());
        res.set_content(settings_json.dump(), "application/json");
    });

    // REST API: Update app settings
    svr.Put("/api/settings", [this](const httplib::Request& req, httplib::Response& res) {
        verbose_in("HTTP", "PUT /api/settings: " + truncate(req.body, 200));
        res.set_header("Content-Type", "application/json");

        if (!settings_) {
            res.status = 500;
            res.set_content(R"({"error": "Settings not available"})", "application/json");
            return;
        }

        try {
            json body = json::parse(req.body);

            // Update submit shortcut if provided
            if (body.contains("submit_shortcut") && body["submit_shortcut"].is_string()) {
                settings_->submit_shortcut = submit_shortcut_from_string(body["submit_shortcut"].get<std::string>());
            }

            // Update model if provided
            if (body.contains("model") && body["model"].is_string()) {
                settings_->model = body["model"].get<std::string>();
            }

            // Update reasoning effort if provided
            if (body.contains("reasoning_effort") && body["reasoning_effort"].is_string()) {
                settings_->reasoning_effort = body["reasoning_effort"].get<std::string>();
            }

            save_settings(*settings_);

            json response = {
                {"submit_shortcut", submit_shortcut_to_string(settings_->submit_shortcut)},
                {"model", settings_->model},
                {"reasoning_effort", settings_->reasoning_effort}
            };

            verbose_out("HTTP", "Updated settings: " + response.dump());
            res.set_content(response.dump(), "application/json");

        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(R"({"error": "Invalid JSON"})", "application/json");
        }
    });

    // REST API: Get available models
    svr.Get("/api/models", [this](const httplib::Request& req, httplib::Response& res) {
        verbose_in("HTTP", "GET /api/models");
        res.set_header("Content-Type", "application/json");

        if (!client_) {
            res.status = 500;
            res.set_content(R"({"error": "OpenAI client not available"})", "application/json");
            return;
        }

        try {
            std::vector<std::string> models = client_->list_models();

            json response = {
                {"models", models}
            };

            verbose_out("HTTP", "Response: " + std::to_string(models.size()) + " models");
            res.set_content(response.dump(), "application/json");

        } catch (const std::exception& e) {
            res.status = 500;
            json error = {{"error", e.what()}};
            res.set_content(error.dump(), "application/json");
        }
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
