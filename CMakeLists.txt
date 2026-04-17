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

# ---------------------------------------------------------------------------
# nlohmann/json  (header-only; fetched at configure time)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

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
    src/emulator/core/Bus.cpp
    src/emulator/core/Machine.cpp
    src/emulator/cpu/CPU6502Base.cpp
    src/emulator/cpu/CPU6510.cpp
    src/emulator/cpu/CPU8502.cpp
    src/emulator/cpu/CPU65C02.cpp
    src/emulator/cpu/Disassembler.cpp
    src/emulator/devices/BankedMemory.cpp
    src/emulator/devices/BankController.cpp
    src/emulator/devices/C64IOSpace.cpp
    src/emulator/devices/BankSelectPort.cpp
    src/emulator/devices/SwitchableRegion.cpp
    src/emulator/devices/CIA6526.cpp
    src/emulator/devices/Memory.cpp
    src/emulator/devices/ROM.cpp
    src/emulator/devices/SID6581.cpp
    src/emulator/devices/VIC6566.cpp
)

if(APPLE)
    list(APPEND SOURCES src/gui/FileDialog.mm)
endif()

add_executable(${PROJECT_NAME} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui_lib SDL2::SDL2 OpenGL::GL nlohmann_json::nlohmann_json)

if(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE GL_SILENCE_DEPRECATION)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
endif()

# ---------------------------------------------------------------------------
# ROM assembly target  (requires cc65: brew install cc65)
#   cmake --build build --target roms
# ---------------------------------------------------------------------------
find_program(CA65_EXE ca65)
find_program(LD65_EXE ld65)

if(CA65_EXE AND LD65_EXE)
    file(GLOB ROM_SOURCES "${CMAKE_SOURCE_DIR}/roms/*.s")
    set(ROM_PRGS)

    foreach(ASM_SRC ${ROM_SOURCES})
        get_filename_component(BASE ${ASM_SRC} NAME_WE)
        set(ROM_CFG "${CMAKE_SOURCE_DIR}/roms/${BASE}.cfg")

        if(NOT EXISTS "${ROM_CFG}")
            continue()
        endif()

        set(ROM_OBJ "${CMAKE_BINARY_DIR}/roms/${BASE}.o")
        set(ROM_RAW "${CMAKE_BINARY_DIR}/roms/${BASE}.raw")
        set(ROM_PRG "${CMAKE_SOURCE_DIR}/roms/${BASE}.prg")

        add_custom_command(
            OUTPUT  "${ROM_PRG}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/roms"
            COMMAND ${CA65_EXE} -o "${ROM_OBJ}" "${ASM_SRC}"
            COMMAND ${LD65_EXE} -C "${ROM_CFG}" -o "${ROM_RAW}" "${ROM_OBJ}"
            COMMAND sh -c "{ printf '\\x00\\x02'; cat '${ROM_RAW}'; } > '${ROM_PRG}'"
            DEPENDS "${ASM_SRC}" "${ROM_CFG}"
            COMMENT "Assembling ${BASE}.s"
        )

        list(APPEND ROM_PRGS "${ROM_PRG}")
    endforeach()

    add_custom_target(roms DEPENDS ${ROM_PRGS}
        COMMENT "All ROMs assembled"
    )
else()
    add_custom_target(roms
        COMMAND ${CMAKE_COMMAND} -E echo "ca65/ld65 not found — install with: brew install cc65"
    )
endif()
