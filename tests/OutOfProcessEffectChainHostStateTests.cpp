// Tests for OutOfProcessEffectChainHost slot-keyed descriptor + state
// accessors (M8 S2). Drive the synthetic CLAP through a single-slot
// chain, then ask for descriptor + state bytes.
#include "ida/OutOfProcessEffectChainHost.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <chrono>
#include <thread>

#ifndef IDA_HOST_BINARY_PATH
    #error "IDA_HOST_BINARY_PATH required"
#endif
#ifndef IDA_SYNTHETIC_CLAP_PATH
    #error "IDA_SYNTHETIC_CLAP_PATH required"
#endif

using ida::OutOfProcessEffectChainHost;

namespace
{
    ida::EffectChain singleEntryChain (const std::string& uniqueId)
    {
        ida::EffectChainEntry entry;
        entry.descriptor.format   = ida::PluginFormat::Clap;
        entry.descriptor.uniqueId = uniqueId;
        entry.descriptor.version  = "1.0.0";
        entry.descriptor.name     = "Sirius Synthetic Identity";
        entry.descriptor.filePath = IDA_SYNTHETIC_CLAP_PATH;
        entry.displayName         = "Synthetic";
        entry.bypassed            = false;
        return ida::EffectChain{}.withAppended (entry);
    }
}

TEST_CASE ("descriptorForSlot returns the configured descriptor",
           "[chain-host-state]")
{
    OutOfProcessEffectChainHost host;
    host.configureBus (1, singleEntryChain ("com.sirius.synthetic.identity"),
                       juce::File (IDA_HOST_BINARY_PATH),
                       juce::File (IDA_SYNTHETIC_CLAP_PATH));
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    const auto desc = host.descriptorForSlot (1, 0);
    REQUIRE (desc.has_value());
    CHECK (desc->uniqueId == "com.sirius.synthetic.identity");

    const auto missing = host.descriptorForSlot (1, 99);
    CHECK_FALSE (missing.has_value());
}

TEST_CASE ("stateBlobForSlot returns the synthetic CLAP's payload",
           "[chain-host-state]")
{
    OutOfProcessEffectChainHost host;
    host.configureBus (2, singleEntryChain ("com.sirius.synthetic.identity"),
                       juce::File (IDA_HOST_BINARY_PATH),
                       juce::File (IDA_SYNTHETIC_CLAP_PATH));
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    const auto state = host.stateBlobForSlot (2, 0);
    REQUIRE (state.has_value());
    REQUIRE (state->size() == 4);
    CHECK (std::to_integer<int> ((*state)[0]) == 0xCA);
}

TEST_CASE ("stateBlobForSlot returns nullopt for missing slot",
           "[chain-host-state]")
{
    OutOfProcessEffectChainHost host;
    CHECK_FALSE (host.stateBlobForSlot (999, 0).has_value());
}
