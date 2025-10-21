execute_process(COMMAND brew --prefix llvm@18 OUTPUT_VARIABLE LLVM_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE)

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_OSX_ARCHITECTURES "x86_64")
set(CMAKE_CXX_COMPILER "${LLVM_ROOT}/bin/clang++")
set(CMAKE_PREFIX_PATH "${LLVM_ROOT}")

# libc++ libraries for x86_64 are taken from x86_64 Homebrew distribution installed with Rosetta
execute_process(COMMAND arch -x86_64 /usr/local/bin/brew --prefix llvm@18 OUTPUT_VARIABLE LLVM_X64_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE)

# link libc++ statically
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib++ -Wl,-deployment_target_mismatches,suppress ${LLVM_X64_ROOT}/lib/c++/libc++.a ${LLVM_X64_ROOT}/lib/c++/libc++abi.a")

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")
execute_process(COMMAND xcrun --sdk macosx --show-sdk-path OUTPUT_VARIABLE CMAKE_OSX_SYSROOT OUTPUT_STRIP_TRAILING_WHITESPACE)
