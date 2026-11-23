cmake_minimum_required(VERSION 3.16)
project(TopMostDesk LANGUAGES CXX)

enable_language(RC)
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_CURRENT_SOURCE_DIR}/src")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.cpp
    src/app.cpp
    src/menu.cpp
    src/common.cpp
    src/config.cpp
    src/winmgr.cpp
    src/overlay.cpp
    src/groups.cpp
    src/dialogs.cpp
    src/inputbox.cpp
    src/ui_theme.cpp
    src/input_hook.cpp
    src/tray.cpp
    src/app.rc
)

add_executable(TopMostDesk WIN32 ${SOURCES})

target_compile_definitions(TopMostDesk PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if (MSVC)
    target_compile_options(TopMostDesk PRIVATE /utf-8 /W3 /MP)
    set_property(TARGET TopMostDesk PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(TopMostDesk PRIVATE
    user32
    gdi32
    gdiplus
    comdlg32
    shell32
    shlwapi
    dwmapi
    ole32
    advapi32
)

# 静态链接 CRT，单 exe 直接拷走
set_target_properties(TopMostDesk PROPERTIES OUTPUT_NAME "TopMostDesk")
