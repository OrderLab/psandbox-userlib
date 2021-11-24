set(TEST_SOURCES
  create_benchmark.cpp
  release_benchmark.cpp
        activate_benchmark.cpp
        freeze_benchmark.cpp
)

foreach(TEST_SOURCE_FILE ${TEST_SOURCES})
  get_filename_component(TEST_EXECUTABLE_NAME ${TEST_SOURCE_FILE} NAME_WE)
  add_executable(${TEST_EXECUTABLE_NAME} ${TEST_SOURCE_FILE})
  target_link_libraries(${TEST_EXECUTABLE_NAME} PUBLIC psandbox)
  if (${TEST_SOURCE_FILE} IN_LIST UNIT_TEST_SOURCES)
    add_test(NAME ${TEST_EXECUTABLE_NAME} COMMAND ${TEST_EXECUTABLE_NAME})
  endif()
endforeach(TEST_SOURCE_FILE ${TEST_SOURCES})
