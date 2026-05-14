add_executable(SiriusTests SmokeTest.cpp)

target_compile_features(SiriusTests PRIVATE cxx_std_20)

target_link_libraries(SiriusTests PRIVATE Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(SiriusTests)
