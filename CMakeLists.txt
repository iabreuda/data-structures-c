cmake_minimum_required(VERSION 3.16)

project(
        CDataStructures
        VERSION 0.1.0
        DESCRIPTION "Data structures implemented in ANSI C"
        LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(
        cds
        STATIC
        src/hash/hashmap.c
)

target_include_directories(
        cds
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(
        hashmap_example
        examples/hashmap_example.c
)

target_link_libraries(
        hashmap_example
        PRIVATE
        cds
)

if(MSVC)
    target_compile_options(cds PRIVATE /W4)
else()
    target_compile_options(cds PRIVATE -Wall -Wextra -Wpedantic)
endif()
