#include "sirius/Constituent.h"

#include <stdexcept>
#include <utility>

namespace sirius
{

Constituent::Constituent (ConstituentId id, Position conceptualIn, Position conceptualOut)
    : id_ (id), conceptualIn_ (conceptualIn), conceptualOut_ (conceptualOut)
{
    if (conceptualOut_ < conceptualIn_)
        throw std::invalid_argument (
            "sirius::Constituent: conceptualOut must not precede conceptualIn");
}

Rational Constituent::duration() const
{
    return conceptualOut_.wholeNotes() - conceptualIn_.wholeNotes();
}

Constituent Constituent::withBoundaries (Position conceptualIn, Position conceptualOut) const
{
    if (conceptualOut < conceptualIn)
        throw std::invalid_argument (
            "sirius::Constituent: conceptualOut must not precede conceptualIn");

    Constituent next (*this);
    next.conceptualIn_ = conceptualIn;
    next.conceptualOut_ = conceptualOut;
    return next;
}

Constituent Constituent::withName (std::string name) const
{
    Constituent next (*this);
    next.name_ = std::move (name);
    return next;
}

Constituent Constituent::withAnchor (AnchorToParent anchor) const
{
    Constituent next (*this);
    next.anchor_ = anchor;
    return next;
}

Constituent Constituent::withLocalMeter (Meter meter) const
{
    Constituent next (*this);
    next.localMeter_ = meter;
    return next;
}

Constituent Constituent::withoutLocalMeter() const
{
    Constituent next (*this);
    next.localMeter_.reset();
    return next;
}

Constituent Constituent::withLocalTempoMap (TempoMap tempoMap) const
{
    Constituent next (*this);
    next.localTempoMap_ = std::move (tempoMap);
    return next;
}

Constituent Constituent::withoutLocalTempoMap() const
{
    Constituent next (*this);
    next.localTempoMap_.reset();
    return next;
}

Constituent Constituent::withRepetitionRules (RepetitionRules rules) const
{
    Constituent next (*this);
    next.repetitionRules_ = std::move (rules);
    return next;
}

Constituent Constituent::withPhraseMetadata (PhraseMetadata metadata) const
{
    Constituent next (*this);
    next.phraseMetadata_ = std::move (metadata);
    return next;
}

Constituent Constituent::withoutPhraseMetadata() const
{
    Constituent next (*this);
    next.phraseMetadata_.reset();
    return next;
}

Constituent Constituent::withTapeReference (TapeReference reference) const
{
    Constituent next (*this);
    next.tapeReference_ = reference;
    return next;
}

Constituent Constituent::withoutTapeReference() const
{
    Constituent next (*this);
    next.tapeReference_.reset();
    return next;
}

Constituent Constituent::withEffectChain (EffectChain chain) const
{
    Constituent next (*this);
    next.effectChain_ = std::move (chain);
    return next;
}

Constituent Constituent::withoutEffectChain() const
{
    Constituent next (*this);
    next.effectChain_.reset();
    return next;
}

Constituent Constituent::withChildAdded (ChildPtr child) const
{
    if (child == nullptr)
        throw std::invalid_argument ("sirius::Constituent: cannot add a null child");

    Constituent next (*this);
    next.children_.push_back (std::move (child));
    return next;
}

Constituent Constituent::withChildReplaced (std::size_t index, ChildPtr child) const
{
    if (index >= children_.size())
        throw std::out_of_range ("sirius::Constituent: child index out of range");
    if (child == nullptr)
        throw std::invalid_argument ("sirius::Constituent: cannot replace with a null child");

    Constituent next (*this);
    next.children_[index] = std::move (child);
    return next;
}

Constituent Constituent::withChildRemoved (std::size_t index) const
{
    if (index >= children_.size())
        throw std::out_of_range ("sirius::Constituent: child index out of range");

    Constituent next (*this);
    next.children_.erase (next.children_.begin()
                          + static_cast<std::vector<ChildPtr>::difference_type> (index));
    return next;
}

} // namespace sirius
