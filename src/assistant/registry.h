// The Function registry: the one list every other reading of the Function
// surface is a projection of (docs/specs/function-surface.md section 4.1,
// ADR-006 D6). The switchboard renders it, the builder filters it, and the
// outbound tool definitions (issue #16) are emitted from it. There is no
// second list anywhere; a Function absent here does not exist.
//
// What is held per entry is what the switchboard and the builder need: the
// frozen catalogue name, the class, the territory the catalogue groups it
// under, the single effect in the user's words, which provider it talks to
// (obligation O-17.2) and what it requires before it may be offered. The
// argument schema, the appended instruction and the bound implementation
// are issue #16's columns and are added there, on this same list.
//
// Names are the catalogue's (section 5), including the Channel and Clip
// terms pending ratification (spec open item 1); the local implementations
// in src/core/ carry the data-model names until that closes.
#pragma once

#include <span>
#include <string_view>

namespace assistant {

// The two classes of core document section 8.4. Only Rework transmits
// Project content. When #16 binds implementations the class becomes a
// difference in type (spec section 3); here it decides how a row is
// labelled and what the builder counts.
enum class FunctionClass { Directive, Rework };

// The catalogue's territories, in its order (spec sections 5.2 to 5.10).
enum class Territory {
    PatternLifecycle,
    StepEvents,
    NoteEvents,
    ChannelsAndInstruments,
    Effects,
    Arrangement,
    MusicalFrame,
    Generation,
    Rework,
};

// Which external party a call to this Function reaches. Directive and
// Rework Functions reach the Assistant's LLM provider; generate_sample
// reaches the separately configured generation model (core document 6.4).
enum class Provider { Assistant, Generation };

struct FunctionDescriptor {
    std::string_view name;      // catalogue name, snake_case verb_object
    FunctionClass function_class;
    Territory territory;
    std::string_view effect;    // the single thing it does, for the switchboard
    Provider provider;
    // Section 6.4: music generation is off by default, and both generation
    // Functions are absent from the file until the user enables it.
    bool requires_music_generation;
};

// The 43 Functions of the frozen catalogue, in declaration order.
std::span<const FunctionDescriptor> registry();

// Looks a Function up by its catalogue name; nullptr when there is none.
const FunctionDescriptor* find(std::string_view name);

// Prose for the switchboard's labels.
std::string_view class_label(FunctionClass function_class);
std::string_view territory_label(Territory territory);
std::string_view provider_label(Provider provider);

}  // namespace assistant
