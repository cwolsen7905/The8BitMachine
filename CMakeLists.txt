cmake_minimum_required(VERSION 3.20)
project(the-8-bit-machine VERSION 0.1.0 LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# SDL2  (install via: brew install sdl2)
# ---------------------------------------------------------------------------
if(APPLE)
    # Support both Apple Silicon (/opt/homebrew) and Intel (/usr/local) Macs
    execute_process(
        COMMAND brew --prefix sdl2
        OUTPUT_VARIABLE SDL2_BREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(SDL2_BREW_PREFIX)
        list(APPEND CMAKE_PREFIX_PATH "${SDL2_BREW_PREFIX}")
    endif()
endif()

find_package(SDL2 REQUIRED)

# ---------------------------------------------------------------------------
# OpenGL
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# Dear ImGui  (fetched at configure time)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.6-docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC SDL2::SDL2 OpenGL::GL)

if(APPLE)
    target_compile_definitions(imgui_lib PUBLIC GL_SILENCE_DEPRECATION)
endif()

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/gui/Application.cpp
    src/emulator/CPU8502.cpp
    src/emulator/CIA6526.cpp
    src/emulator/Disassembler.cpp
    src/emulator/Memory.cpp
    src/emulator/Bus.cpp
    src/emulator/Machine.cpp
)

if(APPLE)
    list(APPEND SOURCES src/gui/FileDialog.mm)
endif()

add_executable(${PROJECT_NAME} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui_lib SDL2::SDL2 OpenGL::GL)

if(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE GL_SILENCE_DEPRECATION)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
endif()
