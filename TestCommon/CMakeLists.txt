if (BUILD_TESTING)
    add_library(unity STATIC unity/src/unity.c)
    target_include_directories(unity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/unity/src)
endif ()
