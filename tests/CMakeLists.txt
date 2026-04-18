include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
if(MSVC)
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(googletest)

add_subdirectory("gtest")

# DEPRECATED: will migrate to googletest
add_executable(tests)
target_include_directories(tests PUBLIC ${CMAKE_SOURCE_DIR} "${CMAKE_SOURCE_DIR}/utils")
target_sources(tests
    PUBLIC
        "tests.cc"
    PUBLIC
    FILE_SET cxx_modules
    TYPE CXX_MODULES
    BASE_DIRS
        "${CMAKE_SOURCE_DIR}"
    FILES
        "${CMAKE_SOURCE_DIR}/range.ixx"
        "${CMAKE_SOURCE_DIR}/cd/cd.ixx"
        "${CMAKE_SOURCE_DIR}/cd/cdrom.ixx"
        "${CMAKE_SOURCE_DIR}/cd/cd_edc.ixx"
        "${CMAKE_SOURCE_DIR}/cd/cd_scrambler.ixx"
        "${CMAKE_SOURCE_DIR}/crc/crc.ixx"
        "${CMAKE_SOURCE_DIR}/crc/crc16_gsm.ixx"
        "${CMAKE_SOURCE_DIR}/crc/crc32.ixx"
        "${CMAKE_SOURCE_DIR}/dvd/dvd_edc.ixx"
        "${CMAKE_SOURCE_DIR}/utils/file_io.ixx"
        "${CMAKE_SOURCE_DIR}/utils/misc.ixx"
        "${CMAKE_SOURCE_DIR}/utils/strings.ixx"
)
add_test(NAME tests COMMAND tests WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
