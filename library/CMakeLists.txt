cmake_minimum_required(VERSION 3.22)
project(dpp_utils)

find_package(DPP REQUIRED)
find_package(PostgreSQL QUIET)
if (${PostgreSQL_FOUND})
    message(STATUS "PostgreSQL found, building PG Client")
    set(PG_FILES src/database.cpp)
endif ()

add_library(dpp_utils STATIC src/command_controller.cpp ${PG_FILES})

target_compile_features(dpp_utils PUBLIC cxx_std_17)
target_compile_features(dpp_utils PRIVATE cxx_variadic_templates)

if (DPP_CORO)
    set(CMAKE_CXX_STANDARD 20)
endif ()

if (${PostgreSQL_FOUND})
    target_compile_definitions(dpp_utils PUBLIC DPP_EXPORT_PG)
    target_include_directories(dpp_utils PUBLIC ${PostgreSQL_INCLUDE_DIRS})
    target_link_libraries(dpp_utils PUBLIC ${PostgreSQL_LIBRARIES})
endif()

target_link_libraries(dpp_utils PUBLIC dpp::dpp)
target_include_directories(dpp_utils PUBLIC ${DPP_INCLUDE_DIRECTORIES} ./include)
target_include_directories(dpp_utils PRIVATE ./include/dpp_utils)