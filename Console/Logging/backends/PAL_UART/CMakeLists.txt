set(module logging_backend_pal_uart)

add_library(logging_backend_pal_uart STATIC)

target_sources(logging_backend_pal_uart PRIVATE
    log_backend_pal_uart.c
)

target_include_directories(logging_backend_pal_uart PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(logging_backend_pal_uart PUBLIC logging pal_uart)