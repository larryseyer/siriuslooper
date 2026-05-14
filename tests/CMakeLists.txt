add_executable(SiriusTests
    RationalTests.cpp
    MeterTests.cpp
    TempoMapTests.cpp
    PositionTests.cpp
    TimeDomainTests.cpp)

target_compile_features(SiriusTests PRIVATE cxx_std_20)

target_link_libraries(SiriusTests PRIVATE
    Sirius::Core
    Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(SiriusTests)
