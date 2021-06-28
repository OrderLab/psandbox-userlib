set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(include)

add_library(psandbox STATIC SHARED
  include/psandbox.h 
  include/hashmap.h
  src/psandbox.c
)
target_link_libraries(psandbox
  Threads::Threads
  ${GLIB_LIBRARIES}
)
