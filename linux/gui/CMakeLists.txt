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
