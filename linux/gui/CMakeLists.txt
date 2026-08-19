# Linux GUI — Qt6 Widgets frontend over the engine's loopback control API.
# Mirrors the Windows WinUI 3 app (MainWindow + HomePage surface); a thin
# client — all backend logic lives in core/ and the engine process.

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(CURL REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(agentredactor-gui
    main.cpp
    app_state.cpp
    autostart.cpp
    main_window.cpp
    password_dialog.cpp
    translator_loader.cpp
    tray_icon.cpp
    update_manager.cpp
    ../engine/control_api_client.cpp
    resources.qrc
)

target_include_directories(agentredactor-gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine
)

target_link_libraries(agentredactor-gui PRIVATE
    agentredactor-core
    Qt6::Widgets
    CURL::libcurl
)

if (AR_SELFRELEASE)
    set(VP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/velopack)
    # Arch-aware lib selection: linux_x64 / linux_arm64 (vpk pack gets the
    # matching -r linux-x64/linux-arm64 in build-release.sh).
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(VP_ARCH arm64)
    else()
        set(VP_ARCH x64)
    endif()
    set(VP_SO ${VP_ROOT}/lib/velopack_libc_linux_${VP_ARCH}_gnu.so)
    if (NOT EXISTS ${VP_SO})
        message(FATAL_ERROR "AR_SELFRELEASE=ON but Velopack lib is missing; run linux/fetch-velopack.sh")
    endif()
    target_compile_definitions(agentredactor-gui PRIVATE AR_SELFRELEASE)
    target_include_directories(agentredactor-gui PRIVATE ${VP_ROOT}/include)
    # The vendored .so has no SONAME and no "lib" prefix. Link via a
    # lib-prefixed symlink so DT_NEEDED records just the filename, which
    # $ORIGIN then resolves both in the build tree and the staged AppDir.
    set(VP_LINK_DIR ${CMAKE_BINARY_DIR}/velopack-link)
    file(MAKE_DIRECTORY ${VP_LINK_DIR})
    file(CREATE_LINK ${VP_SO}
        ${VP_LINK_DIR}/libvelopack_libc_linux_${VP_ARCH}_gnu.so SYMBOLIC)
    target_link_directories(agentredactor-gui PRIVATE ${VP_LINK_DIR})
    target_link_libraries(agentredactor-gui PRIVATE velopack_libc_linux_${VP_ARCH}_gnu)
    # Ship the Velopack lib next to the GUI binary (AppDir staging) and find
    # it there at runtime.
    install(FILES ${VP_SO}
        DESTINATION . RENAME libvelopack_libc_linux_${VP_ARCH}_gnu.so)
    set_target_properties(agentredactor-gui PROPERTIES
        BUILD_RPATH "$ORIGIN;${VP_LINK_DIR}"
        INSTALL_RPATH "$ORIGIN")
endif()
