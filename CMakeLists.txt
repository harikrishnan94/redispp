cmake_minimum_required(VERSION 3.23)

project(
    RedisPP
    DESCRIPTION "C++ Implementation of Redis server"
    LANGUAGES C CXX
    VERSION 0.1)

find_program(CCACHE_PROGRAM ccache)

if(CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

add_subdirectory(src)