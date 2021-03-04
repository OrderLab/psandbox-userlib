cmake_minimum_required(VERSION 3.13)
project(psandbox_userlib)

set(CMAKE_CXX_STANDARD 14)
SET(CMAKE_CXX_FLAGS -pthread)

add_executable(queue tests/queue_case.cpp libs/include/psandbox.h libs/src/psandbox.c
        libs/include/hashmap.h)
add_executable(sleep libs/include/psandbox.h libs/src/psandbox.c tests/sleep_case.cpp
        libs/include/hashmap.h)
add_executable(mutex libs/include/psandbox.h libs/src/psandbox.c tests/mutex_case.cpp
        libs/include/hashmap.h)