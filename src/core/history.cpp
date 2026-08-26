#include "core/history.h"

#include <cassert>
#include <utility>

namespace core {

ProjectHistory::ProjectHistory(Project initial) : project_(std::move(initial)) {}

ApplyResult ProjectHistory::apply(Delta delta) {
    Delta inverse;
    try {
        inverse = apply_delta(project_.musical, delta);
    } catch (const OpError&) {
        // apply_delta rolled back; History and content are unchanged and the
        // future survives (contract section 5.3 rule 3).
        return ApplyResult::Failed;
    }

    if (inverse.ops.empty()) {
        // No change observed: not recorded, redo stack intact (rule 2).
        return ApplyResult::NoChange;
    }

    // Discard-on-branch: truncate the future, permanently, then append.
    entries_.resize(cursor_);
    if (saved_cursor_ > static_cast<std::ptrdiff_t>(cursor_)) saved_cursor_ = -1;

    Entry entry;
    entry.label = delta.label;
    entry.origin = delta.origin;
    entry.bytes = inverse.size_hint();
    entry.delta = std::move(inverse);
    entries_.push_back(std::move(entry));
    cursor_ = entries_.size();

    evict();
    notify();
    return ApplyResult::Applied;
}

bool ProjectHistory::undo() {
    assert(!gesture_ && "undo during a gesture is a programming error");
    if (cursor_ == 0) return false;
    Entry& entry = entries_[cursor_ - 1];
    Delta forward = apply_delta(project_.musical, entry.delta);
    entry.delta = std::move(forward);
    --cursor_;
    notify();
    return true;
}

bool ProjectHistory::redo() {
    assert(!gesture_ && "redo during a gesture is a programming error");
    if (cursor_ == entries_.size()) return false;
    Entry& entry = entries_[cursor_];
    Delta inverse = apply_delta(project_.musical, entry.delta);
    entry.delta = std::move(inverse);
    entry.bytes = entry.delta.size_hint();
    ++cursor_;
    notify();
    return true;
}

void ProjectHistory::begin_gesture(std::string label) {
    assert(!gesture_ && "gestures do not nest");
    gesture_ = Gesture{std::move(label), project_.musical, entries_.size()};
}

void ProjectHistory::end_gesture() {
    assert(gesture_ && "end_gesture without begin_gesture");
    Gesture gesture = std::move(*gesture_);
    gesture_.reset();

    if (entries_.size() <= gesture.first_entry) return;  // nothing applied

    if (project_.musical == gesture.snapshot) {
        // Net no-change: drop the entries; the content is already back where
        // it started, so their inverses restore nothing (contract section 6).
        entries_.resize(gesture.first_entry);
        cursor_ = entries_.size();
        return;
    }

    // Collapse the run into one entry. Undoing the collapsed entry applies the
    // member inverses newest-first, which is exactly the order separate undo
    // presses would have used.
    Entry collapsed;
    collapsed.label = std::move(gesture.label);
    collapsed.origin = entries_[gesture.first_entry].origin;
    for (auto it = entries_.rbegin();
         it != entries_.rend() - static_cast<std::ptrdiff_t>(gesture.first_entry); ++it) {
        for (auto& op : it->delta.ops) collapsed.delta.ops.push_back(std::move(op));
    }
    collapsed.delta.label = collapsed.label;
    collapsed.delta.origin = collapsed.origin;
    collapsed.bytes = collapsed.delta.size_hint();

    entries_.resize(gesture.first_entry);
    entries_.push_back(std::move(collapsed));
    cursor_ = entries_.size();
    evict();
}

HistoryState ProjectHistory::state() const {
    HistoryState s;
    s.can_undo = cursor_ > 0;
    s.can_redo = cursor_ < entries_.size();
    if (s.can_undo) s.undo_label = entries_[cursor_ - 1].label;
    if (s.can_redo) s.redo_label = entries_[cursor_].label;
    s.is_dirty = saved_cursor_ != static_cast<std::ptrdiff_t>(cursor_);
    return s;
}

void ProjectHistory::observe(Listener listener) {
    listeners_.push_back(std::move(listener));
}

void ProjectHistory::mark_saved() {
    saved_cursor_ = static_cast<std::ptrdiff_t>(cursor_);
}

void ProjectHistory::set_limits(std::size_t max_entries, std::size_t max_bytes) {
    max_entries_ = max_entries;
    max_bytes_ = max_bytes;
    evict();
}

void ProjectHistory::notify() {
    for (const auto& listener : listeners_) listener();
}

void ProjectHistory::evict() {
    auto total_bytes = [this] {
        std::size_t sum = 0;
        for (const auto& e : entries_) sum += e.bytes;
        return sum;
    };
    // Oldest end only, and never at or after the cursor — the redo stack is
    // never disturbed by eviction (contract section 9).
    while (cursor_ > 0 &&
           (entries_.size() > max_entries_ || total_bytes() > max_bytes_)) {
        entries_.erase(entries_.begin());
        --cursor_;
        if (saved_cursor_ == 0) {
            saved_cursor_ = -1;  // permanently dirty, conservatively
        } else if (saved_cursor_ > 0) {
            --saved_cursor_;
        }
    }
}

}  // namespace core
