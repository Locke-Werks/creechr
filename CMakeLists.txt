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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(creechr WIN32
    src/main.cpp
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
