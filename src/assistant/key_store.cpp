#include "assistant/key_store.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <wincrypt.h>
#endif

namespace assistant {
namespace {

// At rest the key is wrapped for the logged-in user: DPAPI on Windows, which
// no other account (and no copy of the file) can unwrap. Elsewhere the file
// is owner-only, the same protection ~/.ssh and ~/.netrc rely on.
std::vector<char> seal(const std::string& plain) {
#ifdef _WIN32
    DATA_BLOB in{static_cast<DWORD>(plain.size()),
                 reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"assistant key", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }
    std::vector<char> sealed(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return sealed;
#else
    return std::vector<char>(plain.begin(), plain.end());
#endif
}

std::optional<std::string> unseal(const std::vector<char>& sealed) {
#ifdef _WIN32
    DATA_BLOB in{static_cast<DWORD>(sealed.size()),
                 reinterpret_cast<BYTE*>(const_cast<char*>(sealed.data()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return std::nullopt;
    }
    std::string plain(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return plain;
#else
    return std::string(sealed.begin(), sealed.end());
#endif
}

std::optional<std::vector<char>> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<char>(std::istreambuf_iterator<char>(in), {});
}

// Written to a sibling first and renamed over, so a crash mid-write cannot
// leave a half key. The permissions are narrowed before the bytes land.
bool write_file(const std::filesystem::path& path, const std::vector<char>& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path staging = path;
    staging += ".tmp";
    {
        std::ofstream out(staging, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        std::filesystem::permissions(
            staging, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace, ec);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) return false;
    }
    std::filesystem::rename(staging, path, ec);
    return !ec;
}

const char* env(const char* name) {
    const char* value = std::getenv(name);
    return value && *value ? value : nullptr;
}

}  // namespace

std::filesystem::path FileKeyStore::default_directory() {
#if defined(_WIN32)
    const char* base = env("APPDATA");
    return std::filesystem::path(base ? base : ".") / FCS_APP_ID_STR;
#elif defined(__APPLE__)
    const char* home = env("HOME");
    return std::filesystem::path(home ? home : ".") / "Library" / "Application Support" /
           FCS_APP_ID_STR;
#else
    if (const char* xdg = env("XDG_CONFIG_HOME")) {
        return std::filesystem::path(xdg) / FCS_APP_ID_STR;
    }
    const char* home = env("HOME");
    return std::filesystem::path(home ? home : ".") / ".config" / FCS_APP_ID_STR;
#endif
}

FileKeyStore::FileKeyStore(std::filesystem::path directory)
    : directory_(std::move(directory)) {}

std::filesystem::path FileKeyStore::key_path() const { return directory_ / "assistant.key"; }

std::filesystem::path FileKeyStore::offer_path() const {
    return directory_ / "assistant.key.offered";
}

bool FileKeyStore::has_key() const {
    std::error_code ec;
    return std::filesystem::exists(key_path(), ec);
}

std::optional<std::string> FileKeyStore::key() const {
    std::optional<std::vector<char>> sealed = read_file(key_path());
    if (!sealed || sealed->empty()) return std::nullopt;
    return unseal(*sealed);
}

bool FileKeyStore::store_key(const std::string& key) {
    if (key.empty()) return false;
    std::vector<char> sealed = seal(key);
    if (sealed.empty()) return false;
    return write_file(key_path(), sealed);
}

void FileKeyStore::clear_key() {
    std::error_code ec;
    std::filesystem::remove(key_path(), ec);
}

bool FileKeyStore::offer_made() const {
    std::error_code ec;
    return std::filesystem::exists(offer_path(), ec);
}

void FileKeyStore::record_offer() { write_file(offer_path(), {}); }

}  // namespace assistant
