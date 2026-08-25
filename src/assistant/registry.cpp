// The catalogue. Names follow docs/specs/function-surface.md section 5 where
// the vocabulary is ratified, and the frozen data-model terms where the
// catalogue's word is not (Placement for Clip, Part for Channel — spec open
// item 1), consistent with the core function headers. One Function, one
// effect; the "does not touch" column of the spec is what the implementation
// in core already guarantees.
#include "assistant/registry.h"

#include "core/pattern_functions.h"
#include "core/sample_functions.h"

namespace assistant {
namespace {

using core::Id;
using core::Origin;
using core::functions::Expected;

// --- schema builders ---------------------------------------------------------

Property integer(std::string name, long long min, long long max, std::string description) {
    Property p;
    p.name = std::move(name);
    p.kind = Property::Kind::Integer;
    p.min = min;
    p.max = max;
    p.description = std::move(description);
    return p;
}

Property text(std::string name, std::string description) {
    Property p;
    p.name = std::move(name);
    p.kind = Property::Kind::String;
    p.description = std::move(description);
    return p;
}

Property yes_no(std::string name, std::string description) {
    Property p;
    p.name = std::move(name);
    p.kind = Property::Kind::Boolean;
    p.description = std::move(description);
    return p;
}

Property one_of(std::string name, std::vector<std::string> options, std::string description) {
    Property p;
    p.name = std::move(name);
    p.kind = Property::Kind::Enum;
    p.options = std::move(options);
    p.description = std::move(description);
    return p;
}

Property selector(std::string name, EntityKind entity, std::string description,
                  bool nullable = false) {
    Property p;
    p.name = std::move(name);
    p.kind = Property::Kind::Selector;
    p.entity = entity;
    p.description = std::move(description);
    p.nullable = nullable;
    return p;
}

Property integers(std::string name, long long min, long long max, std::string description) {
    Property p;
    p.name = std::move(name);
    p.kind = Property::Kind::IntegerArray;
    p.min = min;
    p.max = max;
    p.description = std::move(description);
    return p;
}

// --- argument accessors (validated and resolved before dispatch) -------------

Id id_of(const ResolvedArgs& args, const char* name) {
    return std::get<Id>(args.at(name));
}

std::optional<Id> optional_id_of(const ResolvedArgs& args, const char* name) {
    if (const Id* id = std::get_if<Id>(&args.at(name))) return *id;
    return std::nullopt;
}

const std::string& text_of(const ResolvedArgs& args, const char* name) {
    return std::get<std::string>(args.at(name));
}

long long integer_of(const ResolvedArgs& args, const char* name) {
    return std::get<long long>(args.at(name));
}

bool flag_of(const ResolvedArgs& args, const char* name) {
    return std::get<bool>(args.at(name));
}

// The one Arrangement of the MVP (project-data-model spec: exactly one).
Expected<Id> arrangement_of(const core::Project& project) {
    if (project.arrangements.items.empty()) return Expected<Id>::failure("No Arrangement");
    return Expected<Id>::success(project.arrangements.items.front().id);
}

// The lane of `pattern` for `instrument`; with no Instrument named, the
// Pattern's only lane.
Expected<Id> part_of(const core::Project& project, Id pattern, std::optional<Id> instrument) {
    const core::Pattern* pat = project.patterns.find(pattern);
    if (!pat) return Expected<Id>::failure("No such Pattern");
    if (!instrument) {
        if (pat->parts.size() == 1) return Expected<Id>::success(pat->parts.front().id);
        return Expected<Id>::failure("'" + pat->name + "' has " +
                                     std::to_string(pat->parts.size()) +
                                     " lanes; say which Instrument");
    }
    for (const core::Part& part : pat->parts)
        if (part.instrument == *instrument) return Expected<Id>::success(part.id);
    return Expected<Id>::failure("That Instrument is not in '" + pat->name + "'");
}

// Adapters from the core function results to FunctionResult.
Expected<FunctionResult> plain(Expected<core::Delta> result) {
    if (!result.ok()) return Expected<FunctionResult>::failure(result.error);
    return Expected<FunctionResult>::success({std::move(*result), std::nullopt, std::nullopt});
}

Expected<FunctionResult> created(Expected<core::functions::CreatedDelta> result, EntityKind kind) {
    if (!result.ok()) return Expected<FunctionResult>::failure(result.error);
    return Expected<FunctionResult>::success({std::move(result->delta), result->id, kind});
}

Registry make_registry() {
    using namespace core::functions;
    Registry r;

    // --- Pattern lifecycle (spec 5.2) ----------------------------------------
    r.push_back({
        "create_pattern",
        "Create one new, empty Pattern with a name and a length in bars. It is not placed on "
        "any Track and has no Instrument yet.",
        "Creates one empty Pattern",
        {{text("name", "A short name for the Pattern"),
          integer("length_bars", 1, 64, "Length in bars")}},
        [](const ResolvedArgs& a, const core::Project& p) {
            return created(create_pattern(p, text_of(a, "name"),
                                          static_cast<int>(integer_of(a, "length_bars")),
                                          Origin::Assistant),
                           EntityKind::Pattern);
        },
    });

    // --- step events (spec 5.3) ------------------------------------------------
    r.push_back({
        "set_part_steps",
        "Write the sixteenth-note step grid of one Instrument's lane in one Pattern. List the "
        "step numbers that are on (0 is the first sixteenth of the Pattern); all others are off. "
        "The lane must already exist — add the Instrument to the Pattern first if it does not.",
        "Writes the on/off steps of one lane in one Pattern",
        {{selector("pattern", EntityKind::Pattern, "The Pattern"),
          selector("instrument", EntityKind::Instrument, "Whose lane", true),
          integers("steps", 0, 1023, "Step numbers that are on")}},
        [](const ResolvedArgs& a, const core::Project& p) {
            Id pattern = id_of(a, "pattern");
            Expected<Id> part = part_of(p, pattern, optional_id_of(a, "instrument"));
            if (!part.ok()) return Expected<FunctionResult>::failure(part.error);
            std::vector<int> steps;
            for (long long s : std::get<std::vector<long long>>(a.at("steps")))
                steps.push_back(static_cast<int>(s));
            return plain(set_part_steps(p, pattern, *part, std::move(steps), Origin::Assistant));
        },
    });

    // --- Instruments and lanes (spec 5.5) --------------------------------------
    r.push_back({
        "create_instrument",
        "Create one Instrument that plays one Sample already in the Project. It is not added "
        "to any Pattern.",
        "Creates one Instrument over one Sample",
        {{text("name", "A short name for the Instrument"),
          selector("sample", EntityKind::Sample, "The Sample it plays"),
          one_of("mode", {"one_shot", "sustain"},
                 "one_shot for drums and hits; sustain for pitched, held notes")}},
        [](const ResolvedArgs& a, const core::Project& p) {
            core::SamplerMode mode = text_of(a, "mode") == "sustain" ? core::SamplerMode::Sustain
                                                                      : core::SamplerMode::OneShot;
            return created(create_instrument(p, text_of(a, "name"), id_of(a, "sample"), mode,
                                             Origin::Assistant),
                           EntityKind::Instrument);
        },
    });
    r.push_back({
        "add_part",
        "Add one Instrument to one Pattern as a new, empty lane.",
        "Adds one Instrument to one Pattern as an empty lane",
        {{selector("pattern", EntityKind::Pattern, "The Pattern"),
          selector("instrument", EntityKind::Instrument, "The Instrument to add")}},
        [](const ResolvedArgs& a, const core::Project& p) {
            Expected<CreatedDelta> result =
                add_part(p, id_of(a, "pattern"), id_of(a, "instrument"), Origin::Assistant);
            if (!result.ok()) return Expected<FunctionResult>::failure(result.error);
            // A Part is addressed through its Instrument, so nothing new to
            // remember for LastCreated.
            return Expected<FunctionResult>::success({std::move(result->delta), std::nullopt,
                                                      std::nullopt});
        },
    });
    r.push_back({
        "set_part_instrument",
        "Switch one lane of one Pattern to a different Instrument. The notes and steps stay "
        "exactly as they are.",
        "Points one lane of one Pattern at another Instrument",
        {{selector("pattern", EntityKind::Pattern, "The Pattern"),
          selector("current", EntityKind::Instrument, "The Instrument the lane plays now", true),
          selector("instrument", EntityKind::Instrument, "The Instrument to switch to")}},
        [](const ResolvedArgs& a, const core::Project& p) {
            Id pattern = id_of(a, "pattern");
            Expected<Id> part = part_of(p, pattern, optional_id_of(a, "current"));
            if (!part.ok()) return Expected<FunctionResult>::failure(part.error);
            return plain(set_part_instrument(p, pattern, *part, id_of(a, "instrument"),
                                             Origin::Assistant));
        },
    });
    r.push_back({
        "set_instrument_sample",
        "Make one Instrument play a different Sample that is already in the Project.",
        "Points one Instrument at another Sample",
        {{selector("instrument", EntityKind::Instrument, "The Instrument"),
          selector("sample", EntityKind::Sample, "The Sample it should play")}},
        [](const ResolvedArgs& a, const core::Project& p) {
            return plain(set_instrument_sample(p, id_of(a, "instrument"), id_of(a, "sample"),
                                               Origin::Assistant));
        },
    });

    // --- Arrangement (spec 5.7) -----------------------------------------------
    r.push_back({
        "create_track",
        "Add one empty Track lane at the bottom of the Arrangement.",
        "Adds one empty Track",
        {{text("name", "A short name for the Track")}},
        [](const ResolvedArgs& a, const core::Project& p) {
            Expected<Id> arrangement = arrangement_of(p);
            if (!arrangement.ok()) return Expected<FunctionResult>::failure(arrangement.error);
            return created(create_track(p, *arrangement, text_of(a, "name"), std::nullopt,
                                        Origin::Assistant),
                           EntityKind::Track);
        },
    });
    r.push_back({
        "delete_track",
        "Remove one Track from the Arrangement, with everything placed on it. The Patterns "
        "themselves are kept.",
        "Removes one Track",
        {{selector("track", EntityKind::Track, "The Track")}},
        [](const ResolvedArgs& a, const core::Project& p) {
            Expected<Id> arrangement = arrangement_of(p);
            if (!arrangement.ok()) return Expected<FunctionResult>::failure(arrangement.error);
            return plain(delete_track(p, *arrangement, id_of(a, "track"), Origin::Assistant));
        },
    });
    r.push_back({
        "rename_track",
        "Change one Track's name.",
        "Renames one Track",
        {{selector("track", EntityKind::Track, "The Track"), text("name", "The new name")}},
        [](const ResolvedArgs& a, const core::Project& p) {
            Expected<Id> arrangement = arrangement_of(p);
            if (!arrangement.ok()) return Expected<FunctionResult>::failure(arrangement.error);
            return plain(rename_track(p, *arrangement, id_of(a, "track"), text_of(a, "name"),
                                      Origin::Assistant));
        },
    });
    r.push_back({
        "set_track_muted",
        "Mute or unmute one Track.",
        "Mutes or unmutes one Track",
        {{selector("track", EntityKind::Track, "The Track"),
          yes_no("muted", "true to mute, false to unmute")}},
        [](const ResolvedArgs& a, const core::Project& p) {
            Expected<Id> arrangement = arrangement_of(p);
            if (!arrangement.ok()) return Expected<FunctionResult>::failure(arrangement.error);
            return plain(set_track_muted(p, *arrangement, id_of(a, "track"), flag_of(a, "muted"),
                                         Origin::Assistant));
        },
    });
    r.push_back({
        "add_placement",
        "Place one existing Pattern on one Track, starting at a bar (1 is the first bar). The "
        "Pattern is referenced, not copied, and plays for its own length.",
        "Places one Pattern on one Track at one bar",
        {{selector("track", EntityKind::Track, "The Track"),
          selector("pattern", EntityKind::Pattern, "The Pattern to place"),
          integer("bar", 1, 9999, "The bar it starts on")}},
        [](const ResolvedArgs& a, const core::Project& p) {
            Expected<Id> arrangement = arrangement_of(p);
            if (!arrangement.ok()) return Expected<FunctionResult>::failure(arrangement.error);
            core::Ticks start = (integer_of(a, "bar") - 1) * p.time_signature.first * core::kPpq;
            Expected<CreatedDelta> result =
                add_placement(p, *arrangement, id_of(a, "track"), id_of(a, "pattern"), start, 0,
                              Origin::Assistant);
            if (!result.ok()) return Expected<FunctionResult>::failure(result.error);
            return Expected<FunctionResult>::success({std::move(result->delta), std::nullopt,
                                                      std::nullopt});
        },
    });

    return r;
}

}  // namespace

const Registry& registry() {
    static const Registry instance = make_registry();
    return instance;
}

const FunctionDescriptor* find(const Registry& registry, const std::string& name) {
    for (const FunctionDescriptor& descriptor : registry)
        if (descriptor.name == name) return &descriptor;
    return nullptr;
}

}  // namespace assistant
