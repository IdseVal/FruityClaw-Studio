// The Assistant key store (issue #18): kept for the user, readable back,
// never plaintext at rest on Windows, and the first-open offer remembered.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "assistant/key_store.h"

namespace {

// A fresh directory per test, under the system temp, removed at the end.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("assistant-key-test-" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::vector<char> bytes_of(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

TEST_CASE("a fresh store has no key and no offer made") {
    TempDir dir;
    assistant::FileKeyStore store(dir.path);
    CHECK_FALSE(store.has_key());
    CHECK_FALSE(store.key().has_value());
    CHECK_FALSE(store.offer_made());
}

TEST_CASE("a stored key reads back exactly, from a second store over the same directory") {
    TempDir dir;
    const std::string key = "sk-test-0123456789-ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    {
        assistant::FileKeyStore store(dir.path);
        REQUIRE(store.store_key(key));
        CHECK(store.has_key());
    }
    assistant::FileKeyStore reopened(dir.path);
    CHECK(reopened.has_key());
    REQUIRE(reopened.key().has_value());
    CHECK(*reopened.key() == key);
    CHECK_FALSE(reopened.offer_made());  // storing is not the same as being offered
}

TEST_CASE("the key file never holds the key in plain text on Windows") {
    TempDir dir;
    const std::string key = "sk-plaintext-canary-9f8e7d6c";
    assistant::FileKeyStore store(dir.path);
    REQUIRE(store.store_key(key));
    std::vector<char> on_disk = bytes_of(dir.path / "assistant.key");
    REQUIRE_FALSE(on_disk.empty());
    bool contains_plain =
        std::search(on_disk.begin(), on_disk.end(), key.begin(), key.end()) != on_disk.end();
#ifdef _WIN32
    CHECK_FALSE(contains_plain);
#else
    // Elsewhere the file is owner-only rather than encrypted.
    CHECK(contains_plain);
    auto perms = std::filesystem::status(dir.path / "assistant.key").permissions();
    CHECK((perms & std::filesystem::perms::group_all) == std::filesystem::perms::none);
    CHECK((perms & std::filesystem::perms::others_all) == std::filesystem::perms::none);
#endif
}

TEST_CASE("an empty key is refused and leaves the store unchanged") {
    TempDir dir;
    assistant::FileKeyStore store(dir.path);
    CHECK_FALSE(store.store_key(""));
    CHECK_FALSE(store.has_key());
}

TEST_CASE("storing again replaces, clearing removes") {
    TempDir dir;
    assistant::FileKeyStore store(dir.path);
    REQUIRE(store.store_key("first"));
    REQUIRE(store.store_key("second"));
    CHECK(*store.key() == "second");
    store.clear_key();
    CHECK_FALSE(store.has_key());
    CHECK_FALSE(store.key().has_value());
    CHECK_FALSE(std::filesystem::exists(dir.path / "assistant.key.tmp"));
}

TEST_CASE("the offer is remembered across stores without a key") {
    TempDir dir;
    {
        assistant::FileKeyStore store(dir.path);
        store.record_offer();
    }
    assistant::FileKeyStore reopened(dir.path);
    CHECK(reopened.offer_made());
    CHECK_FALSE(reopened.has_key());
}

TEST_CASE("the store creates its directory on first write") {
    TempDir dir;
    assistant::FileKeyStore store(dir.path / "nested" / "deeper");
    REQUIRE(store.store_key("k"));
    CHECK(store.has_key());
}

TEST_CASE("the default directory is a real per-user path") {
    std::filesystem::path dir = assistant::FileKeyStore::default_directory();
    CHECK(dir.is_absolute());
    CHECK_FALSE(dir.filename().empty());
}

TEST_CASE("a key file that cannot be unsealed reads as no key, and is still replaceable") {
    TempDir dir;
    {
        std::ofstream out(dir.path / "assistant.key", std::ios::binary);
        out << "not a sealed blob";
    }
    assistant::FileKeyStore store(dir.path);
    CHECK(store.has_key());  // something is there; the transport decides what to do
#ifdef _WIN32
    CHECK_FALSE(store.key().has_value());  // DPAPI refuses foreign bytes
#endif
    REQUIRE(store.store_key("sk-fresh"));
    CHECK(*store.key() == "sk-fresh");
}

TEST_CASE("an empty key file reads as no key") {
    TempDir dir;
    { std::ofstream out(dir.path / "assistant.key", std::ios::binary); }
    assistant::FileKeyStore store(dir.path);
    CHECK_FALSE(store.key().has_value());
}

TEST_CASE("a directory that cannot be created makes store_key say so") {
    TempDir dir;
    { std::ofstream out(dir.path / "blocker"); out << "a file, not a directory"; }
    assistant::FileKeyStore store(dir.path / "blocker" / "inside");
    CHECK_FALSE(store.store_key("sk-nowhere"));
    CHECK_FALSE(store.has_key());
}

TEST_CASE("recording the offer twice is harmless") {
    TempDir dir;
    assistant::FileKeyStore store(dir.path);
    store.record_offer();
    store.record_offer();
    CHECK(store.offer_made());
    CHECK_FALSE(std::filesystem::exists(dir.path / "assistant.key.offered.tmp"));
}

TEST_CASE("a key with surrounding whitespace and unicode survives the round trip") {
    TempDir dir;
    assistant::FileKeyStore store(dir.path);
    const std::string key = " sk-\xC3\xA9-\xE2\x9C\x93-tail\n";
    REQUIRE(store.store_key(key));
    CHECK(*store.key() == key);  // the store keeps bytes; trimming is the dialog's job
}
