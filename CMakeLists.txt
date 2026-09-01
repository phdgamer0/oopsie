cmake_minimum_required(VERSION 3.16)
project(Oopsie LANGUAGES C)
target_include_directories(Oopsie PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_executable(Oopsie src/main.c)
