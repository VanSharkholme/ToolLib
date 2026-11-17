add_library(pal_uart_contract INTERFACE)
target_include_directories(pal_uart_contract INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/port
)

add_library(pal_uart STATIC src/pal_uart.c)
target_link_libraries(pal_uart PUBLIC pal_uart_contract pal_uart_port)

if(CMAKE_CROSSCOMPILING)
    set(uart_port_default none)
else()
    set(uart_port_default host)
endif()
set(TOOLLIB_UART_PORT "${uart_port_default}" CACHE STRING "UART PAL port (host, stm32 or none)")
set_property(CACHE TOOLLIB_UART_PORT PROPERTY STRINGS host stm32 none)

if(TOOLLIB_UART_PORT STREQUAL "host")
    add_subdirectory(port/host)
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_subdirectory(tests)
    endif()
elseif(TOOLLIB_UART_PORT STREQUAL "stm32")
    add_subdirectory(port/stm32h5)
elseif(NOT TOOLLIB_UART_PORT STREQUAL "none")
    if(NOT TARGET pal_uart_port)
        message(FATAL_ERROR "Unknown TOOLLIB_UART_PORT: ${TOOLLIB_UART_PORT}")
    endif()
endif()
