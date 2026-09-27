cmake_minimum_required(VERSION 3.20)
project(ToolLib C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include_directories(Base)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(TestCommon)
endif()

add_subdirectory(BlockBuffer)
add_subdirectory(LinkedList)

