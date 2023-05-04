add_executable(tests)

target_include_directories(tests PUBLIC ${CMAKE_SOURCE_DIR})
target_sources(tests
	PUBLIC
	"${CMAKE_SOURCE_DIR}/cd.hh"
	"${CMAKE_SOURCE_DIR}/cd.cc"
	"${CMAKE_SOURCE_DIR}/common.hh"
	"${CMAKE_SOURCE_DIR}/common.cc"
	"${CMAKE_SOURCE_DIR}/scrambler.hh"
	"${CMAKE_SOURCE_DIR}/scrambler.cc"
	"tests.cc"
	PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
	"file_io.cc"
)

add_test(NAME tests COMMAND tests WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")