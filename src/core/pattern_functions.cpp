#include "core/pattern_functions.h"

#include <algorithm>
#include <utility>

namespace core::functions {
namespace {

constexpr Ticks kStep = kPpq / 4;  // a sixteenth
constexpr int kMaxPatternBars = 64;

Pattern& pattern_or_throw(Project& p, Id pattern) {
    if (Pattern* pat = p.patterns.find(pattern)) return *pat;
    throw OpError("Pattern not found: " + to_string(pattern));
}

Part& part_or_throw(Project& p, Id pattern, Id part) {
    Pattern& pat = pattern_or_throw(p, pattern);
    for (Part& candidate : pat.parts)
        if (candidate.id == part) return candidate;
    throw OpError("Part not found: " + to_string(part));
}

const Part* find_part(const Pattern& pat, Id part) {
    for (const Part& candidate : pat.parts)
        if (candidate.id == part) return &candidate;
    return nullptr;
}

std::size_t entity_bytes(const Pattern& pat) {
    std::size_t bytes = sizeof(Pattern) + pat.name.size();
    for (const Part& part : pat.parts) bytes += sizeof(Part) + part.events.size() * sizeof(Event);
    return bytes;
}

Op insert_pattern_op(Pattern pattern, std::size_t index);

Op remove_pattern_op(Id pattern, std::size_t bytes) {
    Op op;
    op.bytes = bytes;
    op.run = [pattern](Project& p) -> std::optional<Op> {
        auto& items = p.patterns.items;
        for (auto it = items.begin(); it != items.end(); ++it) {
            if (it->id == pattern) {
                std::size_t index = static_cast<std::size_t>(it - items.begin());
                Pattern removed = std::move(*it);
                items.erase(it);
                return insert_pattern_op(std::move(removed), index);
            }
        }
        throw OpError("Pattern not found: " + to_string(pattern));
    };
    return op;
}

Op insert_pattern_op(Pattern pattern, std::size_t index) {
    Op op;
    op.bytes = entity_bytes(pattern);
    op.run = [pattern = std::move(pattern), index](Project& p) -> std::optional<Op> {
        auto& items = p.patterns.items;
        std::size_t at = std::min(index, items.size());
        items.insert(items.begin() + static_cast<std::ptrdiff_t>(at), pattern);
        return remove_pattern_op(pattern.id, entity_bytes(pattern));
    };
    return op;
}

}  // namespace

Expected<CreatedDelta> create_pattern(const Project& project, std::string name, int length_bars,
                                      Origin origin) {
    if (length_bars < 1 || length_bars > kMaxPatternBars)
        return Expected<CreatedDelta>::failure("Pattern length must be 1 to 64 bars");

    Pattern pattern;
    pattern.id = new_id();
    pattern.name = std::move(name);
    pattern.length = static_cast<Ticks>(length_bars) * project.time_signature.first * kPpq;

    Delta delta;
    delta.label = "Add Pattern '" + pattern.name + "'";
    delta.origin = origin;
    Id id = pattern.id;
    delta.ops.push_back(insert_pattern_op(std::move(pattern), project.patterns.items.size()));
    return Expected<CreatedDelta>::success({std::move(delta), id});
}

Expected<Delta> set_part_steps(const Project& project, Id pattern, Id part,
                               std::vector<int> steps, Origin origin) {
    const Pattern* pat = project.patterns.find(pattern);
    if (!pat) return Expected<Delta>::failure("No such Pattern");
    const Part* lane = find_part(*pat, part);
    if (!lane) return Expected<Delta>::failure("That Instrument is not in '" + pat->name + "'");
    int step_count = static_cast<int>(pat->length / kStep);
    for (int step : steps) {
        if (step < 0 || step >= step_count)
            return Expected<Delta>::failure("'" + pat->name + "' has " +
                                            std::to_string(step_count) + " steps");
    }

    // Keep the Event of every step that stays on, so its velocity survives.
    std::sort(steps.begin(), steps.end());
    steps.erase(std::unique(steps.begin(), steps.end()), steps.end());
    std::vector<Event> events;
    for (int step : steps) {
        Ticks start = step * kStep;
        auto existing = std::find_if(lane->events.begin(), lane->events.end(),
                                     [start](const Event& e) { return e.start == start; });
        if (existing != lane->events.end()) {
            events.push_back(*existing);
        } else {
            events.push_back(Event{new_id(), start, kPpq / 8, 60, 100});
        }
    }

    const Instrument* inst = project.instruments.find(lane->instrument);
    Delta delta;
    delta.label = "Set steps of '" + (inst ? inst->name : std::string("lane")) + "' in '" +
                  pat->name + "'";
    delta.origin = origin;
    delta.ops.push_back(make_set<std::vector<Event>>(
        [pattern, part](Project& p) { return part_or_throw(p, pattern, part).events; },
        [pattern, part](Project& p, const std::vector<Event>& v) {
            part_or_throw(p, pattern, part).events = v;
        },
        std::move(events), std::max(events.size(), lane->events.size()) * sizeof(Event)));
    return Expected<Delta>::success(std::move(delta));
}

Expected<Delta> set_part_instrument(const Project& project, Id pattern, Id part, Id instrument,
                                    Origin origin) {
    const Pattern* pat = project.patterns.find(pattern);
    if (!pat) return Expected<Delta>::failure("No such Pattern");
    if (!find_part(*pat, part))
        return Expected<Delta>::failure("That Instrument is not in '" + pat->name + "'");
    const Instrument* inst = project.instruments.find(instrument);
    if (!inst) return Expected<Delta>::failure("No such Instrument");

    Delta delta;
    delta.label = "Switch a lane of '" + pat->name + "' to '" + inst->name + "'";
    delta.origin = origin;
    delta.ops.push_back(make_set<Id>(
        [pattern, part](Project& p) { return part_or_throw(p, pattern, part).instrument; },
        [pattern, part](Project& p, const Id& v) {
            part_or_throw(p, pattern, part).instrument = v;
        },
        instrument));
    return Expected<Delta>::success(std::move(delta));
}

}  // namespace core::functions
