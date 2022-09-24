add_executable(tests
	"${CMAKE_SOURCE_DIR}/cd.hh"
	"${CMAKE_SOURCE_DIR}/cd.cc"
	"tests.cc"
)
target_include_directories(tests PUBLIC ${CMAKE_SOURCE_DIR} ${FMT_INCLUDE})

add_test(NAME tests COMMAND tests)
