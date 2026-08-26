// ProjectHistory — the module that owns the Project and the only thing that
// mutates its musical content.
// Contract: docs/specs/history-contract.md (linear, Word-style discard on
// branch) and docs/specs/project-data-model.md section 5.1.
//
// Everything here runs on the edit (message) thread; applying, undoing and
// redoing are serialised by that thread. The audio thread never sees this
// module — the engine republishes a baked snapshot after each change.
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/ops.h"

namespace core {

enum class ApplyResult { Applied, NoChange, Failed };

struct HistoryState {
    bool can_undo = false;
    bool can_redo = false;
    std::optional<std::string> undo_label;
    std::optional<std::string> redo_label;
    bool is_dirty = false;
};

class ProjectHistory {
public:
    explicit ProjectHistory(Project initial);

    // The only read surface. Callers outside core hold const views only;
    // the musical content is `read().musical`.
    const Project& read() const { return project_; }

    // Applies one Delta. Contract section 5.3: a no-op is not recorded and
    // does not discard the redo stack; a failure leaves everything unchanged;
    // a real change truncates the future, appends, and advances the cursor.
    ApplyResult apply(Delta delta);

    // False when there is nothing to undo / redo.
    bool undo();
    bool redo();

    // Gesture scope: Deltas applied inside it are applied and published
    // normally but collapse into one History entry at end_gesture. A gesture
    // with no net change is not recorded and does not truncate the future.
    // Gestures do not nest (contract section 6).
    void begin_gesture(std::string label);
    void end_gesture();

    HistoryState state() const;

    // Notified after every observable change to the Project (apply that
    // changed something, undo, redo). Listeners run on the edit thread.
    using Listener = std::function<void()>;
    void observe(Listener listener);

    // Called by an explicit user save (issue #13). Autosaves must not call it.
    void mark_saved();

    // Memory bound (contract section 9): entry count and retained bytes,
    // whichever binds first. Eviction is from the oldest end only.
    void set_limits(std::size_t max_entries, std::size_t max_bytes);

private:
    struct Entry {
        // The Delta stored for this entry: the inverse while the entry is at
        // or below the cursor (applied), the forward Delta while above it.
        Delta delta;
        std::string label;
        Origin origin;
        std::size_t bytes = 0;
    };

    void notify();
    void evict();

    Project project_;
    std::vector<Entry> entries_;
    std::size_t cursor_ = 0;

    // Index the last explicit save corresponds to; -1 once evicted, after
    // which is_dirty is permanently true (contract section 8.3).
    std::ptrdiff_t saved_cursor_ = 0;

    struct Gesture {
        std::string label;
        MusicalContent snapshot;  // Deltas reach nothing else (ADR-060)
        std::size_t first_entry;
    };
    std::optional<Gesture> gesture_;

    std::size_t max_entries_ = 512;
    std::size_t max_bytes_ = 64 * 1024 * 1024;

    std::vector<Listener> listeners_;
};

}  // namespace core
