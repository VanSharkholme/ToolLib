cmake_minimum_required(VERSION 3.20)

set(module logging)

add_library(logging STATIC)
file(GLOB log_src
    backends/*.c
    ports/*.c
    src/*.c
)
target_sources(logging PRIVATE ${log_src})
target_include_directories(logging PUBLIC
    include
    PRIVATE
    backends
    ports
    src
)
target_link_libraries(logging PUBLIC blk_buf)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()