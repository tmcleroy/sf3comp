# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
#
# The game's launcher: the player's ROM set, optional features, controllers, online play; starts the game. Built
# beside the game program (it runs <id> from its own folder). Needs SDL3.
# The launcher itself is the one the recomp ports share (../recomp-common/launcher, a copy of recomp-common's;
# never changed here). What is this toolkit's is src/description.cpp: what it tells that launcher, and the check of
# the ROM set. The game comes from these (the top CMakeLists.txt passes them from games/<id>/game.cfg):
#   GAME_NAME GAME_EXE APP_ID HOME_ENV DISCLAIMER ROM_SET ROM_PARENT ROM_NAME ROM_BIOS ROM_SIMM1..6 PROGRAM_ENCIPHERED
# and, if the game has features whose switch belongs on the first page with "Full screen" (game.cfg, display):
#   DISPLAY_SWITCHES  name|Title|What it says under it, and more of them with || between
# and what it says of other sets it uses (game.cfg, launcher_extra_sets):  EXTRA_SETS_WORDS
# The collection's launcher, for every game (tools/collection.sh configures this folder by itself): COLLECTION=ON and
# GAMES (their ids; the table of them, games.h, is written from their game.cfg files by tools/gamecfg.py
# --collection); GAME_NAME and the rest then name the collection, and GAME_VERSION is its version (VERSION's unless
# said). With UPDATE_REPO (owner/name on GitHub) it updates itself from that repository's releases
# (recomp-common/launcher/src/update.h; tools/release/lib.sh writes each system's manifest).
cmake_minimum_required(VERSION 3.20)
project(cps3recomp_launcher CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(LAUNCHER_GAME_NAME "${GAME_NAME}")
set(LAUNCHER_GAME_EXE "${GAME_EXE}")
set(LAUNCHER_APP_ID "${APP_ID}")
set(LAUNCHER_HOME_ENV "${HOME_ENV}")
set(LAUNCHER_DISCLAIMER "${DISCLAIMER}")
set(LAUNCHER_VERSION "${GAME_VERSION}")
set(LAUNCHER_PORT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/description.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../machine/romset.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../machine/crypt.cpp)
set(LAUNCHER_COLLECTION ${COLLECTION})
if(COLLECTION)
    if(NOT LAUNCHER_VERSION)
        file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION LAUNCHER_VERSION LIMIT_COUNT 1)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION)
    endif()
    if(UPDATE_REPO)
        set(LAUNCHER_UPDATE_REPO ${UPDATE_REPO})
    endif()
endif()
include(../recomp-common/launcher/launcher.cmake)

if(COLLECTION)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(GAMES_HEADER "${CMAKE_CURRENT_BINARY_DIR}/collection/games.h")
    set(GAME_CFGS "")
    foreach(game ${GAMES})
        list(APPEND GAME_CFGS ${CMAKE_CURRENT_SOURCE_DIR}/../../games/${game}/game.cfg)
    endforeach()
    # (written now, and again whenever a game.cfg changes)
    execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gamecfg.py --collection ${GAMES_HEADER} ${GAMES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../.. RESULT_VARIABLE written)
    if(NOT written EQUAL 0)
        message(FATAL_ERROR "the table of games was not written")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GAME_CFGS} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gamecfg.py)
    target_include_directories(launcher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/collection")
else()
    target_compile_definitions(launcher PRIVATE
        LAUNCHER_ROM_SET="${ROM_SET}" LAUNCHER_ROM_PARENT="${ROM_PARENT}" LAUNCHER_ROM_NAME="${ROM_NAME}"
        LAUNCHER_ROM_BIOS="${ROM_BIOS}" LAUNCHER_ROM_SIMM1="${ROM_SIMM1}" LAUNCHER_ROM_SIMM2="${ROM_SIMM2}"
        LAUNCHER_ROM_SIMM3="${ROM_SIMM3}" LAUNCHER_ROM_SIMM4="${ROM_SIMM4}" LAUNCHER_ROM_SIMM5="${ROM_SIMM5}"
        LAUNCHER_ROM_SIMM6="${ROM_SIMM6}" LAUNCHER_PROGRAM_ENCIPHERED="${PROGRAM_ENCIPHERED}")
    if(EXTRA_SETS_WORDS)
        target_compile_definitions(launcher PRIVATE LAUNCHER_EXTRA_SETS_WORDS=" ${EXTRA_SETS_WORDS}")
    endif()
    if(DISPLAY_SWITCHES)
        target_compile_definitions(launcher PRIVATE LAUNCHER_DISPLAY_SWITCHES="${DISPLAY_SWITCHES}")
    endif()
endif()
find_package(ZLIB REQUIRED)
target_link_libraries(launcher PRIVATE ZLIB::ZLIB)
