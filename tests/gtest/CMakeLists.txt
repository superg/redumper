include(GoogleTest)


# add_gtest(<target> SOURCE <test.cc> MODULES <module1.ixx> [module2.ixx ...] [WORKING_DIRECTORY <dir>])
function(add_gtest target)
    cmake_parse_arguments(ARG "" "SOURCE;WORKING_DIRECTORY" "MODULES" ${ARGN})

    add_executable(${target})
    target_include_directories(${target} PUBLIC ${CMAKE_SOURCE_DIR} "${CMAKE_SOURCE_DIR}/utils")
    target_sources(${target}
        PUBLIC
            ${ARG_SOURCE}
        PUBLIC
        FILE_SET cxx_modules
        TYPE CXX_MODULES
        BASE_DIRS
            "${CMAKE_SOURCE_DIR}"
        FILES
            ${ARG_MODULES}
    )
    target_link_libraries(${target} GTest::gtest_main)

    if(ARG_WORKING_DIRECTORY)
        gtest_discover_tests(${target} WORKING_DIRECTORY "${ARG_WORKING_DIRECTORY}" DISCOVERY_MODE PRE_TEST)
    else()
        gtest_discover_tests(${target} DISCOVERY_MODE PRE_TEST)
    endif()
endfunction()


add_gtest(gtest_interval_set
    SOURCE  "test_interval_set.cc"
    MODULES "${CMAKE_SOURCE_DIR}/interval_set.ixx"
)

add_gtest(gtest_scale
    SOURCE  "test_scale.cc"
    MODULES "${CMAKE_SOURCE_DIR}/utils/misc.ixx"
)

add_gtest(gtest_lbamsf
    SOURCE  "test_lbamsf.cc"
    MODULES
        "${CMAKE_SOURCE_DIR}/cd/cd.ixx"
)

add_gtest(gtest_bcd
    SOURCE  "test_bcd.cc"
    MODULES
        "${CMAKE_SOURCE_DIR}/cd/cd.ixx"
)

add_gtest(gtest_unscramble
    SOURCE  "test_unscramble.cc"
    MODULES
        "${CMAKE_SOURCE_DIR}/cd/cd.ixx"
        "${CMAKE_SOURCE_DIR}/cd/cdrom.ixx"
        "${CMAKE_SOURCE_DIR}/cd/cd_scrambler.ixx"
        "${CMAKE_SOURCE_DIR}/utils/file_io.ixx"
        "${CMAKE_SOURCE_DIR}/utils/misc.ixx"
        "${CMAKE_SOURCE_DIR}/utils/strings.ixx"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_gtest(gtest_crc
    SOURCE  "test_crc.cc"
    MODULES
        "${CMAKE_SOURCE_DIR}/crc/crc.ixx"
        "${CMAKE_SOURCE_DIR}/crc/crc16_gsm.ixx"
        "${CMAKE_SOURCE_DIR}/crc/crc32.ixx"
        "${CMAKE_SOURCE_DIR}/cd/cd_edc.ixx"
        "${CMAKE_SOURCE_DIR}/dvd/dvd_edc.ixx"
        "${CMAKE_SOURCE_DIR}/utils/misc.ixx"
)

add_gtest(gtest_range
    SOURCE  "test_range.cc"
    MODULES "${CMAKE_SOURCE_DIR}/range.ixx"
)

add_gtest(gtest_misc
    SOURCE  "test_misc.cc"
    MODULES "${CMAKE_SOURCE_DIR}/utils/misc.ixx"
)

add_gtest(gtest_bit_copy
    SOURCE  "test_bit_copy.cc"
    MODULES "${CMAKE_SOURCE_DIR}/utils/misc.ixx"
)
