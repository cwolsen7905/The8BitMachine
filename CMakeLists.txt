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
# SDL2_image  (install via: brew install sdl2_image)
# ---------------------------------------------------------------------------
if(APPLE)
    execute_process(
        COMMAND brew --prefix sdl2_image
        OUTPUT_VARIABLE SDL2_IMAGE_BREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(SDL2_IMAGE_BREW_PREFIX)
        list(APPEND CMAKE_PREFIX_PATH "${SDL2_IMAGE_BREW_PREFIX}")
    endif()
endif()
find_package(SDL2_image REQUIRED)

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
    src/gui/Application_Debugger.cpp
    src/gui/Application_Designer.cpp
    src/gui/Application_Presets.cpp
    src/emulator/core/Bus.cpp
    src/emulator/core/Machine.cpp
    src/emulator/cpu/CPU6502Base.cpp
    src/emulator/cpu/CPU6510.cpp
    src/emulator/cpu/CPU8502.cpp
    src/emulator/cpu/CPU65C02.cpp
    src/emulator/cpu/CPUZ80.cpp
    src/emulator/cpu/Disassembler.cpp
    src/emulator/devices/BankedMemory.cpp
    src/emulator/devices/BankController.cpp
    src/emulator/devices/C64IOSpace.cpp
    src/emulator/devices/BankSelectPort.cpp
    src/emulator/devices/SwitchableRegion.cpp
    src/emulator/devices/CIA6526.cpp
    src/emulator/devices/ULA.cpp
    src/emulator/devices/AppleIIVideo.cpp
    src/emulator/devices/AppleIIIO.cpp
    src/emulator/devices/Memory.cpp
    src/emulator/devices/ROM.cpp
    src/emulator/devices/SID6581.cpp
    src/emulator/devices/VIC6566.cpp
)

if(APPLE)
    list(APPEND SOURCES src/gui/FileDialog.mm)
endif()

if(APPLE)
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST           "${CMAKE_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME          "The 8-Bit Machine"
        MACOSX_BUNDLE_GUI_IDENTIFIER       "com.the8bitmachine.app"
        MACOSX_BUNDLE_BUNDLE_VERSION       "0.30.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.30.0"
    )
else()
    add_executable(${PROJECT_NAME} ${SOURCES})
endif()
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui_lib SDL2::SDL2 SDL2_image::SDL2_image OpenGL::GL nlohmann_json::nlohmann_json)

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

# ---------------------------------------------------------------------------
# Copy runtime resources next to the executable (or into the bundle on macOS)
# ---------------------------------------------------------------------------
if(APPLE)
    set(RESOURCE_DIR "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources")
else()
    set(RESOURCE_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/presets" "${RESOURCE_DIR}/presets"
    COMMENT "Copying presets/ to resources"
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets" "${RESOURCE_DIR}/assets"
    COMMENT "Copying assets/ to resources"
)

set(ROM_PRGS)

# ---------------------------------------------------------------------------
# 6502 ROMs  (ca65 + ld65)
# ---------------------------------------------------------------------------
if(CA65_EXE AND LD65_EXE)
    file(GLOB ROM_SOURCES_6502 "${CMAKE_SOURCE_DIR}/roms/6502/*.s")

    foreach(ASM_SRC ${ROM_SOURCES_6502})
        get_filename_component(BASE ${ASM_SRC} NAME_WE)
        set(ROM_CFG "${CMAKE_SOURCE_DIR}/roms/6502/${BASE}.cfg")

        if(NOT EXISTS "${ROM_CFG}")
            continue()
        endif()

        set(ROM_OBJ "${CMAKE_BINARY_DIR}/roms/6502/${BASE}.o")
        set(ROM_RAW "${CMAKE_BINARY_DIR}/roms/6502/${BASE}.raw")
        set(ROM_PRG "${CMAKE_SOURCE_DIR}/roms/6502/${BASE}.prg")

        add_custom_command(
            OUTPUT  "${ROM_PRG}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/roms/6502"
            COMMAND ${CA65_EXE} -o "${ROM_OBJ}" "${ASM_SRC}"
            COMMAND ${LD65_EXE} -C "${ROM_CFG}" -o "${ROM_RAW}" "${ROM_OBJ}"
            COMMAND python3 "${CMAKE_SOURCE_DIR}/cmake/prepend_prg.py" "00" "02" "${ROM_RAW}" "${ROM_PRG}"
            DEPENDS "${ASM_SRC}" "${ROM_CFG}"
            COMMENT "Assembling 6502/${BASE}.s"
        )

        list(APPEND ROM_PRGS "${ROM_PRG}")
    endforeach()
else()
    message(STATUS "ca65/ld65 not found — 6502 ROMs will not be assembled (brew install cc65)")
endif()

# ---------------------------------------------------------------------------
# Z80 ROMs  (z80asm)
# ---------------------------------------------------------------------------
find_program(Z80ASM_EXE z80asm)

if(Z80ASM_EXE)
    file(GLOB ROM_SOURCES_Z80 "${CMAKE_SOURCE_DIR}/roms/z80/*.s")

    foreach(ASM_SRC ${ROM_SOURCES_Z80})
        get_filename_component(BASE ${ASM_SRC} NAME_WE)
        set(ROM_BIN "${CMAKE_BINARY_DIR}/roms/z80/${BASE}.bin")
        set(ROM_PRG "${CMAKE_SOURCE_DIR}/roms/z80/${BASE}.prg")

        add_custom_command(
            OUTPUT  "${ROM_PRG}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/roms/z80"
            COMMAND ${Z80ASM_EXE} -o "${ROM_BIN}" "${ASM_SRC}"
            COMMAND python3 "${CMAKE_SOURCE_DIR}/cmake/prepend_prg.py" "00" "00" "${ROM_BIN}" "${ROM_PRG}"
            DEPENDS "${ASM_SRC}"
            COMMENT "Assembling z80/${BASE}.s"
        )

        list(APPEND ROM_PRGS "${ROM_PRG}")
    endforeach()
else()
    message(STATUS "z80asm not found — Z80 ROMs will not be assembled (brew install z80asm)")
endif()

add_custom_target(roms DEPENDS ${ROM_PRGS}
    COMMENT "All ROMs assembled"
)
