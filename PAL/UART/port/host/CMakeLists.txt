add_library(pal_uart_port STATIC pal_uart_host.c)
target_include_directories(pal_uart_port PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pal_uart_port PUBLIC pal_uart_contract)
