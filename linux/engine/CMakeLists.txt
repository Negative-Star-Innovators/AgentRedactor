# Linux engine/CLI binary plus development smoke targets.

add_executable(agentredactor
    main.cpp
    control_api_client.cpp
    engine_loc.cpp
)
target_link_libraries(agentredactor PRIVATE agentredactor-core)

# Dev builds link the onnxruntime shared lib from the tarball location; record
# an rpath so the binary runs without LD_LIBRARY_PATH. $ORIGIN comes first so
# the bundled copy next to the binary (AppImage staging) wins when present.
if (ONNXRUNTIME_LIB)
    get_filename_component(AR_ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_LIB}" DIRECTORY)
    set_target_properties(agentredactor PROPERTIES
        BUILD_RPATH "$ORIGIN;${AR_ONNXRUNTIME_LIB_DIR}"
        INSTALL_RPATH "$ORIGIN")
endif()

add_executable(core-smoke core_smoke.cpp)
target_link_libraries(core-smoke PRIVATE agentredactor-core)
set_target_properties(core-smoke PROPERTIES BUILD_RPATH "${AR_ONNXRUNTIME_LIB_DIR}")
