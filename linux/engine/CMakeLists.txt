# Linux engine/CLI binary (Phase 2) and development smoke targets.
add_executable(core-smoke core_smoke.cpp)
target_link_libraries(core-smoke PRIVATE agentredactor-core)
