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

find_package(Boost REQUIRED system)
find_package(Threads REQUIRED)

add_executable(redis redis.cpp)
target_compile_features(redis PRIVATE cxx_std_20)
target_compile_definitions(redis PRIVATE BOOST_ASIO_HAS_CO_AWAIT=1)
target_link_libraries(redis PRIVATE Threads::Threads Boost::boost Boost::system)