// Core document section 6.4 as tests: off and empty by default, one
// handpicked list with a rights position on every entry, and the rules that
// gate turning generation on. The store that keeps the choice between
// sessions is covered by test_generation_file.cpp.
#include <catch2/catch_test_macros.hpp>

#include "core/generation.h"

using namespace core;

TEST_CASE("a fresh install has generation off with no model and no weights") {
    GenerationSettings fresh;
    CHECK_FALSE(fresh.enabled);
    CHECK(fresh.model.empty());
    CHECK(fresh.weights_path.empty());
}

TEST_CASE("every handpicked entry states its rights position, not just its name") {
    const auto& models = handpicked_models();
    REQUIRE_FALSE(models.empty());
    for (const GenerationModel& model : models) {
        INFO(model.id);
        CHECK_FALSE(model.name.empty());
        CHECK_FALSE(model.runs.empty());
        CHECK_FALSE(model.leaves_machine.empty());
        CHECK_FALSE(model.output_rights.empty());
        CHECK_FALSE(model.conditions.empty());
        CHECK(find_model(model.id) == &model);
    }
}

TEST_CASE("both enablement paths of section 6.4 are on the list") {
    bool local = false;
    bool remote = false;
    for (const GenerationModel& model : handpicked_models()) {
        if (!model.available) continue;
        local |= model.path == GenerationPath::Local;
        remote |= model.path == GenerationPath::Remote;
    }
    CHECK(local);
    CHECK(remote);
}

TEST_CASE("an unlisted model is not found") {
    CHECK(find_model("") == nullptr);
    CHECK(find_model("suno") == nullptr);  // rejected in ADR-003
}

TEST_CASE("enabling needs a chosen, available model") {
    GenerationSettings s;
    CHECK(enable_problem(s, true, true) == "Choose a model first.");

    s.model = "stable-audio-api";  // ADR-003 entry 4: provisional
    CHECK(enable_problem(s, true, true).has_value());
}

TEST_CASE("a local model needs its weights folder and no key") {
    GenerationSettings s;
    s.model = "stable-audio-3";
    CHECK(enable_problem(s, false, true).has_value());
    s.weights_path = "C:/models/stable-audio-3-medium";
    CHECK_FALSE(enable_problem(s, false, true).has_value());
}

TEST_CASE("a remote model needs a stored key and no weights") {
    GenerationSettings s;
    s.model = "elevenlabs-music";
    CHECK(enable_problem(s, false, true).has_value());
    CHECK_FALSE(enable_problem(s, true, true).has_value());
}

TEST_CASE("the section 6.2 warning must be acknowledged before enabling") {
    GenerationSettings s;
    s.model = "ace-step-1.5";
    s.weights_path = "/models/ace-step";
    CHECK(enable_problem(s, false, false) ==
          "Confirm that you understand generated output may not be licenseable.");
    CHECK_FALSE(enable_problem(s, false, true).has_value());
}
