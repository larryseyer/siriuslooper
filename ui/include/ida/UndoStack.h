#pragma once

#include "ida/Constituent.h"
#include "ida/Rational.h"
#include "ida/TapeId.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ida
{

/// Captured-state snapshot recorded with a promotion entry so that undoing
/// the promotion can restore CaptureSession to AwaitingOut with the
/// original in-point intact (white paper Part 14.7 — "undo is sacred",
/// extended to capture-state for the auto-promotion flow). Non-promotion
/// edits omit this; only entries pushed via the three-arg UndoStack::push
/// overload carry it.
struct CaptureRestorePoint
{
    Rational pendingIn;
    TapeId   pendingTape;
};

/// Multi-level undo/redo over immutable Constituent roots (white paper Part
/// 14.7: "undo is sacred"). Because every edit in the system is copy-on-write
/// — a new root that *shares* its untouched children with the previous one —
/// an undo stack is simply a stack of root pointers: no diffs, no replay, no
/// special-case state. The white paper's claim that the immutable-Constituent
/// architecture makes deep undo trivial is encoded here.
///
/// Behaviour matches the rules of Part 14.7:
///   • multi-level, with depth bounded only by configuration;
///   • instantaneous in effect (push/undo/redo are O(1) pointer ops);
///   • visible — every entry carries an optional label describing the edit;
///   • reversible — redo is exactly as accessible as undo, and the redo branch
///     is invalidated only when the user makes a fresh edit after undoing.
class UndoStack
{
public:
    using RootPtr = std::shared_ptr<const Constituent>;

    /// Constructs an empty stack rooted at `initial`. The initial state is the
    /// baseline: it can be undone *to* but not *past*. `maxDepth` caps how
    /// many historical entries are kept; once reached, the oldest entries are
    /// discarded as new edits arrive. Throws std::invalid_argument if
    /// `initial` is null or `maxDepth` is zero.
    UndoStack (RootPtr initial, std::size_t maxDepth = 256);

    /// The current root after every prior edit has been applied.
    const RootPtr& current() const noexcept { return entries_[currentIndex_].root; }

    /// Records a new edit. Truncates the redo branch (the white paper rule:
    /// once a fresh edit lands, the alternate future is gone) and pushes the
    /// new root with an optional descriptive label. Throws std::invalid_argument
    /// if `nextRoot` is null.
    void push (RootPtr nextRoot, std::string label = {});

    /// Records a promotion edit. Behaves exactly like the two-argument push
    /// (truncates redo, advances current) but additionally attaches a
    /// CaptureRestorePoint to the new entry, so undo can restore
    /// CaptureSession::AwaitingOut state on the way back.
    void push (RootPtr nextRoot, std::string label, CaptureRestorePoint restore);

    /// The CaptureRestorePoint of the current entry, or nullopt if the current
    /// entry was a non-promotion edit (or the initial baseline). Stable across
    /// undo/redo: the field belongs to the entry, not to the stack cursor.
    ///
    /// Returned by value (not by reference) so callers can safely interleave
    /// this with push()/undo()/redo(). Any push() may reallocate or erase
    /// from entries_ (depth-cap eviction, redo-branch truncation), which
    /// would invalidate a reference into the vector. The optional is small
    /// (~24-32 bytes, trivially copyable) so the copy is negligible.
    std::optional<CaptureRestorePoint> currentEntryRestorePoint() const noexcept;

    bool canUndo() const noexcept { return currentIndex_ > 0; }
    bool canRedo() const noexcept { return currentIndex_ + 1 < entries_.size(); }

    /// Moves to the previous entry and returns the new current root. Returns
    /// the unchanged current root if no undo is available — undo on an empty
    /// history is a no-op, not an error, because that matches what a
    /// performer's reflex actually does.
    const RootPtr& undo() noexcept;

    /// Moves to the next entry and returns the new current root. No-op if no
    /// redo is available, for the same reason as undo().
    const RootPtr& redo() noexcept;

    /// The label of the edit that would be undone next, or empty if none.
    /// Surfaces white paper 14.7's "visible" requirement.
    const std::string& nextUndoLabel() const noexcept;

    /// The label of the edit that would be redone next, or empty if none.
    const std::string& nextRedoLabel() const noexcept;

    std::size_t depth()       const noexcept { return entries_.size(); }
    std::size_t currentIndex() const noexcept { return currentIndex_; }
    std::size_t maxDepth()    const noexcept { return maxDepth_; }

private:
    struct Entry
    {
        RootPtr     root;
        std::string label;
        std::optional<CaptureRestorePoint> captureRestore;  // promotion entries only
    };

    std::vector<Entry> entries_;
    std::size_t currentIndex_ { 0 };
    std::size_t maxDepth_;
};

} // namespace ida
