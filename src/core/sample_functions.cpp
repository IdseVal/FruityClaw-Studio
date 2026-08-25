#include "core/sample_functions.h"

#include <algorithm>
#include <utility>

namespace core::functions {
namespace {

Pattern& pattern_or_throw(Project& p, Id pattern) {
    if (Pattern* pat = p.patterns.find(pattern)) return *pat;
    throw OpError("Pattern not found: " + to_string(pattern));
}

std::size_t entity_bytes(const Instrument& i) {
    return sizeof(Instrument) + i.name.size() + i.chain.size() * sizeof(Effect);
}

std::size_t entity_bytes(const Part& part) {
    return sizeof(Part) + part.events.size() * sizeof(Event);
}

// Insert/Remove pairs as in arrangement_functions.cpp: a Remove bakes the
// observed index into the Insert it returns so undo restores order exactly.

Op insert_instrument_op(Instrument instrument, std::size_t index);

Op remove_instrument_op(Id instrument, std::size_t bytes) {
    Op op;
    op.bytes = bytes;
    op.run = [instrument](Project& p) -> std::optional<Op> {
        auto& items = p.instruments.items;
        for (auto it = items.begin(); it != items.end(); ++it) {
            if (it->id == instrument) {
                std::size_t index = static_cast<std::size_t>(it - items.begin());
                Instrument removed = std::move(*it);
                items.erase(it);
                return insert_instrument_op(std::move(removed), index);
            }
        }
        throw OpError("Instrument not found: " + to_string(instrument));
    };
    return op;
}

Op insert_instrument_op(Instrument instrument, std::size_t index) {
    Op op;
    op.bytes = entity_bytes(instrument);
    op.run = [instrument = std::move(instrument), index](Project& p) -> std::optional<Op> {
        auto& items = p.instruments.items;
        std::size_t at = std::min(index, items.size());
        items.insert(items.begin() + static_cast<std::ptrdiff_t>(at), instrument);
        return remove_instrument_op(instrument.id, entity_bytes(instrument));
    };
    return op;
}

Op insert_part_op(Id pattern, Part part, std::size_t index);

Op remove_part_op(Id pattern, Id part, std::size_t bytes) {
    Op op;
    op.bytes = bytes;
    op.run = [pattern, part](Project& p) -> std::optional<Op> {
        Pattern& pat = pattern_or_throw(p, pattern);
        for (auto it = pat.parts.begin(); it != pat.parts.end(); ++it) {
            if (it->id == part) {
                std::size_t index = static_cast<std::size_t>(it - pat.parts.begin());
                Part removed = std::move(*it);
                pat.parts.erase(it);
                return insert_part_op(pattern, std::move(removed), index);
            }
        }
        throw OpError("Part not found: " + to_string(part));
    };
    return op;
}

Op insert_part_op(Id pattern, Part part, std::size_t index) {
    Op op;
    op.bytes = entity_bytes(part);
    op.run = [pattern, part = std::move(part), index](Project& p) -> std::optional<Op> {
        Pattern& pat = pattern_or_throw(p, pattern);
        std::size_t at = std::min(index, pat.parts.size());
        pat.parts.insert(pat.parts.begin() + static_cast<std::ptrdiff_t>(at), part);
        return remove_part_op(pattern, part.id, entity_bytes(part));
    };
    return op;
}

}  // namespace

Expected<CreatedDelta> create_instrument(const Project& project, std::string name, Id sample,
                                         SamplerMode mode, Origin origin) {
    if (!project.samples.find(sample)) return Expected<CreatedDelta>::failure("No such Sample");

    Instrument instrument;
    instrument.id = new_id();
    instrument.name = std::move(name);
    instrument.params.sample = sample;
    instrument.params.mode = mode;

    Delta delta;
    delta.label = "Add Instrument '" + instrument.name + "'";
    delta.origin = origin;
    Id id = instrument.id;
    delta.ops.push_back(
        insert_instrument_op(std::move(instrument), project.instruments.items.size()));
    return Expected<CreatedDelta>::success({std::move(delta), id});
}

Expected<CreatedDelta> add_part(const Project& project, Id pattern, Id instrument,
                                Origin origin) {
    const Pattern* pat = project.patterns.find(pattern);
    if (!pat) return Expected<CreatedDelta>::failure("No such Pattern");
    const Instrument* inst = project.instruments.find(instrument);
    if (!inst) return Expected<CreatedDelta>::failure("No such Instrument");
    for (const Part& part : pat->parts) {
        if (part.instrument == instrument)
            return Expected<CreatedDelta>::failure("'" + inst->name + "' is already in '" +
                                                   pat->name + "'");
    }

    Part part;
    part.id = new_id();
    part.instrument = instrument;

    Delta delta;
    delta.label = "Add '" + inst->name + "' to '" + pat->name + "'";
    delta.origin = origin;
    Id id = part.id;
    delta.ops.push_back(insert_part_op(pattern, std::move(part), pat->parts.size()));
    return Expected<CreatedDelta>::success({std::move(delta), id});
}

}  // namespace core::functions
