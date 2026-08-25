// The Function file builder and the toggle store, against the contract in
// docs/specs/function-surface.md sections 4.2, 4.3 and 7 and the two
// worked traces.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>

#include "assistant/function_file.h"
#include "assistant/registry.h"

using namespace assistant;

namespace {

bool offers(const FunctionFile& file, std::string_view name) {
    return std::any_of(file.entries.begin(), file.entries.end(),
                       [name](const FunctionDescriptor* d) { return d->name == name; });
}

const Capabilities kGenerationOff{false};
const Capabilities kGenerationOn{true};

}  // namespace

TEST_CASE("the registry is the frozen catalogue: 43 Functions, 40 Directive, 3 Rework") {
    auto all = registry();
    REQUIRE(all.size() == 43);
    std::size_t rework = 0;
    std::set<std::string> names;
    for (const FunctionDescriptor& d : all) {
        if (d.function_class == FunctionClass::Rework) ++rework;
        names.insert(std::string(d.name));
        CHECK_FALSE(d.effect.empty());
        // Section 2.1: snake_case, and the verb comes from the closed set.
        CHECK(d.name.find(' ') == std::string_view::npos);
    }
    CHECK(rework == 3);
    CHECK(names.size() == 43);  // no duplicate names
    CHECK(find("set_channel_instrument") != nullptr);
    CHECK(find("mutate_project") == nullptr);
    CHECK(find("rework_arrangement") == nullptr);
}

TEST_CASE("only generate_sample reaches the generation provider, and both are gated") {
    for (const FunctionDescriptor& d : registry()) {
        CHECK((d.provider == Provider::Generation) == (d.name == "generate_sample"));
        CHECK(d.requires_music_generation ==
              (d.name == "generate_pattern" || d.name == "generate_sample"));
    }
}

TEST_CASE("a fresh install offers everything but the gated generation Functions") {
    FunctionToggles toggles;
    FunctionFile file = build(registry(), toggles, kGenerationOff);
    CHECK(file.entries.size() == 41);
    CHECK(file.rework_count == 3);
    CHECK_FALSE(file.local_only());
    CHECK_FALSE(offers(file, "generate_pattern"));
    CHECK_FALSE(offers(file, "generate_sample"));

    FunctionFile enabled = build(registry(), toggles, kGenerationOn);
    CHECK(enabled.entries.size() == 43);
    CHECK(offers(enabled, "generate_sample"));
}

TEST_CASE("section 6 trace: a switched-off Function is absent, and returns when re-enabled") {
    FunctionToggles toggles;
    std::uint64_t v0 = toggles.version();
    toggles.set_enabled("set_channel_instrument", false);
    CHECK(toggles.version() == v0 + 1);

    FunctionFile file = build(registry(), toggles, kGenerationOff);
    CHECK(file.entries.size() == 40);
    CHECK_FALSE(offers(file, "set_channel_instrument"));
    CHECK(offers(file, "rename_channel"));
    CHECK(offers(file, "set_channel_sample"));
    CHECK(file.rework_count == 3);

    // Registry order is preserved across the gap.
    auto before = std::find_if(file.entries.begin(), file.entries.end(),
                               [](auto* d) { return d->name == "rename_channel"; });
    CHECK((*(before + 1))->name == "set_channel_sample");

    toggles.set_enabled("set_channel_instrument", true);
    CHECK(toggles.version() == v0 + 2);
    CHECK(build(registry(), toggles, kGenerationOff).entries.size() == 41);
}

TEST_CASE("the manifest hash changes with the file and is stable for equal inputs") {
    FunctionToggles a;
    FunctionToggles b;
    CHECK(build(registry(), a, kGenerationOff).manifest_hash ==
          build(registry(), b, kGenerationOff).manifest_hash);
    b.set_enabled("set_tempo", false);
    CHECK(build(registry(), a, kGenerationOff).manifest_hash !=
          build(registry(), b, kGenerationOff).manifest_hash);
    CHECK(build(registry(), a, kGenerationOff).manifest_hash !=
          build(registry(), a, kGenerationOn).manifest_hash);
}

TEST_CASE("section 7 trace: all three Rework Functions off gives rework_count == 0") {
    FunctionToggles toggles;
    toggles.set_enabled("rework_pattern", false);
    toggles.set_enabled("continue_pattern", false);
    toggles.set_enabled("describe_pattern", false);

    FunctionFile file = build(registry(), toggles, kGenerationOn);
    CHECK(file.rework_count == 0);
    CHECK(file.local_only());
    CHECK(file.entries.size() == 40);
    for (const FunctionDescriptor* d : file.entries) {
        CHECK(d->function_class == FunctionClass::Directive);
    }
    // Generation is Directive and survives the privacy floor (spec 5.9).
    CHECK(offers(file, "generate_pattern"));
}

TEST_CASE("section 4.3: an unset Rework Function defaults off once any Rework is off") {
    FunctionToggles toggles;
    toggles.set_enabled("rework_pattern", false);

    FunctionFile file = build(registry(), toggles, kGenerationOff);
    CHECK(file.rework_count == 0);
    CHECK_FALSE(offers(file, "continue_pattern"));
    CHECK_FALSE(offers(file, "describe_pattern"));
    // An unset Directive Function is still on.
    CHECK(offers(file, "set_tempo"));

    // An explicit on beats the default.
    toggles.set_enabled("describe_pattern", true);
    CHECK(build(registry(), toggles, kGenerationOff).rework_count == 1);
}

TEST_CASE("setting a toggle to its current value is not a change") {
    FunctionToggles toggles;
    toggles.set_enabled("set_tempo", false);
    std::uint64_t v = toggles.version();
    toggles.set_enabled("set_tempo", false);
    CHECK(toggles.version() == v);
    CHECK(toggles.state("set_tempo") == false);
    CHECK_FALSE(toggles.state("set_pattern_length").has_value());
}

TEST_CASE("the toggle store round-trips through text and ignores what it cannot trust") {
    FunctionToggles toggles;
    toggles.set_enabled("rework_pattern", false);
    toggles.set_enabled("set_tempo", true);
    std::string text = toggles.to_text();
    CHECK(text == "rework_pattern=off\nset_tempo=on\n");

    FunctionToggles back = FunctionToggles::from_text(text);
    CHECK(back.state("rework_pattern") == false);
    CHECK(back.state("set_tempo") == true);
    CHECK(back.to_text() == text);

    FunctionToggles hostile = FunctionToggles::from_text(
        "set_tempo=off\r\n"
        "../evil=off\n"
        "set_pan=maybe\n"
        "no equals here\n"
        "Set_Tempo=off\n"
        "\n"
        "describe_pattern=off");
    CHECK(hostile.state("set_tempo") == false);
    CHECK(hostile.state("describe_pattern") == false);
    CHECK(hostile.to_text() == "describe_pattern=off\nset_tempo=off\n");
}
