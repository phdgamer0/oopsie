cmake_minimum_required(VERSION 3.16)
project(Oopsie LANGUAGES C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable CTest framework
enable_testing()

# Build the WAL Engine as a static library
add_library(oopsie_wal STATIC src/oopsie_wal.c)
target_include_directories(oopsie_wal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(oopsie_wal PRIVATE -O3 -Wall -Wextra -pedantic -std=c23)

# Main CLI Application
add_executable(Oopsie src/main.c)
target_link_libraries(Oopsie PRIVATE oopsie_wal ${CMAKE_DL_LIBS})
target_compile_options(Oopsie PRIVATE -O3 -Wall -Wextra -pedantic -std=c23 -march=native -mtune=native -flto)

# Tests
add_executable(test_wal_basic tests/test_wal_basic.c)
target_link_libraries(test_wal_basic PRIVATE oopsie_wal)
add_test(NAME test_wal_basic COMMAND test_wal_basic)

add_executable(test_wal_boundary tests/test_wal_boundary.c)
target_link_libraries(test_wal_boundary PRIVATE oopsie_wal)
add_test(NAME test_wal_boundary COMMAND test_wal_boundary)

add_executable(test_wal_speed tests/test_wal_speed.c)
target_link_libraries(test_wal_speed PRIVATE oopsie_wal)
add_test(NAME test_wal_speed COMMAND test_wal_speed)

add_library(oopsie_shim SHARED src/shim.c)
target_link_libraries(oopsie_shim PRIVATE oopsie_wal ${CMAKE_DL_LIBS})

# Shim Tests
add_executable(test_shim_basic tests/test_shim_basic.c)
target_link_libraries(test_shim_basic PRIVATE oopsie_wal)
add_test(NAME test_shim_basic COMMAND test_shim_basic)
set_tests_properties(test_shim_basic PROPERTIES ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:oopsie_shim>")

add_executable(test_shim_boundary tests/test_shim_boundary.c)
target_link_libraries(test_shim_boundary PRIVATE oopsie_wal)
add_test(NAME test_shim_boundary COMMAND test_shim_boundary)
set_tests_properties(test_shim_boundary PROPERTIES ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:oopsie_shim>")

add_executable(test_shim_speed tests/test_shim_speed.c)
target_link_libraries(test_shim_speed PRIVATE oopsie_wal)
add_test(NAME test_shim_speed COMMAND test_shim_speed)
set_tests_properties(test_shim_speed PROPERTIES ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:oopsie_shim>")
# New tests
add_executable(test_shim_open tests/test_shim_open.c)
target_link_libraries(test_shim_open PRIVATE oopsie_wal)
add_test(NAME test_shim_open COMMAND test_shim_open)
set_tests_properties(test_shim_open PROPERTIES ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:oopsie_shim>")

add_executable(test_shim_rename tests/test_shim_rename.c)
target_link_libraries(test_shim_rename PRIVATE oopsie_wal)
add_test(NAME test_shim_rename COMMAND test_shim_rename)
set_tests_properties(test_shim_rename PROPERTIES ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:oopsie_shim>")
