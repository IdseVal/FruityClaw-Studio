// The Function toggle store: the user's per-Function on/off choices
// (core document 3.10). A Studio setting, outside every Function's reach
// (spec section 1) — nothing in this module is handed to a Function.
//
// The store records only what the user has set; a Function it has never
// heard of has no entry. That distinction is what section 4.3's default
// rule needs, so the store keeps explicit states rather than a set of
// disabled names, and leaves the defaulting to the builder.
//
// No I/O here. The text form below is how the composition root persists
// the store between sessions; where the file lives is its decision.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace assistant {

class FunctionToggles {
public:
    // The recorded state for a Function; nullopt when the user never set it.
    std::optional<bool> state(std::string_view name) const;

    // Records the user's choice and bumps the version, which is what
    // invalidates a cached Function file (spec section 4.4). Setting the
    // same value again is not a change and does not bump.
    void set_enabled(std::string_view name, bool enabled);

    std::uint64_t version() const { return version_; }

    // One line per recorded Function, `name=on` or `name=off`, sorted by
    // name so equal stores serialise to equal text.
    std::string to_text() const;

    // Reads the to_text form. Lines that do not parse are skipped, and a
    // name is accepted only in the catalogue's own alphabet — the file is
    // user-editable and must not be able to inject anything.
    static FunctionToggles from_text(std::string_view text);

private:
    std::map<std::string, bool> states_;
    std::uint64_t version_ = 0;
};

}  // namespace assistant
