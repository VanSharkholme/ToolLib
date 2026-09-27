cmake_minimum_required(VERSION 3.20)

set(module blk_buf)

add_library(blk_buf STATIC)
target_sources(blk_buf PUBLIC ring_blk_buf.c)
target_include_directories(blk_buf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(blk_buf linked_list)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()
