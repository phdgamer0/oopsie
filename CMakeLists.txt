cmake_minimum_required(VERSION 3.16)
project(Oopsie LANGUAGES C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable CTest framework
enable_testing()

# Enable ccache if available to cache build artifacts (even after make clean)
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache enabled")
endif()

# Build the WAL Engine as a static library
add_library(oopsie_wal STATIC src/oopsie_wal.c)
set_target_properties(oopsie_wal PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(oopsie_wal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(oopsie_wal PRIVATE -O3 -Wall -Wextra -pedantic -std=c2x)

include(FetchContent)

# TUI dependency
FetchContent_Declare(
    termbox2
    URL https://raw.githubusercontent.com/termbox/termbox2/master/termbox2.h
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(termbox2)

set(TERMBOX2_INCLUDE_DIR "${CMAKE_BINARY_DIR}/external/termbox2")
file(MAKE_DIRECTORY "${TERMBOX2_INCLUDE_DIR}")
file(COPY "${termbox2_SOURCE_DIR}/termbox2.h" DESTINATION "${TERMBOX2_INCLUDE_DIR}")

add_library(oopsie_graphics STATIC src/oopsie_graphics.c)
target_include_directories(oopsie_graphics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(oopsie_graphics PRIVATE ${termbox2_SOURCE_DIR})
target_compile_options(oopsie_graphics PRIVATE -O3 -Wall -Wextra -std=c2x)

# Main CLI Application
add_executable(Oopsie src/main.c)
target_link_libraries(Oopsie PRIVATE oopsie_wal ${CMAKE_DL_LIBS})
target_link_libraries(Oopsie PRIVATE oopsie_graphics ${CMAKE_DL_LIBS})
target_compile_options(Oopsie PRIVATE -O3 -Wall -Wextra -pedantic -std=c2x -flto)

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

include(ExternalProject)

# Fetch and build liburing from source
ExternalProject_Add(liburing_ext
    GIT_REPOSITORY https://github.com/axboe/liburing.git
    GIT_TAG liburing-2.5
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
    BUILD_COMMAND make -C <SOURCE_DIR>/src
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE 1
    BUILD_BYPRODUCTS <SOURCE_DIR>/src/liburing.a
)
ExternalProject_Get_Property(liburing_ext SOURCE_DIR)
file(MAKE_DIRECTORY ${SOURCE_DIR}/src/include) # Prevent CMake error before it builds
add_library(uring STATIC IMPORTED)
add_dependencies(uring liburing_ext)
set_target_properties(uring PROPERTIES
    IMPORTED_LOCATION ${SOURCE_DIR}/src/liburing.a
    INTERFACE_INCLUDE_DIRECTORIES ${SOURCE_DIR}/src/include
)
target_link_libraries(oopsie_wal PUBLIC uring)

add_library(oopsie_shim SHARED src/shim.c)
target_link_libraries(oopsie_shim PRIVATE oopsie_wal uring ${CMAKE_DL_LIBS})

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

# New Parser Tests
add_executable(test_parse_basic tests/test_parse_basic.c)
target_link_libraries(test_parse_basic PRIVATE oopsie_wal)
add_test(NAME test_parse_basic COMMAND test_parse_basic)
set_tests_properties(test_parse_basic PROPERTIES ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:oopsie_shim>")

add_executable(test_parse_boundary tests/test_parse_boundary.c)
target_link_libraries(test_parse_boundary PRIVATE oopsie_wal)
add_test(NAME test_parse_boundary COMMAND test_parse_boundary)
set_tests_properties(test_parse_boundary PROPERTIES ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:oopsie_shim>")

add_executable(test_parse_speed tests/test_parse_speed.c)
target_link_libraries(test_parse_speed PRIVATE oopsie_wal)
add_test(NAME test_parse_speed COMMAND test_parse_speed)
set_tests_properties(test_parse_speed PROPERTIES ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:oopsie_shim>")

# New Purger Tests
add_executable(test_purge_basic tests/test_purge_basic.c)
target_link_libraries(test_purge_basic PRIVATE oopsie_wal)
add_test(NAME test_purge_basic COMMAND test_purge_basic)
set_tests_properties(test_purge_basic PROPERTIES ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:oopsie_shim>")

add_executable(test_purge_boundary tests/test_purge_boundary.c)
target_link_libraries(test_purge_boundary PRIVATE oopsie_wal)
add_test(NAME test_purge_boundary COMMAND test_purge_boundary)
set_tests_properties(test_purge_boundary PROPERTIES ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:oopsie_shim>")

add_executable(test_purge_speed tests/test_purge_speed.c)
target_link_libraries(test_purge_speed PRIVATE oopsie_wal)
add_test(NAME test_purge_speed COMMAND test_purge_speed)
set_tests_properties(test_purge_speed PROPERTIES ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:oopsie_shim>")

# New Compactor Tests
add_executable(test_compact_basic tests/test_compact_basic.c)
target_link_libraries(test_compact_basic PRIVATE oopsie_wal)
add_test(NAME test_compact_basic COMMAND test_compact_basic)
set_tests_properties(test_compact_basic PROPERTIES ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:oopsie_shim>")

add_executable(test_compact_boundary tests/test_compact_boundary.c)
target_link_libraries(test_compact_boundary PRIVATE oopsie_wal)
add_test(NAME test_compact_boundary COMMAND test_compact_boundary)
set_tests_properties(test_compact_boundary PROPERTIES ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:oopsie_shim>")

add_executable(test_compact_speed tests/test_compact_speed.c)
target_link_libraries(test_compact_speed PRIVATE oopsie_wal)
add_test(NAME test_compact_speed COMMAND test_compact_speed)
set_tests_properties(test_compact_speed PROPERTIES ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:oopsie_shim>")

# New Compare Tests
add_executable(test_cmp_basic tests/test_cmp_basic.c)
target_link_libraries(test_cmp_basic PRIVATE oopsie_wal)
add_test(NAME test_cmp_basic COMMAND test_cmp_basic)

add_executable(test_cmp_boundary tests/test_cmp_boundary.c)
target_link_libraries(test_cmp_boundary PRIVATE oopsie_wal)
add_test(NAME test_cmp_boundary COMMAND test_cmp_boundary)

add_executable(test_cmp_speed tests/test_cmp_speed.c)
target_link_libraries(test_cmp_speed PRIVATE oopsie_wal)
add_test(NAME test_cmp_speed COMMAND test_cmp_speed)
