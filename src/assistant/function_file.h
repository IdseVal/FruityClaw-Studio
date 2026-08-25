// The Function file builder (docs/specs/function-surface.md section 4.2):
// the pure projection of the registry through the Studio's capabilities
// and the user's toggles into the manifest the Assistant is offered.
//
// Removal, not refusal (core document 9.3): a Function that fails a filter
// is absent from `entries`. Nothing records that it existed.
//
// Pure — no I/O, no clock, no globals — so the same inputs give the same
// file, and the switchboard's banner can be computed from exactly the
// artefact that would go on the wire (obligation O-17.3). The provider
// dialect that renders `entries` into tool definitions is issue #16's.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "assistant/registry.h"
#include "assistant/toggles.h"

namespace assistant {

// What the Studio has configured that a Function may require. Music
// generation is off until the user enables it (core document 6.4); the
// generation settings page (issue #20) is what turns this on.
struct Capabilities {
    bool music_generation_enabled = false;
};

struct FunctionFile {
    std::vector<const FunctionDescriptor*> entries;  // registry order
    std::uint64_t manifest_hash = 0;                 // of the entry names
    std::size_t rework_count = 0;

    // "No Project content can leave this machine" — true exactly when the
    // file the Assistant is offered holds no Rework Function (spec section 7).
    bool local_only() const { return rework_count == 0; }
};

// Whether the toggle store offers a Function, applying the section 4.3
// defaults: an unset Function is on, except that an unset Rework Function
// is off whenever any Rework Function is recorded off — an update must not
// silently re-open a privacy floor the user closed.
bool toggle_allows(const FunctionDescriptor& function, const FunctionToggles& toggles);

// Filters in order: capabilities, then toggles.
FunctionFile build(std::span<const FunctionDescriptor> registry, const FunctionToggles& toggles,
                   const Capabilities& capabilities);

}  // namespace assistant
