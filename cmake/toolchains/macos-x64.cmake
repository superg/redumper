execute_process(COMMAND brew --prefix llvm@18 OUTPUT_VARIABLE LLVM_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE)

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_OSX_ARCHITECTURES "x86_64")
set(CMAKE_CXX_COMPILER "${LLVM_ROOT}/bin/clang++")
set(CMAKE_PREFIX_PATH "${LLVM_ROOT}")

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")
execute_process(COMMAND xcrun --sdk macosx --show-sdk-path OUTPUT_VARIABLE CMAKE_OSX_SYSROOT OUTPUT_STRIP_TRAILING_WHITESPACE)

# use LLVM's libc++ instead of system libc++
execute_process(COMMAND arch -x86_64 /usr/local/bin/brew --prefix llvm@18 OUTPUT_VARIABLE LLVM_X64_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE)
set(CMAKE_EXE_LINKER_FLAGS "-L${LLVM_X64_ROOT}/lib/c++")

# this variable is used in CMakeLists.txt to bundle libc++ with the application
set(LLVM_LIB_PATH "${LLVM_X64_ROOT}/lib" CACHE PATH "Path to LLVM libraries")
