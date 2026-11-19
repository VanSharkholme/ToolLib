add_library(pal_spi_contract INTERFACE)
target_include_directories(pal_spi_contract INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/port
)

add_library(pal_spi STATIC src/pal_spi.c)
target_link_libraries(pal_spi PUBLIC pal_spi_contract pal_spi_port)

if(CMAKE_CROSSCOMPILING)
    set(spi_port_default none)
else()
    set(spi_port_default host)
endif()

set(TOOLLIB_SPI_PORT "${spi_port_default}" CACHE STRING "SPI PAL port (host, stm32, stm32f4 or none)")
set_property(CACHE TOOLLIB_SPI_PORT PROPERTY STRINGS host stm32 stm32f4 none)

if(TOOLLIB_SPI_PORT STREQUAL "host")
    add_subdirectory(port/host)
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_subdirectory(tests)
    endif()
elseif(TOOLLIB_SPI_PORT STREQUAL "stm32")
    add_subdirectory(port/stm32h5)
elseif(TOOLLIB_SPI_PORT STREQUAL "stm32f4")
    add_subdirectory(port/stm32f4)
elseif(NOT TOOLLIB_SPI_PORT STREQUAL "none")
    if(NOT TARGET pal_spi_port)
        message(FATAL_ERROR "Unknown TOOLLIB_SPI_PORT: ${TOOLLIB_SPI_PORT}")
    endif()
endif()