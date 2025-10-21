execute_process(COMMAND brew --prefix llvm@18 OUTPUT_VARIABLE LLVM_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE)

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_OSX_ARCHITECTURES "arm64")
set(CMAKE_CXX_COMPILER "${LLVM_ROOT}/bin/clang++")
set(CMAKE_PREFIX_PATH "${LLVM_ROOT}")

# link libc++ statically
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib++ -Wl,-deployment_target_mismatches,suppress ${LLVM_ROOT}/lib/c++/libc++.a ${LLVM_ROOT}/lib/c++/libc++abi.a")

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")
execute_process(COMMAND xcrun --sdk macosx --show-sdk-path OUTPUT_VARIABLE CMAKE_OSX_SYSROOT OUTPUT_STRIP_TRAILING_WHITESPACE)
