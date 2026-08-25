// Project save and load (issue #13).
// Contract: docs/specs/project-data-model.md section 7.1, history-contract.md
// section 8, core document sections 8.1 and 9.6.
//
// Two promises this module keeps:
//
// 1. A save is atomic and never destroys the previous good file. The Project
//    is encoded in memory, written to a temporary sibling, flushed to disk,
//    read back and decoded to prove it is loadable, and only then renamed
//    over the target. A crash at any point leaves the target either the old
//    complete file or the new complete file, never a partial one.
//
// 2. The previous good version is retained, not merely not-destroyed
//    (history contract section 8). Before the rename, the existing file is
//    copied to `previous_version_path(file)`, so an unwanted change noticed
//    after a save-and-quit still has a remedy: undo is the in-session one,
//    the retained version is the cross-session one.
//
// Sample audio is embedded in the file, because a linked Sample can go
// missing and section 9.6 says the Studio must never lose a Project.
//
// The file carries no product name anywhere (core document 8.1): the magic
// derives from the build's FCS_APP_ID and the version is a bare integer.
// Saving does not touch the History; the caller marks the History saved
// after an explicit user save (history contract section 8.1 and 8.3).
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/entities.h"

namespace persistence {

// The current on-disk format version. Loads refuse a newer version rather
// than guess at it; older versions are migrated on read when they exist.
inline constexpr std::uint32_t kFormatVersion = 1;

struct SaveResult {
    bool ok = false;
    std::string error;  // empty when ok
};

struct LoadResult {
    std::optional<core::Project> project;
    std::string error;  // empty when project is set

    bool ok() const { return project.has_value(); }
};

// Where the retained previous good version of `file` lives: a sibling with
// ".previous" appended, so it stays next to the file it backs up.
std::filesystem::path previous_version_path(const std::filesystem::path& file);

// Writes `project` to `file` atomically, retaining any existing `file` at
// `previous_version_path(file)` first. On any failure the existing file is
// untouched and the error says why.
SaveResult save_project(const core::Project& project, const std::filesystem::path& file);

// Reads `file`. A truncated, corrupted, foreign or newer-version file is
// reported, never crashed on; the caller may then offer the retained
// previous version.
LoadResult load_project(const std::filesystem::path& file);

// The pure halves of save and load, exposed so the format can be tested
// without touching disk.
std::vector<std::byte> encode(const core::Project& project);
LoadResult decode(std::span<const std::byte> bytes);

}  // namespace persistence
