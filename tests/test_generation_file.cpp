// The on-disk store behind core::GenerationSettingsPort: a fresh directory
// reads as off, a written choice survives a re-read, the key round-trips
// and is never stored as the bytes that were pasted.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

#include "app/generation_file.h"

using namespace core;

namespace {

// A throw-away directory under the system temp, removed on scope exit.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        std::random_device seed;
        path = std::filesystem::temp_directory_path() /
               ("generation-test-" + std::to_string(seed()));
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

TEST_CASE("an empty config directory reads as the fresh-install settings") {
    TempDir dir;
    app::GenerationFile store(dir.path);
    CHECK(store.read() == GenerationSettings{});
    CHECK_FALSE(store.has_key());
}

TEST_CASE("written settings survive a re-read") {
    TempDir dir;
    app::GenerationFile store(dir.path);
    GenerationSettings chosen{true, "stable-audio-3", "C:/models/sa3=medium"};
    REQUIRE(store.write(chosen));
    CHECK(app::GenerationFile(dir.path).read() == chosen);
}

TEST_CASE("a model since removed from the list cannot stay enabled") {
    TempDir dir;
    app::GenerationFile store(dir.path);
    REQUIRE(store.write({true, "retired-model", ""}));
    GenerationSettings read = store.read();
    CHECK_FALSE(read.enabled);
    CHECK(read.model == "retired-model");  // kept, so the page can say why
}

TEST_CASE("the key round-trips and is not written in the clear") {
    TempDir dir;
    app::GenerationFile store(dir.path);
    CHECK_FALSE(store.store_key(""));
    REQUIRE(store.store_key("xi-secret-123"));
    CHECK(store.has_key());
    CHECK(store.key() == "xi-secret-123");
#ifdef _WIN32
    CHECK(slurp(dir.path / "generation.key").find("xi-secret-123") == std::string::npos);
#endif
    store.clear_key();
    CHECK_FALSE(store.has_key());
    CHECK_FALSE(store.key().has_value());
}

TEST_CASE("a damaged settings file reads as the fresh-install settings, not a crash") {
    TempDir dir;
    std::filesystem::create_directories(dir.path);
    std::ofstream(dir.path / "generation.settings", std::ios::binary)
        << "\x00\xff garbage without a separator\nenabled\n=lonely\n";
    app::GenerationFile store(dir.path);
    CHECK(store.read() == GenerationSettings{});
}

TEST_CASE("enabled with no model chosen reads as off") {
    TempDir dir;
    std::filesystem::create_directories(dir.path);
    std::ofstream(dir.path / "generation.settings") << "enabled=1\nmodel=\n";
    CHECK_FALSE(app::GenerationFile(dir.path).read().enabled);
}

TEST_CASE("a key file that cannot be unsealed yields no key and no crash") {
    TempDir dir;
    std::filesystem::create_directories(dir.path);
    std::ofstream(dir.path / "generation.key", std::ios::binary) << "not a sealed blob";
    app::GenerationFile store(dir.path);
#ifdef _WIN32
    CHECK_FALSE(store.key().has_value());
#endif
    store.clear_key();
    CHECK_FALSE(store.has_key());
}
