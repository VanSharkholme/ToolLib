add_executable(test_pal_gpio test_pal_gpio.c)
target_link_libraries(test_pal_gpio PRIVATE pal_gpio unity)

foreach(case configuration validation edges shared_line)
    add_test(NAME pal_gpio.${case} COMMAND test_pal_gpio test_${case})
    set_tests_properties(pal_gpio.${case} PROPERTIES TIMEOUT 5 LABELS pal_gpio)
endforeach()
