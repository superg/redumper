cmake_minimum_required(VERSION 3.28)

# project / version
set(REDUMPER_PROJECT_NAME "redumper" CACHE STRING "Project name")
string(TIMESTAMP version_major "%Y")
string(TIMESTAMP version_minor "%m")
string(TIMESTAMP version_patch "%d")
set(REDUMPER_VERSION_MAJOR ${version_major} CACHE STRING "Major version number")
set(REDUMPER_VERSION_MINOR ${version_minor} CACHE STRING "Minor version number")
set(REDUMPER_VERSION_PATCH ${version_patch} CACHE STRING "Patch version number")

set(REDUMPER_VERSION_BUILD "LOCAL" CACHE STRING "Version patch")
project(${REDUMPER_PROJECT_NAME} LANGUAGES CXX VERSION "${REDUMPER_VERSION_MAJOR}.${REDUMPER_VERSION_MINOR}.${REDUMPER_VERSION_PATCH}")

set(REDUMPER_CLANG_LINK_OPTIONS "" CACHE STRING "Extra LLVM/Clang linker options")

# build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()
set(CMAKE_CONFIGURATION_TYPES ${CMAKE_BUILD_TYPE} CACHE INTERNAL "Active configuration" FORCE)

# packaging
set(CPACK_GENERATOR "ZIP")
# add build number to a package file name if it was provided
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(system_suffix__ "64")
else()
    set(system_suffix__ "32")
endif()
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}_build${REDUMPER_VERSION_BUILD}-${CMAKE_SYSTEM_NAME}${system_suffix__}")
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
add_definitions(-DREDUMPER_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
                -DREDUMPER_VERSION_MINOR=${PROJECT_VERSION_MINOR}
                -DREDUMPER_VERSION_PATCH=${PROJECT_VERSION_PATCH}
                -DREDUMPER_VERSION_BUILD=${REDUMPER_VERSION_BUILD})

option(REDUMPER_CLANG_USE_LIBCPP "Link to libc++ instead of system default" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(REDUMPER_CLANG_USE_LIBCPP)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
        add_link_options(-stdlib=libc++)
    endif()

    add_link_options(${REDUMPER_CLANG_LINK_OPTIONS})
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(-Wall -Wextra -Werror)

    # remove after https://gcc.gnu.org/bugzilla/show_bug.cgi?id=99426 is fixed
    add_compile_options(--param=ggc-min-expand=10000)
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

# code formatting
find_program(CLANG_FORMAT "clang-format-18")

if(CLANG_FORMAT)
    file(GLOB_RECURSE ALL_SOURCE_FILES
        "${PROJECT_SOURCE_DIR}/*.cc"
        "${PROJECT_SOURCE_DIR}/*.hh"
        "${PROJECT_SOURCE_DIR}/*.ixx"
    )
    
    add_custom_target(format COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES})
    add_custom_target(check-format COMMAND ${CLANG_FORMAT} --dry-run --Werror ${ALL_SOURCE_FILES})
else()
    message(WARNING "clang-format-18 not found, source code formatting targets will not be available")
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

add_executable(redumper)
target_sources(redumper
    PUBLIC
    "dvd/css/css_tables.cc"
    "main.cc"
    "driveoffsets.txt"
    "systems/system.hh"
    "utils/throw_line.hh"
    "redumper.manifest"
    ${DRIVEOFFSETS_FN}
    PRIVATE FILE_SET cxx_modules TYPE CXX_MODULES FILES
    "analyzers/analyzer.ixx"
    "analyzers/silence.ixx"
    "analyzers/sync.ixx"
    "cd/cd.ixx"
    "cd/cd_dump_extra.ixx"
    "cd/cd_dump_new.ixx"
    "cd/cdrom.ixx"
    "cd/ecc.ixx"
    "cd/edc.ixx"
    "cd/fix_msf.ixx"
    "cd/offset_manager.ixx"
    "cd/protection.ixx"
    "cd/scrambler.ixx"
    "cd/cd_split.ixx"
    "cd/subcode.ixx"
    "cd/toc.ixx"
    "crc/crc.ixx"
    "crc/crc16_gsm.ixx"
    "crc/crc32.ixx"
	"drive/flash_mt1339.ixx"
    "dvd/dvd_dump.ixx"
    "dvd/dvd_key.ixx"
    "dvd/dvd_split.ixx"
    "dvd/css/css.ixx"
    "filesystem/iso9660/iso9660.ixx"
    "filesystem/iso9660/iso9660_browser.ixx"
    "filesystem/iso9660/iso9660_defs.ixx"
    "filesystem/iso9660/iso9660_entry.ixx"
    "filesystem/iso9660/iso9660_map.ixx"
    "hash/block_hasher.ixx"
    "hash/md5.ixx"
    "hash/sha1.ixx"
    "readers/disc_read_form1_reader.ixx"
    "readers/image_bin_form1_reader.ixx"
    "readers/image_iso_form1_reader.ixx"
    "readers/image_raw_reader.ixx"
    "readers/image_simple_reader.ixx"
    "readers/sector_reader.ixx"
    "scsi/cmd.ixx"
    "scsi/mmc.ixx"
    "scsi/sptd.ixx"
    "systems/securom.ixx"
    "systems/s_cdrom.ixx"
    "systems/s_iso.ixx"
    "systems/dc.ixx"
    "systems/mcd.ixx"
    "systems/psx.ixx"
    "systems/ps2.ixx"
    "systems/ps3.ixx"
    "systems/ps4.ixx"
    "systems/ps5.ixx"
    "systems/sat.ixx"
    "systems/systems.ixx"
    "utils/animation.ixx"
    "utils/endian.ixx"
    "utils/file_io.ixx"
    "utils/hex_bin.ixx"
    "utils/logger.ixx"
    "utils/misc.ixx"
    "utils/signal.ixx"
    "utils/strings.ixx"
    "utils/xbox.ixx"
    "debug.ixx"
    "dump.ixx"
    "drive.ixx"
    "hash.ixx"
    "info.ixx"
    "options.ixx"
    "range.ixx"
    "redumper.ixx"
    "rings.ixx"
    "rom_entry.ixx"
    "skeleton.ixx"
    "version.ixx"
)

target_include_directories(redumper
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    "utils"
)

set(libs
)

if(REDUMPER_TARGET_MACOSX)
    find_library(CORE_FOUNDATION CoreFoundation REQUIRED)
    find_library(IO_KIT IOKit REQUIRED)
    set(libs ${libs}
        ${CORE_FOUNDATION}
        ${IO_KIT}
    )
endif()

target_link_libraries(redumper ${libs})

install(TARGETS redumper DESTINATION "bin")

# Windows 7 requires administrative access in order to access the disc drive
if(MSVC AND ${CMAKE_SYSTEM_VERSION} EQUAL 6.1)
    set_target_properties(redumper PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
endif()

enable_testing()

add_subdirectory("tests")
