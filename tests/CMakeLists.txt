set(TEST_SOURCES
  mutex_case.cpp
  mutex_predication.cpp
  nest_case.cpp
  queue_1_case.cpp
  queue_case.cpp
  sleep_case.cpp
  ownership_transfer_case_1.cpp
  ownership_transfer_case_2.cpp
)

foreach(TEST_SOURCE_FILE ${TEST_SOURCES})
  get_filename_component(TEST_EXECUTABLE_NAME ${TEST_SOURCE_FILE} NAME_WE)
  add_executable(${TEST_EXECUTABLE_NAME} ${TEST_SOURCE_FILE})
  target_link_libraries(${TEST_EXECUTABLE_NAME} PUBLIC psandbox)
  if (${TEST_SOURCE_FILE} IN_LIST UNIT_TEST_SOURCES)
    add_test(NAME ${TEST_EXECUTABLE_NAME} COMMAND ${TEST_EXECUTABLE_NAME})
  endif()
endforeach(TEST_SOURCE_FILE ${TEST_SOURCES})
