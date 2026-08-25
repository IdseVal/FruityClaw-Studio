#include "assistant/function_file.h"

namespace assistant {
namespace {

bool any_rework_recorded_off(const FunctionToggles& toggles) {
    for (const FunctionDescriptor& function : registry()) {
        if (function.function_class == FunctionClass::Rework &&
            toggles.state(function.name) == false) {
            return true;
        }
    }
    return false;
}

// FNV-1a over the entry names in order. The hash identifies a manifest for
// caching and for recording on a turn; it is not a security boundary.
std::uint64_t hash_names(const std::vector<const FunctionDescriptor*>& entries) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const FunctionDescriptor* entry : entries) {
        for (char c : entry->name) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ull;
        }
        hash ^= '\n';
        hash *= 1099511628211ull;
    }
    return hash;
}

}  // namespace

bool toggle_allows(const FunctionDescriptor& function, const FunctionToggles& toggles) {
    if (std::optional<bool> recorded = toggles.state(function.name)) return *recorded;
    if (function.function_class == FunctionClass::Rework) return !any_rework_recorded_off(toggles);
    return true;
}

FunctionFile build(std::span<const FunctionDescriptor> registry, const FunctionToggles& toggles,
                   const Capabilities& capabilities) {
    FunctionFile file;
    for (const FunctionDescriptor& function : registry) {
        if (function.requires_music_generation && !capabilities.music_generation_enabled) continue;
        if (!toggle_allows(function, toggles)) continue;
        file.entries.push_back(&function);
        if (function.function_class == FunctionClass::Rework) ++file.rework_count;
    }
    file.manifest_hash = hash_names(file.entries);
    return file;
}

}  // namespace assistant
