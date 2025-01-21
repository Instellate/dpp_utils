cmake_minimum_required(VERSION 3.22)
project(dpp_utils_test)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(DPP REQUIRED)

add_executable(dpp_utils_test main.cpp)
add_compile_definitions(DPP_EXPORT_PG)

target_link_libraries(dpp_utils_test PUBLIC dpp_utils dpp::dpp)
