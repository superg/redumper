cmake_minimum_required(VERSION 3.26)

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
	if(REDUMPER_TARGET_LINUX)
		# build complete static executable for linux
		set(extra_link_options "-static")
	elseif(REDUMPER_TARGET_MACOSX)
		# use experimental-library from brew llvm@16
		set(extra_link_options "-L/usr/local/opt/llvm/lib/c++")
	endif()

	# -fexperimental-library is required for clang-16 as std::format is defined there
	add_compile_options(-stdlib=libc++ -fexperimental-library)
	add_link_options(-stdlib=libc++ ${extra_link_options} -fexperimental-library)

	# silence implicit modules use warning
	# TL;DR, this will be taken cared of by listing module dependencies explicitly (cmake)
	# more information:
	# https://github.com/llvm/llvm-project/issues/62707
	if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 17)
		add_compile_options(-Wno-read-modules-implicitly)
	endif()
endif()
if(MSVC)
	# disable modules inlining
	# wait until Microsoft actually fixes this and remove the option, more information:
	# https://developercommunity.visualstudio.com/t/VS-1750-Compiler-is-Broken/10288409
	# https://developercommunity.visualstudio.com/t/Modules-ICE-when-using-cout-inside-of-/10299789
	# https://developercommunity.visualstudio.com/t/C-Modules:-Internal-Compiler-Error-on-/10377473
	add_compile_options(/dxifcInlineFunctions-)

	# build MT configuration by default
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

	# multithreaded build
	add_compile_options(/MP)

	add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -D_NTSCSI_USER_MODE_)

	set_property(GLOBAL PROPERTY USE_FOLDERS ON)
	# flatten source lists
	file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)
	foreach(i IN LISTS files)
		get_filename_component(source_path "${i}" PATH)
		string(REPLACE "/" "\\" source_path "${source_path}")
		source_group("${source_path}" FILES ${i})
	endforeach()
endif()

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
	"main.cc"
	"driveoffsets.txt"
	"utils/throw_line.hh"
	"redumper.manifest"
	${DRIVEOFFSETS_FN}
)

set(includes
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	"utils"
)

add_executable(redumper)
target_sources(redumper
	PUBLIC
	${sources}
	"dvd/css/css_tables.cc"
	PRIVATE FILE_SET cxx_modules TYPE CXX_MODULES FILES
	"analyzers/analyzer.ixx"
	"analyzers/silence.ixx"
	"analyzers/sync.ixx"
	"cd/cd.ixx"
	"cd/cd_dump.ixx"
	"cd/cdrom.ixx"
	"cd/ecc.ixx"
	"cd/edc.ixx"
	"cd/scrambler.ixx"
	"cd/subcode.ixx"
	"cd/toc.ixx"
	"crc/crc.ixx"
	"crc/crc16_gsm.ixx"
	"crc/crc32.ixx"
	"dvd/dvd_dump.ixx"
	"dvd/dvd_key.ixx"
	"dvd/css/css.ixx"
	"filesystem/image_browser.ixx"
	"filesystem/iso9660.ixx"
	"hash/block_hasher.ixx"
	"hash/md5.ixx"
	"hash/sha1.ixx"
	"readers/block_reader.ixx"
	"readers/cd_form1_reader.ixx"
	"readers/form1_reader.ixx"
	"readers/iso_form1_reader.ixx"
	"scsi/cmd.ixx"
	"scsi/mmc.ixx"
	"scsi/sptd.ixx"
	"systems/system_cdrom.ixx"
	"systems/system_iso.ixx"
	"systems/system_mcd.ixx"
	"systems/system_psx.ixx"
	"systems/system_ss.ixx"
	"systems/system.ixx"
	"utils/animation.ixx"
	"utils/endian.ixx"
	"utils/file_io.ixx"
	"utils/hex_bin.ixx"
	"utils/logger.ixx"
	"utils/misc.ixx"
	"utils/signal.ixx"
	"utils/strings.ixx"
	"commands.ixx"
	"debug.ixx"
	"dump.ixx"
	"drive.ixx"
	"info.ixx"
	"offset_manager.ixx"
	"options.ixx"
	"redumper.ixx"
	"rom_entry.ixx"
	"split.ixx"
	"version.ixx"
)
target_include_directories(redumper PUBLIC ${includes})

install(TARGETS redumper DESTINATION "bin")

# Windows 7 requires administrative access in order to access the disc drive
if(MSVC AND ${CMAKE_SYSTEM_VERSION} EQUAL 6.1)
	set_target_properties(redumper PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
endif()

enable_testing()
add_subdirectory("tests")
