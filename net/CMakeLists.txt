# =============================================================================
# SiriusNet — the ensemble layer (white paper Part XII)
# =============================================================================
# Three components, all JUCE-free, headless-testable, no real networking — the
# actual sockets/transport are operator-deferred per project conventions:
#
#   * LmcElection — Marzullo interval intersection + tier dominance + anchor
#     override. The white paper's central claim about distributed clock
#     election made executable (Part 12.3–12.4).
#
#   * SessionMerge — CRDT union semantics for Constituent graphs and tape
#     hashes. Pins down the architectural claim that "conflicts are
#     structurally impossible" (Part 12.6).
#
#   * Transport — the data model for ensemble coordination messages: LMC
#     announcements, marker events, transport-state changes (Part 12.2).
# =============================================================================

add_library(SiriusNet STATIC
    src/LmcElection.cpp
    src/SessionMerge.cpp)

target_include_directories(SiriusNet PUBLIC include)

target_compile_features(SiriusNet PUBLIC cxx_std_20)

target_link_libraries(SiriusNet PUBLIC Sirius::Core)

add_library(Sirius::Net ALIAS SiriusNet)
