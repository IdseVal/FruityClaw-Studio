#include "assistant/toggles.h"

#include <string>

namespace assistant {
namespace {

// Catalogue names are snake_case ASCII (spec section 2.1); anything else in
// a persisted file is not a Function name.
bool is_catalogue_name(std::string_view name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || c == '_')) return false;
    return true;
}

}  // namespace

std::optional<bool> FunctionToggles::state(std::string_view name) const {
    auto it = states_.find(std::string(name));
    if (it == states_.end()) return std::nullopt;
    return it->second;
}

void FunctionToggles::set_enabled(std::string_view name, bool enabled) {
    if (state(name) == enabled) return;
    states_[std::string(name)] = enabled;
    ++version_;
}

std::string FunctionToggles::to_text() const {
    std::string text;
    for (const auto& [name, enabled] : states_) {
        text += name;
        text += enabled ? "=on\n" : "=off\n";
    }
    return text;
}

FunctionToggles FunctionToggles::from_text(std::string_view text) {
    FunctionToggles toggles;
    while (!text.empty()) {
        std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        std::string_view name = line.substr(0, eq);
        std::string_view value = line.substr(eq + 1);
        if (!is_catalogue_name(name)) continue;
        if (value == "on") {
            toggles.states_[std::string(name)] = true;
        } else if (value == "off") {
            toggles.states_[std::string(name)] = false;
        }
    }
    return toggles;
}

}  // namespace assistant
