include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.4
)
FetchContent_MakeAvailable(Catch2)

add_executable(unit_tests
  test_bplus_tree.cpp
  test_disk_manager.cpp
  test_buffer_pool.cpp
  test_disk_bplus_tree.cpp
  test_wal.cpp
)
target_link_libraries(unit_tests PRIVATE storage_engine Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(unit_tests)

add_test(NAME crash_recovery
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/crash_test.sh $<TARGET_FILE:crash_harness>)
