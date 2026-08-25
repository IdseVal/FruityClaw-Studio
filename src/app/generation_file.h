// Where the music-generation choice lives between sessions: two files in the
// Studio's per-user config directory — the settings as plain text, and the
// Remote model's key sealed for the logged-in user. The directory derives
// from FCS_APP_ID (ADR-008: never a product name), so a rename costs nothing
// here. Implements core::GenerationSettingsPort for the composition root.
#pragma once

#include <filesystem>

#include "core/generation.h"

namespace app {

class GenerationFile : public core::GenerationSettingsPort {
public:
    // %APPDATA%, ~/Library/Application Support, or $XDG_CONFIG_HOME, plus
    // the app id.
    static std::filesystem::path default_directory();

    explicit GenerationFile(std::filesystem::path directory);

    // A fresh install — no file yet, or one that cannot be read — reads as
    // the default GenerationSettings: off, nothing chosen.
    core::GenerationSettings read() const override;
    bool write(const core::GenerationSettings& settings) override;

    bool has_key() const override;
    bool store_key(const std::string& key) override;
    void clear_key() override;

    // The key as pasted, for a generator's transport. Never shown or logged.
    std::optional<std::string> key() const;

private:
    std::filesystem::path settings_path() const;
    std::filesystem::path key_path() const;

    std::filesystem::path directory_;
};

}  // namespace app
