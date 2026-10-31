add_library(pal_uart_contract INTERFACE)
target_include_directories(pal_uart_contract INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/port
)

add_library(pal_uart STATIC src/pal_uart.c)
target_link_libraries(pal_uart PUBLIC pal_uart_contract pal_uart_port)
