#include "ida/TapeColoringSink.h"

#include "fx/TapeColorAdapter.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ida
{

TapeColoringSink::TapeColoringSink (ITapeSink* innerSink, double sampleRate, int maxBlockSize)
    : innerSink_   (innerSink),
      sampleRate_  (sampleRate),
      maxBlockSize_ (maxBlockSize),
      scratchLeft_  (static_cast<std::size_t> (std::max (1, maxBlockSize)), 0.0f),
      scratchRight_ (static_cast<std::size_t> (std::max (1, maxBlockSize)), 0.0f)
{
    entries_.reserve (static_cast<std::size_t> (kMaxTapes));
}

TapeColoringSink::~TapeColoringSink() = default;

const TapeColoringSink::Entry* TapeColoringSink::findEntry (TapeId tape) const noexcept
{
    const auto id = tape.value();
    for (const auto& e : entries_)
        if (e->tapeId == id)
            return e.get();
    return nullptr;
}

TapeColoringSink::Entry* TapeColoringSink::findEntry (TapeId tape) noexcept
{
    const auto id = tape.value();
    for (auto& e : entries_)
        if (e->tapeId == id)
            return e.get();
    return nullptr;
}

bool TapeColoringSink::addTape (TapeId tape)
{
    if (findEntry (tape) != nullptr) return false;
    if (entries_.size() >= static_cast<std::size_t> (kMaxTapes)) return false;

    auto entry     = std::make_unique<Entry>();
    entry->tapeId  = tape.value();
    entry->adapter = std::make_unique<TapeColorAdapter>();
    entry->adapter->prepare (sampleRate_, maxBlockSize_);
    // mode is None by default (operator design lock: default-OFF everywhere).
    entries_.push_back (std::move (entry));
    return true;
}

bool TapeColoringSink::removeTape (TapeId tape)
{
    const auto id = tape.value();
    const auto it = std::find_if (entries_.begin(), entries_.end(),
                                  [id] (const std::unique_ptr<Entry>& e) { return e->tapeId == id; });
    if (it == entries_.end()) return false;
    entries_.erase (it);
    return true;
}

bool TapeColoringSink::setMode (TapeId tape, TapeColorMode mode)
{
    auto* e = findEntry (tape);
    if (e == nullptr) return false;
    e->mode.store (mode, std::memory_order_release);
    return true;
}

TapeColorMode TapeColoringSink::modeFor (TapeId tape) const noexcept
{
    const auto* e = findEntry (tape);
    return e ? e->mode.load (std::memory_order_acquire) : TapeColorMode::None;
}

void TapeColoringSink::setSampleRate (double sampleRate, int maxBlockSize)
{
    // Guard A — drop bogus values. Some MainComponent rebuild paths can fire
    // setSampleRate before the audio callback has started, with
    // currentSampleRate() == 0. Forwarding that to juce::dsp::Convolution::
    // prepare hangs forever inside its IR resampler (divide-by-zero ratio).
    if (sampleRate <= 0.0 || maxBlockSize <= 0)
        return;

    // Guard B — no-op when params are unchanged. addTape already prepared
    // each adapter at the device's rate; re-prepare on a same-rate rebuild
    // is wasted work AND re-enters juce::dsp::Convolution::prepare while
    // its IR-load worker (kicked off by the first prepare) is still in
    // flight — racy and (in practice) wedges the engine factory.
    if (sampleRate == sampleRate_ && maxBlockSize == maxBlockSize_)
        return;

    sampleRate_   = sampleRate;
    maxBlockSize_ = maxBlockSize;

    const auto newCapacity = static_cast<std::size_t> (maxBlockSize);
    if (scratchLeft_.size() < newCapacity)
    {
        scratchLeft_.assign  (newCapacity, 0.0f);
        scratchRight_.assign (newCapacity, 0.0f);
    }

    for (auto& e : entries_)
        if (e->adapter != nullptr)
            e->adapter->prepare (sampleRate, maxBlockSize);
}

lsfx::tapecolor::TapeColorConfig& TapeColoringSink::scratchConfig (TapeId tape)
{
    auto* e = findEntry (tape);
    if (e == nullptr)
        throw std::out_of_range ("TapeColoringSink::scratchConfig: tape not registered");
    return e->adapter->scratchConfig();
}

void TapeColoringSink::commitConfig (TapeId tape)
{
    auto* e = findEntry (tape);
    if (e == nullptr)
        throw std::out_of_range ("TapeColoringSink::commitConfig: tape not registered");
    e->adapter->commitConfig();
}

const lsfx::tapecolor::TapeColorConfig& TapeColoringSink::liveConfig (TapeId tape) const
{
    const auto* e = findEntry (tape);
    if (e == nullptr)
        throw std::out_of_range ("TapeColoringSink::liveConfig: tape not registered");
    return e->adapter->liveConfig();
}

void TapeColoringSink::deliverTapeBlock (TapeId tape, const float* left, const float* right,
                                         int numSamples) noexcept
{
    if (innerSink_ == nullptr || left == nullptr || right == nullptr || numSamples <= 0)
        return;

    // Linear scan — matches InputMixer::tapeNodeFor's pattern. kMaxTapes
    // bounds the worst case; n is typically a handful per project.
    Entry* selected = nullptr;
    const auto id   = tape.value();
    for (auto& e : entries_)
        if (e->tapeId == id) { selected = e.get(); break; }

    if (selected == nullptr
        || selected->mode.load (std::memory_order_acquire) != TapeColorMode::BeforeWrite
        || numSamples > maxBlockSize_)
    {
        // None / AfterRead / unknown / oversize block → bit-identical
        // forward. Oversize is a miss (defensive — InputMixer respects
        // kMaxScratchSamples, but a future upstream change shouldn't
        // silently truncate). The downstream sink owns oversize handling.
        innerSink_->deliverTapeBlock (tape, left, right, numSamples);
        return;
    }

    // BeforeWrite: color into scratch, forward scratch downstream.
    auto* scL = scratchLeft_.data();
    auto* scR = scratchRight_.data();
    for (int i = 0; i < numSamples; ++i) { scL[i] = left[i]; scR[i] = right[i]; }

    const float* inPtrs [2] = { scL, scR };
    float*       outPtrs[2] = { scL, scR }; // in-place
    const bool   processed  = selected->adapter->process (inPtrs, outPtrs, /*numChannels=*/ 2, numSamples);

    if (! processed)
    {
        // Adapter missed (un-prepared shouldn't happen here; defensive).
        // Forward original — never deliver partial coloring.
        innerSink_->deliverTapeBlock (tape, left, right, numSamples);
        return;
    }

    innerSink_->deliverTapeBlock (tape, scL, scR, numSamples);
}

} // namespace ida
