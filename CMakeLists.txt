cmake_minimum_required(VERSION 3.10)
project(psandbox_userlib)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/module ${CMAKE_MODULE_PATH})

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

SET(TRACE_DEBUG ON CACHE BOOL "PerfSandbox trace for debugging")

SET(DISABLE_PSANDBOX ON CACHE BOOL "PerfSandbox disable")


find_package(ClangFormat)


include_directories(${GLIB_INCLUDE_DIRS})
link_directories(${GLIB_LIBRARY_DIRS})

include_directories(libs/include)
link_directories(libs)

add_subdirectory(libs)
add_subdirectory(tests)

