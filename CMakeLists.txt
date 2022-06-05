cmake_minimum_required(VERSION 3.14.1)

enable_testing()

set(REDUMPER_PROJECT_NAME "redumper" CACHE STRING "Project name")
project(${REDUMPER_PROJECT_NAME})

# build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()
# hide incomplete configurations from Visual Studio
set(CMAKE_CONFIGURATION_TYPES ${CMAKE_BUILD_TYPE} CACHE INTERNAL "Active configuration" FORCE)
mark_as_advanced(FORCE CMAKE_CONFIGURATION_TYPES)

# installation
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/INSTALL" CACHE PATH "Installation prefix" FORCE)
endif()
set(CMAKE_INSTALL_MESSAGE LAZY)

set(CMAKE_CXX_STANDARD 23)

# Visual Studio settings
if(MSVC)
    # build MT configuration by default
    string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL}")

    # multithreaded build
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
	add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -D_NTSCSI_USER_MODE_)
endif()
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# target that depends on never generated file hack
add_custom_target(redumper_always_run ALL DEPENDS always_run)
set_property(TARGET redumper_always_run PROPERTY FOLDER "CMakePredefinedTargets")

# version
string(TIMESTAMP REDUMPER_VERSION_MAJOR "%Y")
string(TIMESTAMP REDUMPER_VERSION_MINOR "%m")
string(TIMESTAMP REDUMPER_TIMESTAMP "%m/%d/%Y %H:%M:%S")
add_definitions(-DREDUMPER_VERSION_MAJOR=${REDUMPER_VERSION_MAJOR}
                -DREDUMPER_VERSION_MINOR=${REDUMPER_VERSION_MINOR}
				-DREDUMPER_TIMESTAMP=${REDUMPER_TIMESTAMP})

add_custom_command(OUTPUT always_run "${PROJECT_BINARY_DIR}/version.cc"
    COMMAND cmake -E echo "version touch"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/version.cc" "${PROJECT_BINARY_DIR}"
	COMMAND ${CMAKE_COMMAND} -E touch "${PROJECT_BINARY_DIR}/version.cc"
)

# packaging
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${REDUMPER_VERSION_MAJOR}.${REDUMPER_VERSION_MINOR}")
include(CPack)

set(DRIVEOFFSETS_FN "${CMAKE_CURRENT_BINARY_DIR}/driveoffsets.inc")

add_executable(generate_offsets generate_offsets.cc)

add_custom_command(
	OUTPUT ${DRIVEOFFSETS_FN}
	COMMAND generate_offsets "${CMAKE_CURRENT_SOURCE_DIR}/driveoffsets.txt"
	DEPENDS generate_offsets "driveoffsets.txt"
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set(sources
	"block_hasher.hh"
	"cd.cc"
	"cd.hh"
	"cmd.cc"
	"cmd.hh"
	"common.cc"
	"common.hh"
	"crc16_gsm.cc"
	"crc16_gsm.hh"
	"crc32.cc"
	"crc32.hh"
	"drive.cc"
	"drive.hh"
	"ecc_edc.cc"
	"ecc_edc.hh"
	"endian.cc"
	"endian.hh"
	"file_io.cc"
	"file_io.hh"
	"hex_bin.cc"
	"hex_bin.hh"
	"image_browser.cc"
	"image_browser.hh"
	"iso9660.cc"
	"iso9660.hh"
	"logger.cc"
	"logger.hh"
	"main.cc"
	"md5.cc"
	"md5.hh"
	"options.cc"
	"options.hh"
	"redumper.cc"
	"redumper.hh"
	"scrambler.cc"
	"scrambler.hh"
	"scsi.cc"
	"scsi.hh"
	"sha1.cc"
	"sha1.hh"
	"split.cc"
	"split.hh"
	"subcode.cc"
	"subcode.hh"
	"toc.cc"
	"toc.hh"
	"windows.cc"
	"windows.hh"
	"version.hh"
	"${PROJECT_BINARY_DIR}/version.cc"
	"driveoffsets.txt"
	${DRIVEOFFSETS_FN}
	"redumper.exe.manifest"
)

set(includes
	${CMAKE_CURRENT_BINARY_DIR}
)

set(libs
)

add_executable(redumper ${sources})
target_include_directories(redumper PUBLIC ${includes})
target_link_libraries(redumper ${libs})

install(TARGETS redumper DESTINATION "bin")

# flatten the sources lists
file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)
foreach(i IN LISTS files)
    get_filename_component(source_path "${i}" PATH)
    string(REPLACE "/" "\\" source_path "${source_path}")
    source_group("${source_path}" FILES ${i})
endforeach()

add_subdirectory("tests")
