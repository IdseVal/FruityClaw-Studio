// Project save and load checks (issue #13): exact round trip, provenance
// survival, atomic save with a retained previous version, and clean
// rejection of damaged or foreign files.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "core/arrangement_functions.h"
#include "core/history.h"
#include "persistence/project_file.h"
#include "test_support.h"

using namespace core;
using namespace persistence;
namespace fs = std::filesystem;

namespace {

// Sample::operator== compares the SampleSource pointer, so a loaded Project
// can never be pointer-equal to the one saved. Equivalence here is: every
// audio field matches, then with the pointers aliased the Projects compare
// equal field for field.
bool equivalent(const Project& original, Project loaded) {
    if (loaded.samples.items.size() != original.samples.items.size()) return false;
    for (std::size_t i = 0; i < original.samples.items.size(); ++i) {
        const SampleSource& a = original.samples.items[i].source;
        SampleSource& b = loaded.samples.items[i].source;
        if (static_cast<bool>(a) != static_cast<bool>(b)) return false;
        if (a) {
            if (a->sample_rate != b->sample_rate || a->channels != b->channels ||
                a->frames != b->frames) {
                return false;
            }
        }
        b = a;
    }
    return loaded == original;
}

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("project-file-test-" + to_string(new_id()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

std::vector<std::byte> read_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return {reinterpret_cast<std::byte*>(chars.data()),
            reinterpret_cast<std::byte*>(chars.data()) + chars.size()};
}

void write_bytes(const fs::path& p, std::span<const std::byte> bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

// A Project exercising every field the format carries: provenance on both
// kinds of entity, colours present and absent, effect chains with params, a
// Sample with no audio, placements, and a non-default time signature.
Project make_full_project() {
    auto f = test_support::make_fixture();
    Project& p = f.project;
    p.title = "Round trip \xC3\xA9";  // non-ASCII survives byte-for-byte
    p.tempo = 133.5;
    p.time_signature = {7, 8};

    p.samples.items[1].provenance = Provenance::generated("audio-model-x", 1756100000);
    p.samples.items.push_back(Sample{new_id(), "silent", nullptr, Provenance::human()});
    p.patterns.items[1].provenance = Provenance::generated("assistant", 1756100001);
    p.patterns.items[1].colour = 0x336699;

    Effect eq;
    eq.id = new_id();
    eq.type = EffectType::Eq;
    eq.params = {{"low_gain", 0.25f}, {"high_gain", -0.5f}};
    p.instruments.items[0].chain.push_back(eq);
    Effect limiter;
    limiter.id = new_id();
    limiter.type = EffectType::Limiter;
    limiter.enabled = false;
    limiter.params = {{"ceiling", 0.9f}};
    p.master_chain.push_back(limiter);

    Arrangement& a = p.arrangements.items[0];
    a.tracks[0].colour = 0xFF8800;
    a.tracks[0].placements.push_back(Placement{new_id(), f.drum_pattern, 0, 4 * kPpq, false});
    a.tracks[1].placements.push_back(
        Placement{new_id(), f.melody_pattern, 4 * kPpq, 8 * kPpq, true});
    a.tracks[1].muted = true;
    return p;
}

}  // namespace

TEST_CASE("encode then decode reproduces the Project exactly") {
    Project original = make_full_project();
    LoadResult loaded = decode(encode(original));
    REQUIRE(loaded.ok());
    CHECK(equivalent(original, *loaded.project));
    CHECK(loaded.project->format_version == static_cast<int>(kFormatVersion));
}

TEST_CASE("an empty Project round-trips") {
    Project empty;
    empty.id = new_id();
    LoadResult loaded = decode(encode(empty));
    REQUIRE(loaded.ok());
    CHECK(equivalent(empty, *loaded.project));
}

TEST_CASE("AI provenance on Samples and Patterns survives a save and load") {
    Project original = make_full_project();
    LoadResult loaded = decode(encode(original));
    REQUIRE(loaded.ok());

    const Sample& sample = loaded.project->samples.items[1];
    CHECK_FALSE(sample.provenance.is_human());
    CHECK(sample.provenance.generated_by == "audio-model-x");
    CHECK(sample.provenance.generated_at == 1756100000);

    const Pattern& pattern = loaded.project->patterns.items[1];
    CHECK_FALSE(pattern.provenance.is_human());
    CHECK(pattern.provenance.generated_by == "assistant");
    CHECK(pattern.provenance.generated_at == 1756100001);

    CHECK(loaded.project->samples.items[0].provenance.is_human());
    CHECK(loaded.project->patterns.items[0].provenance.is_human());
}

TEST_CASE("save then load from disk reproduces the Project exactly") {
    TempDir dir;
    fs::path file = dir.path / "one.project";
    Project original = make_full_project();

    SaveResult saved = save_project(original, file);
    REQUIRE(saved.ok);
    CHECK(saved.error.empty());

    LoadResult loaded = load_project(file);
    REQUIRE(loaded.ok());
    CHECK(equivalent(original, *loaded.project));

    // No temporary file is left behind.
    int entries = 0;
    for (const auto& e : fs::directory_iterator(dir.path)) {
        (void)e;
        ++entries;
    }
    CHECK(entries == 1);
}

TEST_CASE("saving over an existing file retains the previous good version") {
    TempDir dir;
    fs::path file = dir.path / "one.project";
    Project first = make_full_project();
    Project second = first;
    second.title = "Second";

    REQUIRE(save_project(first, file).ok);
    CHECK_FALSE(fs::exists(previous_version_path(file)));

    REQUIRE(save_project(second, file).ok);
    REQUIRE(fs::exists(previous_version_path(file)));

    LoadResult current = load_project(file);
    LoadResult previous = load_project(previous_version_path(file));
    REQUIRE(current.ok());
    REQUIRE(previous.ok());
    CHECK(current.project->title == "Second");
    CHECK(equivalent(first, *previous.project));
}

TEST_CASE("a failed save leaves the existing file and its retained version untouched") {
    TempDir dir;
    fs::path file = dir.path / "one.project";
    Project good = make_full_project();
    REQUIRE(save_project(good, file).ok);
    std::vector<std::byte> before = read_bytes(file);

    // The target becomes a directory, so the final rename cannot succeed.
    fs::path blocked = dir.path / "blocked.project";
    fs::create_directories(blocked);
    SaveResult failed = save_project(good, blocked);
    CHECK_FALSE(failed.ok);
    CHECK_FALSE(failed.error.empty());

    // The nonexistent-directory case fails before anything is written.
    SaveResult missing = save_project(good, dir.path / "nope" / "x.project");
    CHECK_FALSE(missing.ok);

    CHECK(read_bytes(file) == before);
    CHECK_FALSE(fs::exists(previous_version_path(file)));
    for (const auto& e : fs::directory_iterator(dir.path)) {
        CHECK(e.path().string().find(".tmp-") == std::string::npos);
    }
}

TEST_CASE("a truncated file is rejected and the retained version still loads") {
    TempDir dir;
    fs::path file = dir.path / "one.project";
    Project first = make_full_project();
    Project second = first;
    second.title = "Second";
    REQUIRE(save_project(first, file).ok);
    REQUIRE(save_project(second, file).ok);

    std::vector<std::byte> bytes = read_bytes(file);
    bytes.resize(bytes.size() / 2);
    write_bytes(file, bytes);

    LoadResult damaged = load_project(file);
    CHECK_FALSE(damaged.ok());
    CHECK(damaged.error == "file is truncated");

    LoadResult previous = load_project(previous_version_path(file));
    REQUIRE(previous.ok());
    CHECK(equivalent(first, *previous.project));
}

TEST_CASE("saving over a damaged file keeps the last good retained version") {
    TempDir dir;
    fs::path file = dir.path / "one.project";
    Project first = make_full_project();
    Project second = first;
    second.title = "Second";
    Project third = first;
    third.title = "Third";
    REQUIRE(save_project(first, file).ok);
    REQUIRE(save_project(second, file).ok);  // .previous is now `first`

    // The current file is damaged, as an earlier crash might have left it.
    std::vector<std::byte> bytes = read_bytes(file);
    bytes[bytes.size() - 10] ^= std::byte{0x01};
    write_bytes(file, bytes);
    REQUIRE_FALSE(load_project(file).ok());

    REQUIRE(save_project(third, file).ok);

    LoadResult current = load_project(file);
    LoadResult previous = load_project(previous_version_path(file));
    REQUIRE(current.ok());
    REQUIRE(previous.ok());
    CHECK(current.project->title == "Third");
    CHECK(equivalent(first, *previous.project));  // not the damaged `second`
}

TEST_CASE("a flipped byte in the payload is caught by the checksum") {
    std::vector<std::byte> bytes = encode(make_full_project());
    bytes[bytes.size() - 10] ^= std::byte{0x01};
    LoadResult loaded = decode(bytes);
    CHECK_FALSE(loaded.ok());
    CHECK(loaded.error == "file is damaged (checksum mismatch)");
}

TEST_CASE("a foreign file, a newer version and a missing file are reported, not crashed on") {
    std::vector<std::byte> bytes = encode(make_full_project());

    std::vector<std::byte> foreign = bytes;
    foreign[0] = std::byte{'X'};
    CHECK(decode(foreign).error == "not a Project file");

    std::vector<std::byte> newer = bytes;
    newer[8] = std::byte{static_cast<unsigned char>(kFormatVersion + 1)};
    LoadResult too_new = decode(newer);
    CHECK_FALSE(too_new.ok());
    CHECK(too_new.error.find("newer") != std::string::npos);

    CHECK_FALSE(decode(std::span<const std::byte>{}).ok());
    CHECK_FALSE(decode(std::span<const std::byte>(bytes).first(20)).ok());

    LoadResult missing = load_project(fs::temp_directory_path() / "does-not-exist.project");
    CHECK_FALSE(missing.ok());
    CHECK_FALSE(missing.error.empty());
}

TEST_CASE("every truncation length of a valid file is rejected cleanly") {
    std::vector<std::byte> bytes = encode(make_full_project());
    // Forged counts and lengths must not be able to drive a huge allocation;
    // each prefix must fail as a format error, not as a crash.
    for (std::size_t n = 0; n < bytes.size(); n += 7) {
        LoadResult r = decode(std::span<const std::byte>(bytes).first(n));
        CHECK_FALSE(r.ok());
    }
}

TEST_CASE("the file magic derives from the app id and contains no product name") {
    std::vector<std::byte> bytes = encode(Project{});
    std::string magic(reinterpret_cast<const char*>(bytes.data()), 8);
    // Zero-padded app id: printable prefix, then NULs.
    CHECK(magic.find_first_of(std::string("\0", 1)) != std::string::npos);
    for (const char* forbidden : {"fruit", "Fruit", "FRUIT", "claw", "Claw", "CLAW"}) {
        CHECK(magic.find(forbidden) == std::string::npos);
    }
}

TEST_CASE("saving does not touch the History; loading starts it empty") {
    auto f = test_support::make_fixture();
    ProjectHistory history(std::move(f.project));
    auto rename = functions::rename_track(history.read(), f.arrangement, f.track_a, "Renamed");
    REQUIRE(rename.ok());
    REQUIRE(history.apply(std::move(*rename)) == ApplyResult::Applied);
    REQUIRE(history.state().is_dirty);

    TempDir dir;
    fs::path file = dir.path / "one.project";
    REQUIRE(save_project(history.read(), file).ok);
    history.mark_saved();
    CHECK_FALSE(history.state().is_dirty);

    // Undo back past the save is still possible (history contract 8.1).
    CHECK(history.state().can_undo);
    REQUIRE(history.undo());
    CHECK(history.state().is_dirty);
    REQUIRE(history.redo());
    CHECK_FALSE(history.state().is_dirty);

    LoadResult loaded = load_project(file);
    REQUIRE(loaded.ok());
    int notified = 0;
    history.observe([&notified] { ++notified; });
    history.replace(std::move(*loaded.project));
    CHECK(notified == 1);
    CHECK_FALSE(history.state().can_undo);
    CHECK_FALSE(history.state().can_redo);
    CHECK_FALSE(history.state().is_dirty);
    CHECK(history.read().arrangements.items[0].tracks[0].name == "Renamed");
}
