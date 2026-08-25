#include "core/pattern_functions.h"

#include <algorithm>
#include <utility>

namespace core::functions {
namespace {

constexpr int kMaxPatternBars = 64;
constexpr Ticks kStepDuration = kPpq / 8;  // ignored by OneShot; a thirty-second for Sustain
constexpr Velocity kDefaultVelocity = 100;

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

bool on_grid(const Event& e) { return e.start % kStepTicks == 0; }

int step_count(const Pattern& pat) { return static_cast<int>(pat.length / kStepTicks); }

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

// The one Op every step function reduces to: replace the lane's Event list.
// Undo restores the observed list bit-for-bit.
Op set_events_op(Id pattern, Id part, std::vector<Event> events, std::size_t before_count) {
    std::size_t bytes = std::max(events.size(), before_count) * sizeof(Event);
    return make_set<std::vector<Event>>(
        [pattern, part](Project& p) { return part_or_throw(p, pattern, part).events; },
        [pattern, part](Project& p, const std::vector<Event>& v) {
            part_or_throw(p, pattern, part).events = v;
        },
        std::move(events), bytes);
}

// Resolves a Pattern and one of its Parts for the step functions, naming the
// lane by its Instrument for labels. Any of the three pointers may be null.
struct Lane {
    const Pattern* pattern = nullptr;
    const Part* part = nullptr;
    std::string name;  // the Instrument's name, or "lane" when it is gone
};

Lane find_lane(const Project& project, Id pattern, Id part) {
    Lane lane;
    lane.pattern = project.patterns.find(pattern);
    if (!lane.pattern) return lane;
    lane.part = find_part(*lane.pattern, part);
    if (!lane.part) return lane;
    const Instrument* inst = project.instruments.find(lane.part->instrument);
    lane.name = inst ? inst->name : "lane";
    return lane;
}

std::string lane_error(const Lane& lane) {
    if (!lane.pattern) return "No such Pattern";
    return "That Instrument is not in '" + lane.pattern->name + "'";
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

Expected<Delta> rename_pattern(const Project& project, Id pattern, std::string name,
                               Origin origin) {
    if (!project.patterns.find(pattern)) return Expected<Delta>::failure("No such Pattern");
    if (name.empty()) return Expected<Delta>::failure("A Pattern needs a name");

    Delta delta;
    delta.label = "Rename Pattern to '" + name + "'";
    delta.origin = origin;
    delta.ops.push_back(make_set<std::string>(
        [pattern](Project& p) { return pattern_or_throw(p, pattern).name; },
        [pattern](Project& p, const std::string& v) { pattern_or_throw(p, pattern).name = v; },
        std::move(name), 64));
    return Expected<Delta>::success(std::move(delta));
}

Expected<Delta> set_part_steps(const Project& project, Id pattern, Id part,
                               std::vector<int> steps, Origin origin) {
    Lane lane = find_lane(project, pattern, part);
    if (!lane.part) return Expected<Delta>::failure(lane_error(lane));
    int count = step_count(*lane.pattern);
    for (int step : steps) {
        if (step < 0 || step >= count)
            return Expected<Delta>::failure("'" + lane.pattern->name + "' has " +
                                            std::to_string(count) + " steps");
    }

    // Off-grid Events are not steps: carried over untouched.
    std::vector<Event> events;
    for (const Event& e : lane.part->events)
        if (!on_grid(e)) events.push_back(e);

    // Keep the Event of every step that stays on, so its velocity survives.
    std::sort(steps.begin(), steps.end());
    steps.erase(std::unique(steps.begin(), steps.end()), steps.end());
    for (int step : steps) {
        Ticks start = step * kStepTicks;
        auto existing = std::find_if(lane.part->events.begin(), lane.part->events.end(),
                                     [start](const Event& e) { return e.start == start; });
        if (existing != lane.part->events.end()) {
            events.push_back(*existing);
        } else {
            events.push_back(Event{new_id(), start, kStepDuration, 60, kDefaultVelocity});
        }
    }

    Delta delta;
    delta.label = "Set steps of '" + lane.name + "' in '" + lane.pattern->name + "'";
    delta.origin = origin;
    delta.ops.push_back(
        set_events_op(pattern, part, std::move(events), lane.part->events.size()));
    return Expected<Delta>::success(std::move(delta));
}

Expected<Delta> set_part_step_velocities(const Project& project, Id pattern, Id part,
                                         std::vector<StepVelocity> velocities, Origin origin) {
    Lane lane = find_lane(project, pattern, part);
    if (!lane.part) return Expected<Delta>::failure(lane_error(lane));

    std::vector<Event> events = lane.part->events;
    for (const StepVelocity& sv : velocities) {
        if (sv.velocity < 1 || sv.velocity > 127)
            return Expected<Delta>::failure("Velocity must be 1 to 127");
        Ticks start = sv.step * kStepTicks;
        auto hit = std::find_if(events.begin(), events.end(),
                                [start](const Event& e) { return e.start == start; });
        if (sv.step < 0 || hit == events.end())
            return Expected<Delta>::failure("Step " + std::to_string(sv.step + 1) + " of '" +
                                            lane.name + "' is off");
        hit->velocity = sv.velocity;
    }

    Delta delta;
    delta.label = "Set velocity of '" + lane.name + "' in '" + lane.pattern->name + "'";
    delta.origin = origin;
    delta.ops.push_back(set_events_op(pattern, part, std::move(events), lane.part->events.size()));
    return Expected<Delta>::success(std::move(delta));
}

Expected<Delta> clear_part_steps(const Project& project, Id pattern, Id part, Origin origin) {
    Lane lane = find_lane(project, pattern, part);
    if (!lane.part) return Expected<Delta>::failure(lane_error(lane));

    std::vector<Event> events;
    for (const Event& e : lane.part->events)
        if (!on_grid(e)) events.push_back(e);

    Delta delta;
    delta.label = "Clear steps of '" + lane.name + "' in '" + lane.pattern->name + "'";
    delta.origin = origin;
    delta.ops.push_back(set_events_op(pattern, part, std::move(events), lane.part->events.size()));
    return Expected<Delta>::success(std::move(delta));
}

Expected<Delta> set_part_muted(const Project& project, Id pattern, Id part, bool muted,
                               Origin origin) {
    Lane lane = find_lane(project, pattern, part);
    if (!lane.part) return Expected<Delta>::failure(lane_error(lane));

    Delta delta;
    delta.label = std::string(muted ? "Mute" : "Unmute") + " '" + lane.name + "' in '" +
                  lane.pattern->name + "'";
    delta.origin = origin;
    delta.ops.push_back(make_set<bool>(
        [pattern, part](Project& p) { return part_or_throw(p, pattern, part).muted; },
        [pattern, part](Project& p, bool v) { part_or_throw(p, pattern, part).muted = v; },
        muted));
    return Expected<Delta>::success(std::move(delta));
}

}  // namespace core::functions
