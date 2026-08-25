#include "assistant/function_file.h"

#include <algorithm>

namespace assistant {
namespace {

std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out + "\"";
}

std::string type_of(const Property& property) {
    switch (property.kind) {
        case Property::Kind::Integer: return "integer";
        case Property::Kind::String: return "string";
        case Property::Kind::Boolean: return "boolean";
        case Property::Kind::Enum: return "string";
        case Property::Kind::Selector: return "object";
        case Property::Kind::IntegerArray: return "array";
    }
    return "string";
}

// A Selector on the wire: a closed object naming the entity the way the user
// did. Not an id — the model has none (function-surface spec section 2.3).
std::string selector_json(const Property& property) {
    std::string kind = kind_name(property.entity);
    return "{\"type\":\"object\",\"properties\":{"
           "\"by\":{\"type\":\"string\",\"enum\":[\"focused\",\"ordinal\",\"named\","
           "\"last_created\"],\"description\":\"How the user referred to the " + kind +
           ": the one they have selected, its position (1-based), the name they used, or the "
           "one just created in this conversation.\"},"
           "\"ordinal\":{\"type\":[\"integer\",\"null\"]},"
           "\"name\":{\"type\":[\"string\",\"null\"],\"description\":\"The " + kind +
           " name exactly as the user said it.\"}},"
           "\"required\":[\"by\",\"ordinal\",\"name\"],\"additionalProperties\":false}";
}

std::string property_json(const Property& property) {
    if (property.kind == Property::Kind::Selector) {
        std::string out = selector_json(property);
        if (!property.nullable) return out;
        return "{\"anyOf\":[" + out + ",{\"type\":\"null\"}]," +
               "\"description\":" + quoted(property.description) + "}";
    }
    std::string type = type_of(property);
    std::string out = property.nullable ? "{\"type\":[\"" + type + "\",\"null\"]"
                                        : "{\"type\":\"" + type + "\"";
    out += ",\"description\":" + quoted(property.description);
    if (property.kind == Property::Kind::Enum) {
        out += ",\"enum\":[";
        for (std::size_t i = 0; i < property.options.size(); ++i) {
            if (i) out += ",";
            out += quoted(property.options[i]);
        }
        out += "]";
    }
    if (property.kind == Property::Kind::IntegerArray) out += ",\"items\":{\"type\":\"integer\"}";
    return out + "}";
}

std::string schema_json(const ClosedSchema& schema) {
    std::string out = "{\"type\":\"object\",\"properties\":{";
    std::string required = "[";
    for (std::size_t i = 0; i < schema.properties.size(); ++i) {
        const Property& property = schema.properties[i];
        if (i) {
            out += ",";
            required += ",";
        }
        out += quoted(property.name) + ":" + property_json(property);
        required += quoted(property.name);
    }
    return out + "},\"required\":" + required + "],\"additionalProperties\":false}";
}

// FNV-1a: stable across platforms and sessions, which is what makes the
// hash usable as a cache key and a turn record.
std::uint64_t fnv1a(const std::string& bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : bytes) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

}  // namespace

bool Toggles::enabled(const std::string& name) const {
    return std::find(disabled.begin(), disabled.end(), name) == disabled.end();
}

FunctionFile build(const Registry& registry, const Toggles& toggles) {
    FunctionFile file;
    for (const FunctionDescriptor& descriptor : registry) {
        if (!toggles.enabled(descriptor.name)) continue;  // absent, not refused
        file.entries.push_back({descriptor.name, descriptor.instruction, descriptor.schema});
    }
    file.manifest_hash = fnv1a(to_wire_json(file));
    return file;
}

std::string to_wire_json(const FunctionFile& file) {
    std::string out = "[";
    for (std::size_t i = 0; i < file.entries.size(); ++i) {
        const ToolDefinition& entry = file.entries[i];
        if (i) out += ",";
        out += "{\"name\":" + quoted(entry.name) + ",\"description\":" +
               quoted(entry.description) + ",\"strict\":true,\"input_schema\":" +
               schema_json(entry.schema) + "}";
    }
    return out + "]";
}

}  // namespace assistant
