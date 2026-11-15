cmake_minimum_required(VERSION 3.20)
add_executable(test_logging_lifecycle test_logging_lifecycle.c
    ../src/log.c ../src/log_backend_manager.c ../src/log_formatter.c)
target_include_directories(test_logging_lifecycle PRIVATE ../include ../src)
# Buffer/port fakes exercise the core without building the CMSIS RTOS adapter.
target_link_libraries(test_logging_lifecycle PRIVATE blk_buf unity)
add_executable(test_logging_pal_uart test_logging_pal_uart.c
    ../backends/PAL_UART/log_backend_pal_uart.c)
target_include_directories(test_logging_pal_uart PRIVATE ../include ../backends/PAL_UART)
target_link_libraries(test_logging_pal_uart PRIVATE pal_uart unity)

foreach(target IN ITEMS test_logging_lifecycle test_logging_pal_uart)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${target}.c)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${target}.c" cases
         REGEX "^[ ]+TEST_CASE\\(test_[A-Za-z0-9_]+\\)")
    foreach(entry IN LISTS cases)
        string(REGEX MATCH "test_[A-Za-z0-9_]+" case_name "${entry}")
        add_test(NAME logging.${case_name} COMMAND ${target} ${case_name})
        set_tests_properties(logging.${case_name} PROPERTIES TIMEOUT 5 LABELS logging)
    endforeach()
endforeach()

# Compile the real formatter and manager under every color/newline combination.
foreach(color RANGE 0 1)
    foreach(newline RANGE 0 1)
        set(target test_logging_color_${color}_${newline})
        add_executable(${target} test_logging_color.c
            ../src/log_formatter.c ../src/log_backend_manager.c)
        target_include_directories(${target} PRIVATE ../include ../src)
        target_link_libraries(${target} PRIVATE lock unity)
        target_compile_definitions(${target} PRIVATE
            LOG_ENABLE_COLOR=${color} LOG_AUTO_NEWLINE=${newline})
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS test_logging_color.c)
        file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/test_logging_color.c" color_cases
             REGEX "^[ ]+TEST_CASE\\(test_[A-Za-z0-9_]+\\)")
        foreach(entry IN LISTS color_cases)
            string(REGEX MATCH "test_[A-Za-z0-9_]+" case_name "${entry}")
            set(test_name logging.color_${color}_${newline}.${case_name})
            add_test(NAME ${test_name} COMMAND ${target} ${case_name})
            set_tests_properties(${test_name} PROPERTIES TIMEOUT 5 LABELS logging)
        endforeach()
    endforeach()
endforeach()
