// Selectors: how a Function argument designates a Project entity.
// Contract: docs/specs/function-surface.md section 2.3. The model never sees
// an id and is never sent a candidate list; it echoes the user's own words
// and the Studio matches them against the real Project, locally. Ambiguity
// is answered by the user in the Assistant panel (obligation O-16.2).
#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/entities.h"

namespace assistant {

enum class EntityKind { Pattern, Track, Instrument, Sample };

struct Selector {
    enum class Kind {
        Focused,      // the entity the user currently has open or selected
        Ordinal,      // "track 3", "the second pattern" — 1-based
        Named,        // the user's own words, echoed back from their prompt
        LastCreated,  // the entity the previous Function in this turn created
    };
    Kind kind = Kind::Named;
    int ordinal = 0;
    std::string name;
};

struct Candidate {
    core::Id id;
    std::string name;
};

// What a Selector resolves against beyond the Project itself: the user's
// current focus and what this turn has created so far, per kind.
struct Focus {
    std::optional<core::Id> pattern;
    std::optional<core::Id> track;
    std::optional<core::Id> instrument;
    std::optional<core::Id> sample;

    std::optional<core::Id>& slot(EntityKind kind);
    const std::optional<core::Id>& slot(EntityKind kind) const;
};

struct NotFound {
    std::string message;  // in domain vocabulary, shown to the user
};
struct Ambiguous {
    std::vector<Candidate> candidates;
};
using Resolution = std::variant<core::Id, NotFound, Ambiguous>;

// Resolves `selector` to one entity of `kind`. Pure: Project in, answer out.
Resolution resolve(const Selector& selector, EntityKind kind, const core::Project& project,
                   const Focus& focused, const Focus& last_created);

// The user-facing noun for a kind, in section 5 vocabulary.
const char* kind_name(EntityKind kind);

}  // namespace assistant
