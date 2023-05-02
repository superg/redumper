cmake_minimum_required(VERSION 3.26)
cmake_policy(SET CMP0096 NEW) # preserve leading zeroes in version components

# project / version
set(REDUMPER_PROJECT_NAME "redumper" CACHE STRING "Project name")
string(TIMESTAMP REDUMPER_VERSION_MAJOR "%Y")
string(TIMESTAMP REDUMPER_VERSION_MINOR "%m")
string(TIMESTAMP REDUMPER_VERSION_PATCH "%d")
set(REDUMPER_VERSION_BUILD "LOCAL" CACHE STRING "Version patch")
project(${REDUMPER_PROJECT_NAME}
	LANGUAGES CXX
	VERSION "${REDUMPER_VERSION_MAJOR}.${REDUMPER_VERSION_MINOR}.${REDUMPER_VERSION_PATCH}")

# build type
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the build type" FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()
set(CMAKE_CONFIGURATION_TYPES ${CMAKE_BUILD_TYPE} CACHE INTERNAL "Active configuration" FORCE)

# packaging
set(CPACK_GENERATOR "ZIP")
# add build number to a package file name if it was provided
if(NOT REDUMPER_VERSION_BUILD STREQUAL "LOCAL")
	set(system_name__ ${CMAKE_SYSTEM_NAME})
	if(system_name__ STREQUAL "Windows")
		if(CMAKE_SIZEOF_VOID_P EQUAL 8)
			set(system_name__ "win64")
		else()
			set(system_name__ "win32")
		endif()
	endif()
	set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}_build${REDUMPER_VERSION_BUILD}-${system_name__}")
endif()
include(CPack)

# platform
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(REDUMPER_TARGET_LINUX 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	set(REDUMPER_TARGET_WINDOWS 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	set(REDUMPER_TARGET_MACOSX 1)
else()
	message(FATAL_ERROR "Unknown target platform")
endif()

# C/C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "2182bf5c-ef0d-489a-91da-49dbc3090d2a")
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP 1)
add_definitions(-DREDUMPER_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
                -DREDUMPER_VERSION_MINOR=${PROJECT_VERSION_MINOR}
                -DREDUMPER_VERSION_PATCH=${PROJECT_VERSION_PATCH}
                -DREDUMPER_VERSION_BUILD=${REDUMPER_VERSION_BUILD})

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	# needed for std::format
	add_compile_options(-fexperimental-library -stdlib=libc++)
	add_link_options(-fexperimental-library -stdlib=libc++)
endif()
if(MSVC)
	# build MT configuration by default
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

	# multithreaded build
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

	add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -D_NTSCSI_USER_MODE_)

	set_property(GLOBAL PROPERTY USE_FOLDERS ON)
	# flatten the sources lists
	file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)
	foreach(i IN LISTS files)
		get_filename_component(source_path "${i}" PATH)
		string(REPLACE "/" "\\" source_path "${source_path}")
		source_group("${source_path}" FILES ${i})
	endforeach()
endif()

#if(CMAKE_COMPILER_IS_GNUCC)
#	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
#endif()

# generated source file
set(DRIVEOFFSETS_FN "${CMAKE_CURRENT_BINARY_DIR}/driveoffsets.inc")
add_executable(generate_offsets generate_offsets.cc)
add_custom_command(
	OUTPUT ${DRIVEOFFSETS_FN}
	COMMAND generate_offsets "${CMAKE_CURRENT_SOURCE_DIR}/driveoffsets.txt"
	DEPENDS generate_offsets "driveoffsets.txt"
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set(sources
	"analyzers/analyzer.hh"
	"analyzers/silence.hh"
	"analyzers/silence.cc"
	"analyzers/sync.hh"
	"analyzers/sync.cc"
	"systems/system.cc"
	"systems/system.hh"
	"systems/cdrom.cc"
	"systems/cdrom.hh"
	"systems/iso.cc"
	"systems/iso.hh"
	"systems/psx.cc"
	"systems/psx.hh"
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
	"dump.cc"
	"dump.hh"
	"dump_cd.cc"
	"dump_cd.hh"
	"dump_dvd.cc"
	"dump_dvd.hh"
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
	"main.cc"
	"md5.cc"
	"md5.hh"
	"mmc.hh"
	"offset_manager.cc"
	"offset_manager.hh"
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
	"signal.cc"
	"signal.hh"
	"split.cc"
	"split.hh"
	"driveoffsets.txt"
	${DRIVEOFFSETS_FN}
)

set(includes
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(redumper)
target_sources(redumper
	PUBLIC ${sources}
	PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
	"logger.cc"
	"subcode.cc"
    "toc.cc"
    "version.cc"
)
target_include_directories(redumper PUBLIC ${includes})

add_custom_target(version_touch ALL COMMAND ${CMAKE_COMMAND} -E touch "${PROJECT_SOURCE_DIR}/version.cc")
add_dependencies(redumper version_touch)

install(TARGETS redumper DESTINATION "bin")

# Windows 7 requires administrative access in order to access the disc drive
if(MSVC AND ${CMAKE_SYSTEM_VERSION} EQUAL 6.1)
	set_target_properties(redumper PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
endif()

enable_testing()
add_subdirectory("tests")
