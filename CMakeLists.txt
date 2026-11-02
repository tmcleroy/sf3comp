# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy

cmake_minimum_required(VERSION 3.28)
project(sf3comp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

set(GAME "sfiii3" CACHE STRING "Game id (directory under games/)")
set(GAME_DIR "${CMAKE_SOURCE_DIR}/games/${GAME}")
set(WORK_DIR "${CMAKE_SOURCE_DIR}/work/${GAME}")
set(CR "${CMAKE_SOURCE_DIR}/cps3recomp")

find_package(ZLIB REQUIRED)
if (APPLE)
    find_package(SDL3 3.4.16 REQUIRED CONFIG)
else ()
    find_package(SDL3 REQUIRED CONFIG)
endif ()
file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" RECOMP_VERSION LIMIT_COUNT 1)

# game.cfg's values as GAME_<KEY> strings
add_custom_command(OUTPUT "${WORK_DIR}/game_cfg.h"
    COMMAND python3 "${CR}/tools/gamecfg.py" --header "${WORK_DIR}/game_cfg.h" ${GAME}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS "${GAME_DIR}/game.cfg" "${CR}/tools/gamecfg.py")
add_custom_command(OUTPUT "${WORK_DIR}/game_info.h"
    COMMAND python3 "${CR}/tools/gamecfg.py" --header "${WORK_DIR}/game_info.h" ${GAME}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS "${GAME_DIR}/game.cfg" "${CR}/tools/gamecfg.py")
add_custom_target(game_cfg DEPENDS "${WORK_DIR}/game_cfg.h" "${WORK_DIR}/game_info.h")

set(MACHINE_SOURCES
    ${CR}/machine/sh2.cpp ${CR}/machine/machine.cpp ${CR}/machine/crypt.cpp ${CR}/machine/romset.cpp
    ${CR}/machine/state.cpp ${CR}/video/renderer.cpp ${CR}/sound/pcm.cpp)
add_library(cps3machine STATIC ${MACHINE_SOURCES})
target_include_directories(cps3machine PUBLIC ${CR}/machine ${CR}/video ${CR}/sound ${CR}/runtime ${CR}/port)
target_link_libraries(cps3machine PUBLIC ZLIB::ZLIB)
target_compile_options(cps3machine PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_options(cps3machine PUBLIC -fsigned-char)

add_executable(${GAME}-headless ${CR}/port/headless.cpp)
add_dependencies(${GAME}-headless game_cfg)
target_include_directories(${GAME}-headless PRIVATE "${WORK_DIR}")
target_link_libraries(${GAME}-headless PRIVATE cps3machine)

# The game, with its window, menu and features (port/, taken from cps2recomp; recomp-common/ is the shared copy)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${GAME_DIR}/src/*.cpp")
if (NOT GAME_SOURCES)
    set(GAME_SOURCES ${CR}/runtime/no_game.cpp)
endif ()
set(PORT_SOURCES
    ${CR}/port/main.cpp ${CR}/port/frontend_sdl.cpp ${CR}/port/present.cpp ${CR}/port/shading.cpp ${CR}/port/zoom.cpp
    ${CR}/port/menu.cpp ${CR}/port/hud.cpp ${CR}/port/touch.cpp ${CR}/port/netplay.cpp
    ${CR}/recomp-common/launcher/src/online.cpp ${CR}/recomp-common/launcher/src/run_program.cpp
    ${CR}/recomp-common/menu.cpp ${CR}/recomp-common/settings.cpp
    ${CR}/runtime/game.cpp ${CR}/runtime/system.cpp ${GAME_SOURCES})
if (APPLE)
    enable_language(OBJCXX)
    list(APPEND PORT_SOURCES ${CR}/port/front_macos.mm)
endif ()
add_executable(${GAME} ${PORT_SOURCES})
add_dependencies(${GAME} game_cfg)
if (WIN32)
    set_target_properties(${GAME} PROPERTIES WIN32_EXECUTABLE ON)
endif ()
target_include_directories(${GAME} PRIVATE ${CR}/runtime ${CR}/port "${WORK_DIR}" "${GAME_DIR}")
target_link_libraries(${GAME} PRIVATE cps3machine SDL3::SDL3)
if (APPLE)
    target_link_libraries(${GAME} PRIVATE "-framework AppKit")
endif ()
if (WIN32)
    target_link_libraries(${GAME} PRIVATE ws2_32 iphlpapi)
endif ()
target_compile_options(${GAME} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-unused-label -Wno-unused-variable)
target_compile_definitions(${GAME} PRIVATE RECOMP_VERSION="${RECOMP_VERSION}")
