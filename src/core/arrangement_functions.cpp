#include "core/arrangement_functions.h"

#include <algorithm>
#include <utility>

namespace core::functions {
namespace {

// --- id-addressed lookups, throwing OpError from inside Ops ----------------

Arrangement& arrangement_or_throw(Project& p, Id arrangement) {
    if (Arrangement* a = p.arrangements.find(arrangement)) return *a;
    throw OpError("Arrangement not found: " + to_string(arrangement));
}

Track& track_or_throw(Project& p, Id arrangement, Id track) {
    Arrangement& a = arrangement_or_throw(p, arrangement);
    for (Track& t : a.tracks)
        if (t.id == track) return t;
    throw OpError("Track not found: " + to_string(track));
}

Placement& placement_or_throw(Track& t, Id placement) {
    for (Placement& pl : t.placements)
        if (pl.id == placement) return pl;
    throw OpError("Placement not found: " + to_string(placement));
}

// --- validation against the read-only view ---------------------------------

const Arrangement* find_arrangement(const Project& p, Id arrangement) {
    return p.arrangements.find(arrangement);
}

const Track* find_track(const Arrangement& a, Id track) {
    for (const Track& t : a.tracks)
        if (t.id == track) return &t;
    return nullptr;
}

const Placement* find_placement(const Track& t, Id placement) {
    for (const Placement& pl : t.placements)
        if (pl.id == placement) return &pl;
    return nullptr;
}

std::size_t entity_bytes(const Track& t) {
    return sizeof(Track) + t.name.size() + t.placements.size() * sizeof(Placement);
}

// --- op builders over Track / Placement collections ------------------------
//
// Insert and Remove come in mutually inverse pairs. A Remove discovers the
// entity's position at application time and bakes the observed index into the
// Insert it returns, so undo restores the collection bit-for-bit (invariant
// I4) — position is payload, never an address.

Op insert_track_op(Id arrangement, Track track, std::size_t index);

Op remove_track_op(Id arrangement, Id track_id, std::size_t bytes) {
    Op op;
    op.bytes = bytes;
    op.run = [arrangement, track_id, bytes](Project& p) -> std::optional<Op> {
        Arrangement& a = arrangement_or_throw(p, arrangement);
        for (auto it = a.tracks.begin(); it != a.tracks.end(); ++it) {
            if (it->id == track_id) {
                std::size_t index = static_cast<std::size_t>(it - a.tracks.begin());
                Track removed = std::move(*it);
                a.tracks.erase(it);
                return insert_track_op(arrangement, std::move(removed), index);
            }
        }
        throw OpError("Track not found: " + to_string(track_id));
    };
    return op;
}

Op insert_track_op(Id arrangement, Track track, std::size_t index) {
    Op op;
    op.bytes = entity_bytes(track);
    op.run = [arrangement, track = std::move(track), index](Project& p) -> std::optional<Op> {
        Arrangement& a = arrangement_or_throw(p, arrangement);
        std::size_t at = std::min(index, a.tracks.size());
        a.tracks.insert(a.tracks.begin() + static_cast<std::ptrdiff_t>(at), track);
        return remove_track_op(arrangement, track.id, entity_bytes(track));
    };
    return op;
}

Op insert_placement_op(Id arrangement, Id track, Placement placement, std::size_t index);

Op remove_placement_op(Id arrangement, Id track, Id placement) {
    Op op;
    op.bytes = sizeof(Placement);
    op.run = [arrangement, track, placement](Project& p) -> std::optional<Op> {
        Track& t = track_or_throw(p, arrangement, track);
        for (auto it = t.placements.begin(); it != t.placements.end(); ++it) {
            if (it->id == placement) {
                std::size_t index = static_cast<std::size_t>(it - t.placements.begin());
                Placement removed = *it;
                t.placements.erase(it);
                return insert_placement_op(arrangement, track, removed, index);
            }
        }
        throw OpError("Placement not found: " + to_string(placement));
    };
    return op;
}

Op insert_placement_op(Id arrangement, Id track, Placement placement, std::size_t index) {
    Op op;
    op.bytes = sizeof(Placement);
    op.run = [arrangement, track, placement, index](Project& p) -> std::optional<Op> {
        Track& t = track_or_throw(p, arrangement, track);
        std::size_t at = std::min(index, t.placements.size());
        t.placements.insert(t.placements.begin() + static_cast<std::ptrdiff_t>(at), placement);
        return remove_placement_op(arrangement, track, placement.id);
    };
    return op;
}

// Labels describe the effect in domain vocabulary; they never name a Function
// (history contract section 7.3).
std::string pattern_label(const Project& p, Id pattern) {
    const Pattern* pat = p.patterns.find(pattern);
    return pat ? "'" + pat->name + "'" : "Pattern";
}

}  // namespace

Expected<CreatedDelta> create_track(const Project& project, Id arrangement,
                                    std::string name, std::optional<Colour> colour,
                                    Origin origin) {
    const Arrangement* a = find_arrangement(project, arrangement);
    if (!a) return Expected<CreatedDelta>::failure("No such Arrangement");

    Track track;
    track.id = new_id();
    track.name = std::move(name);
    track.colour = colour;

    Delta delta;
    delta.label = "Add Track '" + track.name + "'";
    delta.origin = origin;
    Id id = track.id;
    delta.ops.push_back(insert_track_op(arrangement, std::move(track), a->tracks.size()));
    return Expected<CreatedDelta>::success({std::move(delta), id});
}

Expected<Delta> delete_track(const Project& project, Id arrangement, Id track,
                             Origin origin) {
    const Arrangement* a = find_arrangement(project, arrangement);
    if (!a) return Expected<Delta>::failure("No such Arrangement");
    const Track* t = find_track(*a, track);
    if (!t) return Expected<Delta>::failure("No such Track");

    Delta delta;
    delta.label = "Delete Track '" + t->name + "'";
    delta.origin = origin;
    delta.ops.push_back(remove_track_op(arrangement, track, entity_bytes(*t)));
    return Expected<Delta>::success(std::move(delta));
}

Expected<Delta> rename_track(const Project& project, Id arrangement, Id track,
                             std::string name, Origin origin) {
    const Arrangement* a = find_arrangement(project, arrangement);
    const Track* t = a ? find_track(*a, track) : nullptr;
    if (!t) return Expected<Delta>::failure("No such Track");

    Delta delta;
    delta.label = "Rename Track to '" + name + "'";
    delta.origin = origin;
    delta.ops.push_back(make_set<std::string>(
        [arrangement, track](Project& p) {
            return track_or_throw(p, arrangement, track).name;
        },
        [arrangement, track](Project& p, const std::string& v) {
            track_or_throw(p, arrangement, track).name = v;
        },
        std::move(name), 64));
    return Expected<Delta>::success(std::move(delta));
}

Expected<Delta> set_track_muted(const Project& project, Id arrangement, Id track,
                                bool muted, Origin origin) {
    const Arrangement* a = find_arrangement(project, arrangement);
    const Track* t = a ? find_track(*a, track) : nullptr;
    if (!t) return Expected<Delta>::failure("No such Track");

    Delta delta;
    delta.label = std::string(muted ? "Mute" : "Unmute") + " Track '" + t->name + "'";
    delta.origin = origin;
    delta.ops.push_back(make_set<bool>(
        [arrangement, track](Project& p) {
            return track_or_throw(p, arrangement, track).muted;
        },
        [arrangement, track](Project& p, bool v) {
            track_or_throw(p, arrangement, track).muted = v;
        },
        muted));
    return Expected<Delta>::success(std::move(delta));
}

Expected<CreatedDelta> add_placement(const Project& project, Id arrangement, Id track,
                                     Id pattern, Ticks start, Ticks length,
                                     Origin origin) {
    const Arrangement* a = find_arrangement(project, arrangement);
    const Track* t = a ? find_track(*a, track) : nullptr;
    if (!t) return Expected<CreatedDelta>::failure("No such Track");
    const Pattern* pat = project.patterns.find(pattern);
    if (!pat) return Expected<CreatedDelta>::failure("No such Pattern");
    if (start < 0) return Expected<CreatedDelta>::failure("Placement start before zero");
    if (length < 0) return Expected<CreatedDelta>::failure("Placement length below zero");

    Placement placement;
    placement.id = new_id();
    placement.pattern = pattern;
    placement.start = start;
    placement.length = length == 0 ? pat->length : length;

    Delta delta;
    delta.label = "Place " + pattern_label(project, pattern) + " on '" + t->name + "'";
    delta.origin = origin;
    Id id = placement.id;
    delta.ops.push_back(
        insert_placement_op(arrangement, track, std::move(placement), t->placements.size()));
    return Expected<CreatedDelta>::success({std::move(delta), id});
}

Expected<Delta> move_placement(const Project& project, Id arrangement, Id track,
                               Id placement, Ticks new_start, Id to_track,
                               Origin origin) {
    const Arrangement* a = find_arrangement(project, arrangement);
    const Track* from = a ? find_track(*a, track) : nullptr;
    if (!from) return Expected<Delta>::failure("No such Track");
    const Placement* pl = find_placement(*from, placement);
    if (!pl) return Expected<Delta>::failure("No such Placement");
    const Track* to = find_track(*a, to_track);
    if (!to) return Expected<Delta>::failure("No such destination Track");
    if (new_start < 0) return Expected<Delta>::failure("Placement start before zero");

    Delta delta;
    delta.label = "Move " + pattern_label(project, pl->pattern);
    delta.origin = origin;

    if (track == to_track) {
        delta.ops.push_back(make_set<Ticks>(
            [arrangement, track, placement](Project& p) {
                Track& t = track_or_throw(p, arrangement, track);
                return placement_or_throw(t, placement).start;
            },
            [arrangement, track, placement](Project& p, Ticks v) {
                Track& t = track_or_throw(p, arrangement, track);
                placement_or_throw(t, placement).start = v;
            },
            new_start));
    } else {
        // Across Tracks: one Delta, two Ops — the whole move applies or
        // neither half does, and its inverse restores both together.
        Placement moved = *pl;
        moved.start = new_start;
        delta.ops.push_back(remove_placement_op(arrangement, track, placement));
        delta.ops.push_back(
            insert_placement_op(arrangement, to_track, std::move(moved), to->placements.size()));
    }
    return Expected<Delta>::success(std::move(delta));
}

Expected<Delta> resize_placement(const Project& project, Id arrangement, Id track,
                                 Id placement, Ticks new_length, Origin origin) {
    const Arrangement* a = find_arrangement(project, arrangement);
    const Track* t = a ? find_track(*a, track) : nullptr;
    if (!t) return Expected<Delta>::failure("No such Track");
    const Placement* pl = find_placement(*t, placement);
    if (!pl) return Expected<Delta>::failure("No such Placement");
    if (new_length <= 0) return Expected<Delta>::failure("Placement length must be positive");

    Delta delta;
    delta.label = "Resize " + pattern_label(project, pl->pattern);
    delta.origin = origin;
    delta.ops.push_back(make_set<Ticks>(
        [arrangement, track, placement](Project& p) {
            Track& tr = track_or_throw(p, arrangement, track);
            return placement_or_throw(tr, placement).length;
        },
        [arrangement, track, placement](Project& p, Ticks v) {
            Track& tr = track_or_throw(p, arrangement, track);
            placement_or_throw(tr, placement).length = v;
        },
        new_length));
    return Expected<Delta>::success(std::move(delta));
}

Expected<Delta> remove_placement(const Project& project, Id arrangement, Id track,
                                 Id placement, Origin origin) {
    const Arrangement* a = find_arrangement(project, arrangement);
    const Track* t = a ? find_track(*a, track) : nullptr;
    if (!t) return Expected<Delta>::failure("No such Track");
    const Placement* pl = find_placement(*t, placement);
    if (!pl) return Expected<Delta>::failure("No such Placement");

    Delta delta;
    delta.label = "Remove " + pattern_label(project, pl->pattern) + " from '" + t->name + "'";
    delta.origin = origin;
    delta.ops.push_back(remove_placement_op(arrangement, track, placement));
    return Expected<Delta>::success(std::move(delta));
}

}  // namespace core::functions
