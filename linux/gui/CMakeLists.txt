# Linux GUI — Qt6 Widgets frontend over the engine's loopback control API.
# Mirrors the Windows WinUI 3 app (MainWindow + HomePage surface); a thin
# client — all backend logic lives in core/ and the engine process.

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(CURL REQUIRED)

# Translation catalogs: i18n/agentredactor_<locale>.ts (synced from the
# Windows resw catalogs by i18n/sync_ts.py) are compiled to .qm with
# lrelease and embedded under :/i18n. When lrelease is unavailable the GUI
# builds English-only with a warning.
file(GLOB AR_TS_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/agentredactor_*.ts)
set(AR_QRC_EXTRAS "")
if (AR_TS_FILES)
    find_package(Qt6 COMPONENTS LinguistTools QUIET)
    if (NOT Qt6_LRELEASE_EXECUTABLE)
        find_program(Qt6_LRELEASE_EXECUTABLE NAMES lrelease lrelease-qt6 pyside6-lrelease
            HINTS /usr/lib/qt6/bin)
    endif()
    if (Qt6_LRELEASE_EXECUTABLE)
        set(AR_QM_FILES "")
        foreach(TS_FILE ${AR_TS_FILES})
            get_filename_component(TS_NAME ${TS_FILE} NAME_WE)
            set(QM_FILE ${CMAKE_CURRENT_BINARY_DIR}/i18n/${TS_NAME}.qm)
            add_custom_command(
                OUTPUT ${QM_FILE}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/i18n
                COMMAND ${Qt6_LRELEASE_EXECUTABLE} ${TS_FILE} -qm ${QM_FILE}
                DEPENDS ${TS_FILE}
                VERBATIM)
            list(APPEND AR_QM_FILES ${QM_FILE})
        endforeach()
        # Generated qrc mapping each .qm to :/i18n/<name>.qm.
        set(AR_I18N_QRC ${CMAKE_CURRENT_BINARY_DIR}/i18n.qrc)
        file(WRITE ${AR_I18N_QRC} "<RCC>\n    <qresource prefix=\"/i18n\">\n")
        foreach(QM_FILE ${AR_QM_FILES})
            get_filename_component(QM_NAME ${QM_FILE} NAME)
            file(APPEND ${AR_I18N_QRC}
                "        <file alias=\"${QM_NAME}\">${QM_FILE}</file>\n")
        endforeach()
        file(APPEND ${AR_I18N_QRC} "    </qresource>\n</RCC>\n")
        set(AR_QRC_EXTRAS ${AR_I18N_QRC} ${AR_QM_FILES})
        # Ordering: the .qm files must exist before AUTORCC compiles the
        # generated qrc (same-target custom commands are not ordered).
        add_custom_target(agentredactor-i18n ALL DEPENDS ${AR_QM_FILES})
        set_source_files_properties(${AR_I18N_QRC} PROPERTIES
            OBJECT_DEPENDS "${AR_QM_FILES}")
    else()
        message(WARNING "lrelease not found — building without translations (English only)")
    endif()
endif()

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
    ${AR_QRC_EXTRAS}
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
