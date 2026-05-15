add_executable(SiriusTests
    RationalTests.cpp
    MeterTests.cpp
    TempoMapTests.cpp
    PositionTests.cpp
    TimeDomainTests.cpp
    ConstituentTests.cpp
    ArrangementTests.cpp
    RepetitionRulesTests.cpp
    PhraseTests.cpp
    TapeTests.cpp
    LockFreeSpscQueueTests.cpp
    RetroactiveRingTests.cpp
    LmcTests.cpp
    SampleClockTests.cpp
    AudioDeviceCalibrationTests.cpp
    MembraneTests.cpp
    LoopRendererTests.cpp
    AsrcTests.cpp
    RenderPipelineTests.cpp
    CapabilityTierTests.cpp
    OverloadProtectionTests.cpp)

target_compile_features(SiriusTests PRIVATE cxx_std_20)

# Sirius::Engine links Sirius::Core publicly, so linking Engine here brings
# Core's headers and library along transitively — no need to list it twice.
target_link_libraries(SiriusTests PRIVATE
    Sirius::Engine
    Sirius::AppCore
    Catch2::Catch2WithMain
    Threads::Threads)

include(Catch)
catch_discover_tests(SiriusTests)
