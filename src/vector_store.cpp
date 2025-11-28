#include "vector_store.hpp"
#include "file_resolver.hpp"
#include "openai_client.hpp"
#include "console.hpp"
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

namespace rag {

std::string create_vector_store(
    const std::vector<std::string>& file_patterns,
    OpenAIClient& client,
    Console& console
) {
    // Resolve file patterns to actual files
    std::vector<std::string> files_to_upload = resolve_file_patterns(file_patterns, console);

    if (files_to_upload.empty()) {
        console.print_error("Error: No supported files found");
        console.println();
        console.println("Usage: rag-cli 'docs/*.md' 'src/**/*.py'");
        console.println("Supported: .txt, .md, .pdf, .py, .js, .json, .yaml, and many more");
        return "";
    }

    console.println();
    console.print_warning("Uploading " + std::to_string(files_to_upload.size()) + " files...");

    std::vector<std::string> file_ids;
    size_t total_files = files_to_upload.size();

    for (size_t i = 0; i < files_to_upload.size(); ++i) {
        const auto& filepath = files_to_upload[i];
        std::string display_name = fs::path(filepath).filename().string();

        console.start_status("Uploading (" + std::to_string(i + 1) + "/" +
                            std::to_string(total_files) + "): " + display_name);

        try {
            std::string file_id = client.upload_file(filepath);
            file_ids.push_back(file_id);
            console.clear_status();
            console.print_success("(" + std::to_string(i + 1) + "/" +
                                 std::to_string(total_files) + ") " + filepath);
        } catch (const std::exception& e) {
            console.clear_status();
            console.print_error("Failed to upload " + filepath + ": " + e.what());
            return "";
        }
    }

    console.println();
    console.print_warning("Creating vector store...");

    std::string vector_store_id;
    try {
        vector_store_id = client.create_vector_store("cli-rag-store");
        console.print_success("Vector store created: " + vector_store_id);
    } catch (const std::exception& e) {
        console.print_error("Failed to create vector store: " + std::string(e.what()));
        return "";
    }

    console.print_warning("Starting batch indexing...");

    std::string batch_id;
    try {
        batch_id = client.create_file_batch(vector_store_id, file_ids);
    } catch (const std::exception& e) {
        console.print_error("Failed to create file batch: " + std::string(e.what()));
        return "";
    }

    console.start_status("Indexing " + std::to_string(total_files) +
                        " files (this may take a minute)...");

    // Poll for batch completion
    while (true) {
        try {
            std::string status = client.get_batch_status(vector_store_id, batch_id);

            if (status == "completed") {
                break;
            } else if (status == "failed") {
                console.clear_status();
                console.print_error("Error: Vector store indexing failed");
                return "";
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        } catch (const std::exception& e) {
            console.clear_status();
            console.print_error("Error checking batch status: " + std::string(e.what()));
            return "";
        }
    }

    console.clear_status();
    console.print_success("Vector store ready.");

    return vector_store_id;
}

} // namespace rag
