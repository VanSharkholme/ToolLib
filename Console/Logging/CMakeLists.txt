cmake_minimum_required(VERSION 3.20)

add_subdirectory(backends)
set(module logging)

add_library(logging STATIC)
file(GLOB log_src CONFIGURE_DEPENDS
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

set(LOG_BACKENDS "" CACHE STRING "Logging backends, separated by semicolons")

foreach(backend IN LISTS LOG_BACKENDS)
    string(TOLOWER "${backend}" backend_name)
    set(backend_target "logging_backend_${backend_name}")

    if(NOT TARGET "${backend_target}")
        message(FATAL_ERROR "Unknown logging backend: ${backend}")
    endif()

    target_link_libraries(logging PUBLIC "${backend_target}")
endforeach()

if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()
