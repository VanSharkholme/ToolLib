cmake_minimum_required(VERSION 3.20)

set(module lock)

add_library(lock STATIC)
file(GLOB src
    lock.c
    IRQ/*.c
    Spinlock/*.c
)
target_sources(lock PRIVATE ${src})
target_include_directories(lock
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(lock
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/IRQ
        ${CMAKE_CURRENT_SOURCE_DIR}/Spinlock
)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()