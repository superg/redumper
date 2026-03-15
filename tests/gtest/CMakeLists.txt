add_executable(gtest_interval_set)

target_include_directories(gtest_interval_set PUBLIC ${CMAKE_SOURCE_DIR} "${CMAKE_SOURCE_DIR}/utils")
target_sources(gtest_interval_set
    PUBLIC
        "test_interval_set.cc"
    PUBLIC
    FILE_SET cxx_modules
    TYPE CXX_MODULES
    BASE_DIRS
        "${CMAKE_SOURCE_DIR}"
    FILES
        "${CMAKE_SOURCE_DIR}/interval_set.ixx"
)

target_link_libraries(gtest_interval_set GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(gtest_interval_set)
