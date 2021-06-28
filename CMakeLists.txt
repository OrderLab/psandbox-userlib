cmake_minimum_required(VERSION 3.13)
project(psandbox_userlib)

set(CMAKE_CXX_STANDARD 11)
FIND_PACKAGE(PkgConfig REQUIRED)
PKG_CHECK_MODULES(GLIB REQUIRED glib-2.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

include_directories(${GLIB_INCLUDE_DIRS})
link_directories(${GLIB_LIBRARY_DIRS})

include_directories(libs/include)
link_directories(libs)

add_subdirectory(libs)
add_subdirectory(tests)

