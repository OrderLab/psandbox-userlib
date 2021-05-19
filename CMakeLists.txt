cmake_minimum_required(VERSION 3.13)
project(psandbox_userlib)

set(CMAKE_CXX_STANDARD 14)
SET(CMAKE_CXX_FLAGS -pthread)

add_executable(queue_1 tests/queue_1_case.cpp libs/include/psandbox.h libs/src/psandbox.c
        libs/include/hashmap.h)
add_executable(queue tests/queue_case.cpp libs/include/psandbox.h libs/src/psandbox.c
        libs/include/hashmap.h)
add_executable(sleep libs/include/psandbox.h libs/src/psandbox.c tests/sleep_case.cpp
        libs/include/hashmap.h)
add_executable(mutex libs/include/psandbox.h libs/src/psandbox.c tests/mutex_case.cpp
        libs/include/hashmap.h )
add_executable(mutex_pre libs/include/psandbox.h libs/src/psandbox.c tests/mutex_predication.cpp
        libs/include/hashmap.h )
add_executable(nest tests/nest_case.cpp libs/include/psandbox.h libs/src/psandbox.c
        libs/include/hashmap.h)

add_library(psandbox SHARED libs/include/psandbox.h libs/src/psandbox.c
        libs/include/hashmap.h)