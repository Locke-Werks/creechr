# this is the bare minimum to get qt6 + msvc cooperating on windows.
# if you're reading this and it doesn't build, check that:
#   1. you're in a vcvars-initialized shell (or use scripts/build.bat)
#   2. CMAKE_PREFIX_PATH points at your qt6 msvc kit
#   3. you remembered to install the c++ workload, not just visual studio
cmake_minimum_required(VERSION 3.25)
project(creechr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# qt is a moc-and-uic-generating goblin. let cmake handle the goblin.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# dynamic msvc runtime. static is a path to dll-not-found tickets.
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(creechr WIN32
    src/main.cpp
    src/app/creechr_app.h
    src/app/creechr_app.cpp
    src/app/tray_icon.h
    src/app/tray_icon.cpp
    src/render/overlay_window.h
    src/render/overlay_window.cpp
    src/render/sprite_atlas.h
    src/render/sprite_atlas.cpp
    src/render/animator.h
    src/render/animator.cpp
    src/creature/creechr.h
    src/creature/creechr.cpp
    src/creature/state_machine.h
    src/creature/state_machine.cpp
    src/creature/world_context.h
    src/world/window_enumerator.h
    src/world/window_enumerator.cpp
    src/world/window_geometry.h
    src/world/window_geometry.cpp
    src/world/fullscreen_detector.h
    src/world/fullscreen_detector.cpp
    src/heist/hoard.h
    src/heist/hoard.cpp
    src/heist/bitmap_capture.h
    src/heist/bitmap_capture.cpp
    src/ipc/extension_pipe_server.h
    src/ipc/extension_pipe_server.cpp
    src/targets/target_provider.h
    src/targets/window_target_provider.h
    src/targets/window_target_provider.cpp
    src/targets/cursor_target_provider.h
    src/targets/cursor_target_provider.cpp
    src/targets/uia_target_provider.h
    src/targets/uia_target_provider.cpp
    src/targets/extension_target_provider.h
    src/targets/extension_target_provider.cpp
    src/util/logging.h
    src/util/logging.cpp
    src/util/dpi.h
    src/util/dpi.cpp
    src/util/win32_helpers.h
    src/util/win32_helpers.cpp
)

target_compile_definitions(creechr PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

# /W4 because we like warnings. no /WX because we like shipping more.
target_compile_options(creechr PRIVATE
    /W4
    /permissive-
    /Zc:__cplusplus
    /utf-8
    /EHsc
)

target_include_directories(creechr PRIVATE src)

# everything we'll need eventually. linking unused libs costs nothing on
# msvc (the linker drops dead imports), and adding them now means i don't
# have to come back here every other commit. dwmapi is for the "GetWindowRect
# is a damn liar" problem we'll definately hit in the world enumerator.
target_link_libraries(creechr PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    dwmapi
    user32
    gdi32
    shell32
    uiautomationcore
    oleacc
    ole32
    oleaut32
)

# put the binary at the build root so i don't have to go fishing for it
set_target_properties(creechr PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# === creechr-bridge.exe ===
# tiny console binary that chrome / edge spawn via native messaging.
# speaks 4-byte-length-prefix json on stdin/stdout, forwards messages
# to the always-on creechr.exe over a local named pipe (eventually).
# subsystem is console because nm hosts MUST have a stdin/stdout pipe
# attached, and qt is not linked because the bridge is pure stdlib.
add_executable(creechr-bridge
    src/ipc/creechr_bridge_main.cpp
)
target_compile_definitions(creechr-bridge PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
target_compile_options(creechr-bridge PRIVATE
    /W4
    /permissive-
    /Zc:__cplusplus
    /utf-8
    /EHsc
)
set_target_properties(creechr-bridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
