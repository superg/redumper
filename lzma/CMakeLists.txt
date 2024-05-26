set(sources
    "7zTypes.h"
    "7zWindows.h"
    "Alloc.c"
    "Alloc.h"
    "Compiler.h"
    "CpuArch.c"
    "CpuArch.h"
    "LzFind.c"
    "LzFind.h"
    "LzFindMt.c"
    "LzFindMt.h"
    "LzFindOpt.c"
    "LzHash.h"
    "LzmaDec.c"
    "LzmaDec.h"
    "LzmaEnc.c"
    "LzmaEnc.h"
    "Precomp.h"
    "Threads.c"
    "Threads.h"
)

set(includes
)

add_library(lzma STATIC)
target_sources(lzma
    PUBLIC
    ${sources}
)
target_include_directories(lzma PUBLIC ${includes})
