// Closed argument schemas and the argument values that satisfy them.
// Contract: docs/specs/function-surface.md section 2.2 — every property is
// required, no additional properties, enumerated domains closed at schema
// level. The one argument shape that names an entity is a Selector
// (section 2.3), never an id.
//
// Bounds are validated locally at dispatch (`validate`) because strict tool
// use guarantees the shape of an argument but not its range (issue #4
// research): a schema-valid `velocity: 9000` must still be refused here.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "assistant/selector.h"

namespace assistant {

struct Property {
    enum class Kind { Integer, String, Boolean, Enum, Selector, IntegerArray };

    std::string name;
    Kind kind = Kind::String;
    std::string description;

    // Integer and IntegerArray: inclusive bounds, checked at dispatch.
    long long min = 0;
    long long max = 0;

    // Enum: the closed set of accepted strings.
    std::vector<std::string> options;

    // Selector: which collection it addresses.
    EntityKind entity = EntityKind::Pattern;

    // A nullable property may be sent as null; it is still required
    // (optionality is a nullable union, never an absent key).
    bool nullable = false;
};

struct ClosedSchema {
    std::vector<Property> properties;
};

// A null argument is std::monostate.
using ArgValue = std::variant<std::monostate, long long, bool, std::string, Selector,
                              std::vector<long long>>;
using Args = std::map<std::string, ArgValue>;

// Returns a description of the first violation, or nullopt when `args` has
// exactly the schema's properties with values in their domains.
std::optional<std::string> validate(const ClosedSchema& schema, const Args& args);

}  // namespace assistant
