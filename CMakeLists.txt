cmake_minimum_required(VERSION 3.16)
project(Oopsie LANGUAGES C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(Oopsie src/main.c)
target_compile_options(Oopsie PRIVATE
    -O3
    -Wall
    -Wextra
    -pedantic
    -std=c23
    -march=native
    -mtune=native
    -flto
)
target_link_libraries(Oopsie PRIVATE
    ${CMAKE_DL_LIBS}
)
target_include_directories(Oopsie PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

