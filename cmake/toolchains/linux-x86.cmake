set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_CXX_COMPILER "clang++-18")

set(CMAKE_CXX_COMPILER_TARGET "i686-linux-gnu")

# link libstdc++ statically
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
