#include "assistant/schema.h"

namespace assistant {
namespace {

std::optional<std::string> check(const Property& property, const ArgValue& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        if (property.nullable) return std::nullopt;
        return "'" + property.name + "' is missing";
    }
    switch (property.kind) {
        case Property::Kind::Integer: {
            const auto* v = std::get_if<long long>(&value);
            if (!v) return "'" + property.name + "' is not a whole number";
            if (*v < property.min || *v > property.max)
                return "'" + property.name + "' must be " + std::to_string(property.min) +
                       " to " + std::to_string(property.max);
            return std::nullopt;
        }
        case Property::Kind::IntegerArray: {
            const auto* v = std::get_if<std::vector<long long>>(&value);
            if (!v) return "'" + property.name + "' is not a list of whole numbers";
            for (long long item : *v) {
                if (item < property.min || item > property.max)
                    return "'" + property.name + "' entries must be " +
                           std::to_string(property.min) + " to " + std::to_string(property.max);
            }
            return std::nullopt;
        }
        case Property::Kind::String:
            if (!std::holds_alternative<std::string>(value))
                return "'" + property.name + "' is not text";
            return std::nullopt;
        case Property::Kind::Boolean:
            if (!std::holds_alternative<bool>(value)) return "'" + property.name + "' is not yes/no";
            return std::nullopt;
        case Property::Kind::Enum: {
            const auto* v = std::get_if<std::string>(&value);
            if (!v) return "'" + property.name + "' is not text";
            for (const std::string& option : property.options)
                if (option == *v) return std::nullopt;
            return "'" + property.name + "' is not one of the allowed values";
        }
        case Property::Kind::Selector: {
            const auto* v = std::get_if<Selector>(&value);
            if (!v) return "'" + property.name + "' does not name anything";
            if (v->kind == Selector::Kind::Ordinal && v->ordinal < 1)
                return "'" + property.name + "' ordinal must be 1 or more";
            if (v->kind == Selector::Kind::Named && v->name.empty())
                return "'" + property.name + "' names nothing";
            return std::nullopt;
        }
    }
    return "'" + property.name + "' has an unknown kind";
}

}  // namespace

std::optional<std::string> validate(const ClosedSchema& schema, const Args& args) {
    for (const Property& property : schema.properties) {
        auto it = args.find(property.name);
        if (it == args.end()) return "'" + property.name + "' is missing";
        if (auto problem = check(property, it->second)) return problem;
    }
    // additionalProperties: false — an argument the schema does not name is
    // a malformed reply, not an extension point.
    for (const auto& [name, value] : args) {
        bool known = false;
        for (const Property& property : schema.properties) known |= property.name == name;
        if (!known) return "unexpected argument '" + name + "'";
    }
    return std::nullopt;
}

}  // namespace assistant
