find_package(Boost REQUIRED system)
find_package(fmt CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(redis main.cpp exec.cpp)
target_compile_features(redis PRIVATE cxx_std_20)
target_compile_definitions(redis PRIVATE BOOST_ASIO_HAS_CO_AWAIT=1)
target_link_libraries(redis PRIVATE Threads::Threads Boost::boost Boost::system fmt::fmt)