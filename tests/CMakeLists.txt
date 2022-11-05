add_executable(tests
	"${CMAKE_SOURCE_DIR}/cd.hh"
	"${CMAKE_SOURCE_DIR}/cd.cc"
	"${CMAKE_SOURCE_DIR}/common.hh"
	"${CMAKE_SOURCE_DIR}/common.cc"
	"${CMAKE_SOURCE_DIR}/file_io.hh"
	"${CMAKE_SOURCE_DIR}/file_io.cc"
	"${CMAKE_SOURCE_DIR}/scrambler.hh"
	"${CMAKE_SOURCE_DIR}/scrambler.cc"
	"tests.cc"
)
target_include_directories(tests PUBLIC ${CMAKE_SOURCE_DIR} ${FMT_INCLUDE})

add_test(NAME tests COMMAND tests WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
