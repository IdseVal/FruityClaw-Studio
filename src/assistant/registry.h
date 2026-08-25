// The Function registry: the ONE list of what the Assistant may do.
// Contract: docs/specs/function-surface.md section 4.1 and ADR-006 D6. The
// Function file, the dispatch table and the switchboard (#17) are all
// projections of this list; there is no second copy anywhere.
//
// Every Function is handed the Project — the musical content and nothing
// else. The Project type holds no settings, no credentials, no toggle store,
// no view state and no file path (project-data-model spec, section 2), so
// there is nothing for a Function to reach through (ADR-006 D2).
//
// Each implementation returns a Delta; it never writes. ProjectHistory is
// the only writer, so every Assistant action is undoable by construction.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "assistant/schema.h"
#include "core/arrangement_functions.h"

namespace assistant {

// A Selector argument, resolved locally before dispatch. The implementation
// sees ids; the model never does.
using ResolvedArgs = std::map<std::string, std::variant<std::monostate, long long, bool,
                                                        std::string, core::Id,
                                                        std::vector<long long>>>;

struct FunctionResult {
    core::Delta delta;
    // Set by a creation, so a later Function in the same turn can address it
    // with LastCreated.
    std::optional<core::Id> created;
    std::optional<EntityKind> created_kind;
};

// A Directive Function: computes a Delta from its arguments and the Project.
// It has no member that could produce Project content for transmission
// (function-surface spec section 3, invariant I6).
using DirectiveFn = std::function<core::functions::Expected<FunctionResult>(
    const ResolvedArgs&, const core::Project&)>;

struct FunctionDescriptor {
    std::string name;         // verb_object, from the closed verb set
    std::string instruction;  // what the model reads: the section 8.4 appended instruction
    std::string effect;       // the single thing it does, for the switchboard
    ClosedSchema schema;
    DirectiveFn impl;
};

using Registry = std::vector<FunctionDescriptor>;

// The catalogue, in declaration order. Built once; immutable.
const Registry& registry();

// Lookup by name; nullptr when absent.
const FunctionDescriptor* find(const Registry& registry, const std::string& name);

}  // namespace assistant
