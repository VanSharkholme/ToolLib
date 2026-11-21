add_library(pal_gpio_port STATIC pal_gpio_host.c)
target_include_directories(pal_gpio_port PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pal_gpio_port PUBLIC pal_gpio_contract)
