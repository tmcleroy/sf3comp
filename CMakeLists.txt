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

# game.cfg's values as GAME_<KEY> strings
add_custom_command(OUTPUT "${WORK_DIR}/game_cfg.h"
    COMMAND python3 "${CR}/tools/gamecfg.py" --header "${WORK_DIR}/game_cfg.h" ${GAME}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS "${GAME_DIR}/game.cfg" "${CR}/tools/gamecfg.py")
add_custom_target(game_cfg DEPENDS "${WORK_DIR}/game_cfg.h")

set(MACHINE_SOURCES
    ${CR}/machine/sh2.cpp ${CR}/machine/machine.cpp ${CR}/machine/crypt.cpp ${CR}/machine/romset.cpp
    ${CR}/video/renderer.cpp ${CR}/sound/pcm.cpp)
add_library(cps3machine STATIC ${MACHINE_SOURCES})
target_include_directories(cps3machine PUBLIC ${CR}/machine ${CR}/video ${CR}/sound ${CR}/runtime ${CR}/port)
target_link_libraries(cps3machine PUBLIC ZLIB::ZLIB)
target_compile_options(cps3machine PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_options(cps3machine PUBLIC -fsigned-char)

add_executable(${GAME}-headless ${CR}/port/headless.cpp)
add_dependencies(${GAME}-headless game_cfg)
target_include_directories(${GAME}-headless PRIVATE "${WORK_DIR}")
target_link_libraries(${GAME}-headless PRIVATE cps3machine)
