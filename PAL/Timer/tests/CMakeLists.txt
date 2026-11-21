add_executable(test_pal_timer test_pal_timer.c)
target_link_libraries(test_pal_timer PRIVATE pal_timer unity)

foreach(case independent preload trigger master_trigger capture binding compare id_bounds status channel_mapping)
    add_test(NAME pal_timer.${case} COMMAND test_pal_timer test_${case})
    set_tests_properties(pal_timer.${case} PROPERTIES TIMEOUT 5 LABELS pal_timer)
endforeach()
