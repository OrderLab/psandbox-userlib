cmake_minimum_required(VERSION 3.13)
project(psandbox_userlib)

set(CMAKE_CXX_STANDARD 14)
SET(CMAKE_CXX_FLAGS -pthread)
FIND_PACKAGE(PkgConfig REQUIRED)
PKG_CHECK_MODULES(GLIB REQUIRED glib-2.0)

include_directories (${GLIB_INCLUDE_DIRS})
LINK_DIRECTORIES(${GLIB_LIBRARY_DIRS})

#add_executable(queue_1 tests/queue_1_case.cpp libs/include/psandbox.h libs/src/psandbox.c
#        libs/include/hashmap.h)
#add_executable(queue tests/queue_case.cpp libs/include/psandbox.h libs/src/psandbox.c
#        libs/include/hashmap.h)
#add_executable(sleep libs/include/psandbox.h libs/src/psandbox.c tests/sleep_case.cpp
#        libs/include/hashmap.h)
#add_executable(mutex libs/include/psandbox.h libs/src/psandbox.c tests/mutex_case.cpp
#        libs/include/hashmap.h )
#add_executable(mutex_pre libs/include/psandbox.h libs/src/psandbox.c tests/mutex_predication.cpp
#        libs/include/hashmap.h )
#add_executable(nest tests/nest_case.cpp libs/include/psandbox.h libs/src/psandbox.c
#        libs/include/hashmap.h)

add_library(psandbox SHARED libs/include/psandbox.h libs/src/psandbox.c
        libs/include/hashmap.h)

#target_link_libraries (queue_1 ${GLIB_LIBRARIES})
#target_link_libraries (queue ${GLIB_LIBRARIES})
#target_link_libraries (sleep ${GLIB_LIBRARIES})
#target_link_libraries (mutex ${GLIB_LIBRARIES})
#target_link_libraries (mutex_pre ${GLIB_LIBRARIES})
#target_link_libraries (nest ${GLIB_LIBRARIES})
target_link_libraries (psandbox ${GLIB_LIBRARIES})