cmake_minimum_required(VERSION 3.28)

# project / version
set(REDUMPER_PROJECT_NAME "redumper" CACHE STRING "Project name")
set(REDUMPER_VERSION_BUILD "LOCAL" CACHE STRING "Build number")
project(${REDUMPER_PROJECT_NAME} LANGUAGES CXX)

# build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()
set(CMAKE_CONFIGURATION_TYPES ${CMAKE_BUILD_TYPE} CACHE INTERNAL "Active configuration" FORCE)

# target operating system, it's assumed that it's the same as the host operating system
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(REDUMPER_TARGET_LINUX 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(REDUMPER_TARGET_WINDOWS 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(REDUMPER_TARGET_MACOS 1)
endif()

# packaging
set(CPACK_GENERATOR "ZIP")
# extract platform suffix from toolchain filename
if(CMAKE_TOOLCHAIN_FILE)
    get_filename_component(toolchain_basename ${CMAKE_TOOLCHAIN_FILE} NAME_WE)
    set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${REDUMPER_VERSION_BUILD}-${toolchain_basename}")
else()
    set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${REDUMPER_VERSION_BUILD}")
endif()
include(CPack)

# C/C++
set(CMAKE_CXX_STANDARD 20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
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
find_program(CLANG_FORMAT "clang-format")

if(CLANG_FORMAT)
    file(GLOB_RECURSE ALL_SOURCE_FILES
        "${PROJECT_SOURCE_DIR}/*.cc"
        "${PROJECT_SOURCE_DIR}/*.hh"
        "${PROJECT_SOURCE_DIR}/*.ixx"
    )
    
    add_custom_target(format COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES})
    add_custom_target(check-format COMMAND ${CLANG_FORMAT} --dry-run --Werror ${ALL_SOURCE_FILES})
else()
    message(WARNING "clang-format not found, source code formatting targets will not be available")
endif()

add_executable(redumper)
target_sources(redumper
    PUBLIC
    "dvd/css/css_tables.cc"
    "main.cc"
    "systems/system.hh"
    "utils/throw_line.hh"
    "redumper.manifest"
    PRIVATE FILE_SET cxx_modules TYPE CXX_MODULES FILES
    "analyzers/analyzer.ixx"
    "analyzers/silence.ixx"
    "analyzers/sync.ixx"
    "cd/cd.ixx"
    "cd/cd_common.ixx"
    "cd/cd_dump_extra.ixx"
    "cd/cd_dump.ixx"
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
    "drive/flash_plextor.ixx"
    "drive/flash_sd616.ixx"
    "drive/flash_tsst.ixx"
    "drive/plextor.ixx"
    "drive/mediatek.ixx"
    "drive/test.ixx"
    "dvd/dvd_dump.ixx"
    "dvd/dvd_key.ixx"
    "dvd/dvd_split.ixx"
    "dvd/css/css.ixx"
    "filesystem/high_sierra/high_sierra.ixx"
    "filesystem/high_sierra/high_sierra_browser.ixx"
    "filesystem/high_sierra/high_sierra_defs.ixx"
    "filesystem/iso9660/iso9660.ixx"
    "filesystem/iso9660/iso9660_browser.ixx"
    "filesystem/iso9660/iso9660_defs.ixx"
    "filesystem/iso9660/iso9660_entry.ixx"
    "filesystem/iso9660/iso9660_map.ixx"
    "hash/block_hasher.ixx"
    "hash/md5.ixx"
    "hash/sha1.ixx"
    "readers/disc_read_cdda_reader.ixx"
    "readers/disc_read_reader.ixx"
    "readers/image_bin_reader.ixx"
    "readers/image_iso_reader.ixx"
    "readers/image_raw_reader.ixx"
    "readers/image_scram_reader.ixx"
    "readers/image_simple_reader.ixx"
    "readers/data_reader.ixx"
    "scsi/cmd.ixx"
    "scsi/mmc.ixx"
    "scsi/sptd.ixx"
    "systems/securom.ixx"
    "systems/s_cdrom.ixx"
    "systems/s_high_sierra.ixx"
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
	"utils/win32.ixx"
    "utils/xbox.ixx"
    "common.ixx"
    "debug.ixx"
    "drive.ixx"
    "hash.ixx"
    "info.ixx"
    "offsets.ixx"
    "options.ixx"
    "range.ixx"
    "redumper.ixx"
    "rings.ixx"
    "rom_entry.ixx"
    "skeleton.ixx"
    "version.ixx"
)

target_compile_definitions(redumper PRIVATE REDUMPER_VERSION_BUILD="${REDUMPER_VERSION_BUILD}")

target_include_directories(redumper
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    "utils"
)

set(libs
)

if(REDUMPER_TARGET_MACOS)
    find_library(CORE_FOUNDATION CoreFoundation REQUIRED)
    find_library(IO_KIT IOKit REQUIRED)
    set(libs ${libs}
        ${CORE_FOUNDATION}
        ${IO_KIT}
    )
endif()

target_link_libraries(redumper ${libs})

install(TARGETS redumper DESTINATION "bin")

if(REDUMPER_TARGET_MACOS)
    # Install the library with its symlinks
    install(FILES 
        "${LLVM_LIBCXX_PATH}/libc++.1.0.dylib"
        "${LLVM_LIBCXX_PATH}/libc++.1.dylib"
        "${LLVM_LIBCXX_PATH}/libc++.dylib"
        DESTINATION "lib"
    )
    
    # Change the install name in the installed binary (must run after binary is installed)
    install(CODE "
        execute_process(COMMAND \"${CMAKE_INSTALL_NAME_TOOL}\" 
            -change \"${LLVM_LIBCXX_PATH}/libc++.1.dylib\" 
            \"@executable_path/../lib/libc++.1.dylib\" 
            \"\${CMAKE_INSTALL_PREFIX}/bin/redumper\")
    ")
endif()

# Windows 7 requires administrative access in order to access the disc drive
if(MSVC AND CMAKE_SYSTEM_VERSION VERSION_EQUAL "6.1")
    set_target_properties(redumper PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
endif()

enable_testing()

add_subdirectory("tests")
