#pragma once

/**
 * Factory for creating AI provider instances.
 */

#include "provider.hpp"
#include <memory>
#include <stdexcept>

namespace rag::providers {

/**
 * Configuration for creating a provider instance.
 */
struct ProviderConfig {
    ProviderType type;
    std::string api_key;
    std::string api_base_url;  // Optional base URL override
};

/**
 * Exception thrown when a provider is not available or configured.
 */
class ProviderNotAvailableError : public std::runtime_error {
public:
    explicit ProviderNotAvailableError(const std::string& message)
        : std::runtime_error(message) {}
};

/**
 * Factory for creating AI provider instances.
 */
class ProviderFactory {
public:
    /**
     * Creates a provider instance based on configuration.
     * Throws ProviderNotAvailableError if the provider cannot be created.
     */
    static std::unique_ptr<IAIProvider> create(const ProviderConfig& config);

    /**
     * Creates a provider by auto-detecting from environment variables.
     * Checks for API keys in this order:
     * 1. OPENAI_API_KEY / OPEN_AI_API_KEY -> OpenAI provider
     * 2. GEMINI_API_KEY / GOOGLE_API_KEY -> Gemini provider
     * Throws ProviderNotAvailableError if no API key is found.
     */
    static std::unique_ptr<IAIProvider> create_from_environment();

    /**
     * Checks if a provider type is available (has the required dependencies).
     */
    static bool is_available(ProviderType type);

    /**
     * Returns the human-readable name for a provider type.
     */
    static std::string get_provider_name(ProviderType type);
};

} // namespace rag::providers
