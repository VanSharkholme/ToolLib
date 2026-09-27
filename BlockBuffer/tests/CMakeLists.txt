cmake_minimum_required(VERSION 3.20)

add_executable(test_${module} test_${module}.c)
target_link_libraries(test_${module} PRIVATE ${module} unity)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_${module} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The registration table is the single source of truth for test discovery.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS test_${module}.c)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/test_${module}.c" cases
     REGEX "^[ ]+TEST_CASE\\(test_[A-Za-z0-9_]+\\)")
foreach(entry IN LISTS cases)
    string(REGEX MATCH "test_[A-Za-z0-9_]+" case_name "${entry}")
    add_test(NAME ${module}.${case_name} COMMAND test_${module} ${case_name})
    set_tests_properties(${module}.${case_name} PROPERTIES TIMEOUT 5 LABELS ${module})
endforeach()

if(NOT CMAKE_CROSSCOMPILING)
    include(CheckIncludeFile)
    check_include_file(pthread.h HAVE_PTHREAD_H)
endif()
if(NOT CMAKE_CROSSCOMPILING AND HAVE_PTHREAD_H)
    find_package(Threads REQUIRED)
    add_executable(test_${module}_concurrent test_${module}_concurrent.c)
    target_link_libraries(test_${module}_concurrent PRIVATE ${module} Threads::Threads)
    add_test(NAME ${module}.concurrent COMMAND test_${module}_concurrent)
    set_tests_properties(${module}.concurrent PROPERTIES TIMEOUT 20 LABELS ${module})
endif()
