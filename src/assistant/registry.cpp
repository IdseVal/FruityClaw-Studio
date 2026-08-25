#include "assistant/registry.h"

#include <array>

namespace assistant {
namespace {

using enum Territory;

constexpr FunctionDescriptor d(std::string_view name, Territory territory,
                               std::string_view effect) {
    return {name, FunctionClass::Directive, territory, effect, Provider::Assistant, false};
}

constexpr FunctionDescriptor r(std::string_view name, std::string_view effect) {
    return {name, FunctionClass::Rework, Territory::Rework, effect, Provider::Assistant, false};
}

// Catalogue order is declaration order (spec section 4.2): the Function file
// and the switchboard both read it top to bottom.
constexpr std::array<FunctionDescriptor, 43> kRegistry = {{
    // 5.2 Pattern lifecycle
    d("create_pattern", PatternLifecycle, "Create one empty Pattern of a given kind and length"),
    d("delete_pattern", PatternLifecycle, "Remove one Pattern"),
    d("rename_pattern", PatternLifecycle, "Change one Pattern's name"),
    d("duplicate_pattern", PatternLifecycle, "Make one copy of one Pattern"),
    d("set_pattern_length", PatternLifecycle, "Change one Pattern's length in bars"),

    // 5.3 Step events
    d("set_channel_steps", StepEvents, "Write the on/off steps of one Channel's lane in one Pattern"),
    d("set_channel_step_velocities", StepEvents,
      "Write the step velocities of one Channel's lane in one Pattern"),
    d("clear_channel_steps", StepEvents, "Empty one Channel's lane in one Pattern"),

    // 5.4 Note events
    d("add_notes", NoteEvents, "Add notes to one Pattern"),
    d("remove_notes", NoteEvents, "Remove named notes from one Pattern"),
    d("clear_pattern_notes", NoteEvents, "Empty one Pattern of notes"),
    d("set_note_velocities", NoteEvents, "Set the velocity of named notes in one Pattern"),
    d("transpose_pattern", NoteEvents, "Shift every note in one Pattern by an interval"),
    d("quantize_pattern", NoteEvents, "Snap the note timing of one Pattern to a grid"),

    // 5.5 Channels and Instruments
    d("create_channel", ChannelsAndInstruments, "Add one Channel"),
    d("delete_channel", ChannelsAndInstruments, "Remove one Channel"),
    d("rename_channel", ChannelsAndInstruments, "Change one Channel's name"),
    d("set_channel_instrument", ChannelsAndInstruments,
      "Point one Channel at a different Instrument"),
    d("set_channel_sample", ChannelsAndInstruments,
      "Point one Channel at a Sample already in the Project"),
    d("set_channel_volume", ChannelsAndInstruments, "Set one Channel's volume"),
    d("set_channel_pan", ChannelsAndInstruments, "Set one Channel's pan"),
    d("set_channel_muted", ChannelsAndInstruments, "Mute or unmute one Channel"),

    // 5.6 Effects
    d("add_effect", Effects, "Add one of the six stock Effects to one chain"),
    d("remove_effect", Effects, "Remove one Effect from one chain"),
    d("move_effect", Effects, "Move one Effect within its chain"),
    d("set_effect_parameter", Effects, "Set one parameter of one Effect"),
    d("set_effect_bypassed", Effects, "Bypass or re-enable one Effect"),

    // 5.7 Arrangement
    d("create_track", Arrangement, "Add one Track"),
    d("delete_track", Arrangement, "Remove one Track and the clips on it"),
    d("rename_track", Arrangement, "Change one Track's name"),
    d("set_track_muted", Arrangement, "Mute or unmute one Track"),
    d("place_clip", Arrangement, "Place one Pattern on one Track at one bar"),
    d("move_clip", Arrangement, "Move one clip in time or to another Track"),
    d("resize_clip", Arrangement, "Change one clip's length"),
    d("remove_clip", Arrangement, "Remove one clip from the Arrangement"),

    // 5.8 Musical frame
    d("set_tempo", MusicalFrame, "Set the Project tempo"),
    d("set_time_signature", MusicalFrame, "Set the Project time signature"),
    d("set_project_key", MusicalFrame, "Set the Project's key"),

    // 5.9 Generation — Directive, gated on the section 6.4 opt-in
    {"generate_pattern", FunctionClass::Directive, Territory::Generation,
     "Fill one Pattern with notes or steps composed from your prompt", Provider::Assistant, true},
    {"generate_sample", FunctionClass::Directive, Territory::Generation,
     "Make one new Sample from your prompt with the generation model you chose",
     Provider::Generation, true},

    // 5.10 Rework — the three Functions that transmit
    r("rework_pattern", "Replace one Pattern's notes with a transformation of them"),
    r("continue_pattern", "Continue one Pattern's material with new notes"),
    r("describe_pattern", "Describe one Pattern in words"),
}};

}  // namespace

std::span<const FunctionDescriptor> registry() { return kRegistry; }

const FunctionDescriptor* find(std::string_view name) {
    for (const FunctionDescriptor& entry : kRegistry)
        if (entry.name == name) return &entry;
    return nullptr;
}

std::string_view class_label(FunctionClass function_class) {
    return function_class == FunctionClass::Rework ? "Rework" : "Directive";
}

std::string_view territory_label(Territory territory) {
    switch (territory) {
        case PatternLifecycle: return "Patterns";
        case StepEvents: return "Steps";
        case NoteEvents: return "Notes";
        case ChannelsAndInstruments: return "Channels and Instruments";
        case Effects: return "Effects";
        case Arrangement: return "Arrangement";
        case MusicalFrame: return "Tempo, time signature and key";
        case Territory::Generation: return "Generation";
        case Territory::Rework: return "Rework";
    }
    return "";
}

std::string_view provider_label(Provider provider) {
    return provider == Provider::Generation ? "Generation model" : "Assistant provider";
}

}  // namespace assistant
