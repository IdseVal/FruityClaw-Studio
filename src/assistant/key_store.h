// The on-disk adapter for core::AssistantKeyPort. One directory holds two
// files: the key, and a marker that the first-open offer was answered.
#pragma once

#include <filesystem>

#include "core/assistant_key.h"

namespace assistant {

class FileKeyStore : public core::AssistantKeyPort {
public:
    // The per-user config directory the Studio owns, derived from the app id
    // (ADR-008): %APPDATA%, ~/Library/Application Support, or $XDG_CONFIG_HOME.
    static std::filesystem::path default_directory();

    explicit FileKeyStore(std::filesystem::path directory);

    bool has_key() const override;
    std::optional<std::string> key() const override;
    bool store_key(const std::string& key) override;
    void clear_key() override;

    bool offer_made() const override;
    void record_offer() override;

private:
    std::filesystem::path key_path() const;
    std::filesystem::path offer_path() const;

    std::filesystem::path directory_;
};

}  // namespace assistant
