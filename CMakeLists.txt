cmake_minimum_required(VERSION 3.30.2)

project(Main)

set(TARGET_PROJECT "game" CACHE STRING "game, font or sdf")

if(TARGET_PROJECT STREQUAL "game")
    add_subdirectory(src/game)
# TODO
elseif(TARGET_PROJECT STREQUAL "font")
    add_subdirectory(src/font)
elseif(TARGET_PROJECT STREQUAL "sdf")
    add_subdirectory(src/sdf)
else()
    message(FATAL_ERROR "Please set -DTARGET_PROJECT to game, font or sdf")
endif()
