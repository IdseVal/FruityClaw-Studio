// Opaque entity identity.
// Contract: docs/specs/project-data-model.md section 3.0 — globally unique,
// stable, never reused, carries no meaning and no branding (invariant I10).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace core {

struct Id {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;

    bool operator==(const Id&) const = default;

    // True for a default-constructed Id, which addresses nothing.
    bool is_null() const { return hi == 0 && lo == 0; }
};

// Allocates a fresh random 128-bit Id. Thread-safe.
Id new_id();

// Hex form for logs and test failure messages only. Never an address format.
std::string to_string(const Id& id);

}  // namespace core

template <>
struct std::hash<core::Id> {
    std::size_t operator()(const core::Id& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.hi) ^ (std::hash<std::uint64_t>{}(id.lo) << 1);
    }
};
