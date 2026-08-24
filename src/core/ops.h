// Operations and Deltas — the unit of change.
// Contract: docs/specs/history-contract.md sections 3.1 and 4, and
// docs/specs/project-data-model.md section 5.
//
// An Op mutates the Project and returns its exact inverse, built from the
// state actually observed at the moment of application — never from a
// before-image predicted earlier. A Delta is an ordered list of Ops; its
// inverse is the reversed list of the inverted Ops. Ops address entities by
// id (closures capture ids and look targets up at application time), never by
// index or path; positions inside ordered collections are payload.
#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/entities.h"

namespace core {

// Thrown by an Op whose target no longer resolves. apply_delta turns it into
// a rolled-back failure; it never escapes to a caller of ProjectHistory.
struct OpError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Op {
    // Mutates the Project and returns the inverse Op, or nullopt when
    // application observed no change (a Set whose value was already current).
    std::function<std::optional<Op>(Project&)> run;

    // Retained-cost hint for the History's memory bound.
    std::size_t bytes = 0;
};

enum class Origin { User, Assistant };

struct Delta {
    // User-facing, describes the effect in domain vocabulary. Never derived
    // from a Function's identity (history contract section 7.3).
    std::string label;
    Origin origin = Origin::User;
    std::vector<Op> ops;

    std::size_t size_hint() const {
        std::size_t total = 0;
        for (const auto& op : ops) total += op.bytes;
        return total;
    }
};

// Applies delta.ops in order and returns the inverse Delta (reversed list of
// returned inverses; no-op members dropped). An inverse with no ops means the
// whole application was a no-op. On failure, ops already applied are rolled
// back in reverse and the OpError is rethrown — the Project is left unchanged.
Delta apply_delta(Project& project, const Delta& delta);

// Builds a Set-kind Op over a field reached through `get`/`set` closures that
// address their entity by id and throw OpError when it is missing.
// `get(project)` returns the current value; `set(project, value)` writes it.
template <typename T, typename GetFn, typename SetFn>
Op make_set(GetFn get, SetFn set, T after, std::size_t bytes = sizeof(T)) {
    Op op;
    op.bytes = bytes;
    op.run = [get, set, after = std::move(after), bytes](Project& p) -> std::optional<Op> {
        T before = get(p);
        if (before == after) return std::nullopt;
        set(p, after);
        return make_set<T>(get, set, std::move(before), bytes);
    };
    return op;
}

}  // namespace core
