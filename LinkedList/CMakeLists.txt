cmake_minimum_required(VERSION 3.20)

set(module linked_list)

add_library(linked_list INTERFACE)
target_sources(linked_list INTERFACE linked_list.h)
target_include_directories(linked_list INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()