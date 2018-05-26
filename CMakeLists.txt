cmake_minimum_required(VERSION 2.8)
project(rd-multi-strm)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror -fPIC")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -fPIC -std=gnu++11")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_DEBUG")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DEBUG")
endif()

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,'$ORIGIN/'")

set(SOURCE_FILES main.cpp signal-handling.cpp util.cpp uncompress-stream.cpp child-process-tracking.cpp read-buf-ctx.cpp read-multi-strm.cpp)

SET(LIBRARY_OUTPUT_PATH "${rd-multi-strm_SOURCE_DIR}/${CMAKE_BUILD_TYPE}")

SET(EXECUTABLE_OUTPUT_PATH "${LIBRARY_OUTPUT_PATH}")

add_executable(rd-multi-strm ${SOURCE_FILES})

target_link_libraries(rd-multi-strm rt pthread)

set_target_properties(rd-multi-strm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${EXECUTABLE_OUTPUT_PATH}"
)
