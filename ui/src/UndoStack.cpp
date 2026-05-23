#include "ida/UndoStack.h"

#include <stdexcept>
#include <utility>

namespace ida
{

namespace
{
    const std::string emptyLabel {};
}

UndoStack::UndoStack (RootPtr initial, std::size_t maxDepth)
    : maxDepth_ (maxDepth)
{
    if (initial == nullptr)
        throw std::invalid_argument ("ida::UndoStack: initial root must not be null");
    if (maxDepth == 0)
        throw std::invalid_argument ("ida::UndoStack: maxDepth must be at least 1");

    entries_.push_back ({ std::move (initial), {}, std::nullopt });
}

void UndoStack::push (RootPtr nextRoot, std::string label)
{
    if (nextRoot == nullptr)
        throw std::invalid_argument ("ida::UndoStack: pushed root must not be null");

    // A fresh edit invalidates any alternate future.
    if (currentIndex_ + 1 < entries_.size())
        entries_.erase (entries_.begin()
                        + static_cast<std::vector<Entry>::difference_type> (currentIndex_ + 1),
                        entries_.end());

    entries_.push_back ({ std::move (nextRoot), std::move (label), std::nullopt });
    currentIndex_ = entries_.size() - 1;

    // Cap depth from the *oldest* end so the most recent history survives.
    while (entries_.size() > maxDepth_)
    {
        entries_.erase (entries_.begin());
        --currentIndex_;
    }
}

void UndoStack::push (RootPtr nextRoot, std::string label, CaptureRestorePoint restore)
{
    if (nextRoot == nullptr)
        throw std::invalid_argument ("ida::UndoStack: pushed root must not be null");

    // A fresh edit invalidates any alternate future.
    if (currentIndex_ + 1 < entries_.size())
        entries_.erase (entries_.begin()
                        + static_cast<std::vector<Entry>::difference_type> (currentIndex_ + 1),
                        entries_.end());

    entries_.push_back ({ std::move (nextRoot), std::move (label), std::move (restore) });
    currentIndex_ = entries_.size() - 1;

    // Cap depth from the *oldest* end so the most recent history survives.
    while (entries_.size() > maxDepth_)
    {
        entries_.erase (entries_.begin());
        --currentIndex_;
    }
}

std::optional<CaptureRestorePoint>
UndoStack::currentEntryRestorePoint() const noexcept
{
    return entries_[currentIndex_].captureRestore;
}

const UndoStack::RootPtr& UndoStack::undo() noexcept
{
    if (canUndo())
        --currentIndex_;
    return current();
}

const UndoStack::RootPtr& UndoStack::redo() noexcept
{
    if (canRedo())
        ++currentIndex_;
    return current();
}

const std::string& UndoStack::nextUndoLabel() const noexcept
{
    return canUndo() ? entries_[currentIndex_].label : emptyLabel;
}

const std::string& UndoStack::nextRedoLabel() const noexcept
{
    return canRedo() ? entries_[currentIndex_ + 1].label : emptyLabel;
}

} // namespace ida
