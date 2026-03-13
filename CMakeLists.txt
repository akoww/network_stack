cmake_minimum_required(VERSION 3.20)
project(network_stack 
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "Modern C++23 Network Stack with ASIO"
)

# ============================================================================
# C++23 Standard Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)


# ============================================================================
# Include Custom Macros
# ============================================================================
# If you created a 'cmake' directory, use "cmake/macros.cmake"
# If the file is in the root, use "macros.cmake"
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/makro_separate_tests.cmake")
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/makro_separate_tests.cmake)
else()
    include(makro_separate_tests.cmake)
endif()

# ============================================================================
# ASIO Dependency
# ============================================================================
find_package(ASIO CONFIG QUIET)

if(NOT ASIO_FOUND)
    message(STATUS "ASIO not found as package, attempting to use system headers...")
    find_path(ASIO_INCLUDE_DIR asio.hpp 
        PATHS
            /usr/include
            /usr/local/include
            /opt/local/include
            $ENV{ASIO_ROOT}/include
            $ENV{INCLUDE}
        DOC "Path to ASIO headers"
    )
    if(ASIO_INCLUDE_DIR)
        message(STATUS "Found ASIO headers at: ${ASIO_INCLUDE_DIR}")
        add_library(ASIO::ASIO INTERFACE IMPORTED)
        set_property(TARGET ASIO::ASIO PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${ASIO_INCLUDE_DIR}")
    else()
        message(FATAL_ERROR "ASIO headers not found. Please install ASIO or set ASIO_ROOT.")
    endif()
endif()
# ============================================================================
# GTest Dependency
# ============================================================================
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# ============================================================================
# SPDLOG Dependency
# ============================================================================
find_package(spdlog REQUIRED)


# ============================================================================
# Library Target: network_stack_core
# ============================================================================
add_library(network_stack_core STATIC)

# Include directories
target_include_directories(network_stack_core 
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${ASIO_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Source files
target_sources(network_stack_core 
    PRIVATE
        src/core/NetworkService.cpp
        src/core/Context.cpp
        src/socket/AsioTcpSocket.cpp
        src/socket/TcpImpl.cpp
        src/client/ClientAsync.cpp
        src/client/ClientSync.cpp
        src/server/ServerAsync.cpp
        src/server/ServerSync.cpp
)

# Compile options
target_compile_options(network_stack_core PRIVATE 
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# Link ASIO
target_link_libraries(network_stack_core 
    PUBLIC
        ASIO::ASIO 
        spdlog::spdlog
)

# ============================================================================
# Test Executable: network_stack_tests
# ============================================================================
enable_testing()

set(TEST_SOURCES
    tests/test_error_code.cpp
    tests/test_socket_interface.cpp
    tests/test_client_async.cpp
    #tests/test_context_wrapper.cpp
)

add_separate_tests(TEST_SOURCES)