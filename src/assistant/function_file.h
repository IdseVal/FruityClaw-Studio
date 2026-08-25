// The Function file: what the Assistant is offered on a given turn.
// Contract: docs/specs/function-surface.md section 4.2 — a pure projection
// of the registry through the user's toggles. A switched-off Function is
// absent from `entries`: no stub, no marker, no error string (core document
// section 9.3). The audience and capability filters of the spec arrive with
// their first users (Sub-agents, the generation Functions); today the
// registry has neither.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "assistant/registry.h"

namespace assistant {

// The user's Function switches (core document section 3.10). A name absent
// from `disabled` is enabled. The switchboard (#17) writes it; this module
// only reads it.
struct Toggles {
    std::vector<std::string> disabled;

    bool enabled(const std::string& name) const;
};

struct ToolDefinition {
    std::string name;
    std::string description;
    ClosedSchema schema;
};

struct FunctionFile {
    std::vector<ToolDefinition> entries;  // registry declaration order
    std::uint64_t manifest_hash = 0;      // of the wire form; recorded on the turn
    std::size_t rework_count = 0;         // the switchboard's local-only predicate (O-17.3)
};

// Pure: no I/O, no clock, no globals. Same inputs, same bytes.
FunctionFile build(const Registry& registry, const Toggles& toggles);

// The wire form: strict tool definitions with closed schemas — `strict`,
// `additionalProperties: false`, every property required (core document
// Appendix B.4). Provider-neutral JSON; a provider adapter sends it as is or
// re-shapes it.
std::string to_wire_json(const FunctionFile& file);

}  // namespace assistant
