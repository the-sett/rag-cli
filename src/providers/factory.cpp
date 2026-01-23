#include "factory.hpp"
#include "openai/openai_provider.hpp"
#include "gemini/gemini_provider.hpp"
#include <cstdlib>

namespace rag::providers {

std::unique_ptr<IAIProvider> ProviderFactory::create(const ProviderConfig& config) {
    switch (config.type) {
        case ProviderType::OpenAI:
            return std::make_unique<openai::OpenAIProvider>(config.api_key, config.api_base_url);
        case ProviderType::Gemini:
            return std::make_unique<gemini::GeminiProvider>(config.api_key, config.api_base_url);
        default:
            throw ProviderNotAvailableError("Unknown provider type");
    }
}

std::unique_ptr<IAIProvider> ProviderFactory::create_from_environment() {
    // Check for OpenAI API key
    const char* openai_key = std::getenv("OPENAI_API_KEY");
    if (!openai_key) {
        openai_key = std::getenv("OPEN_AI_API_KEY");  // Legacy name
    }
    if (openai_key && openai_key[0] != '\0') {
        return create({ProviderType::OpenAI, openai_key, ""});
    }

    // Check for Gemini API key
    const char* gemini_key = std::getenv("GEMINI_API_KEY");
    if (!gemini_key) {
        gemini_key = std::getenv("GOOGLE_API_KEY");
    }
    if (gemini_key && gemini_key[0] != '\0') {
        return create({ProviderType::Gemini, gemini_key, ""});
    }

    throw ProviderNotAvailableError(
        "No API key found. Set OPENAI_API_KEY or GEMINI_API_KEY environment variable."
    );
}

bool ProviderFactory::is_available(ProviderType type) {
    // All providers are available if the code is compiled
    // Actual availability depends on having an API key
    switch (type) {
        case ProviderType::OpenAI:
        case ProviderType::Gemini:
            return true;
        default:
            return false;
    }
}

std::string ProviderFactory::get_provider_name(ProviderType type) {
    switch (type) {
        case ProviderType::OpenAI:
            return "OpenAI";
        case ProviderType::Gemini:
            return "Google Gemini";
        default:
            return "Unknown";
    }
}

} // namespace rag::providers
