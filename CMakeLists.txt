cmake_minimum_required(VERSION 3.10)
project(salamander C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# raylib - use parent project's external directory
set(RAYLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../external/raylib)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

# Check if raylib exists
if(EXISTS ${RAYLIB_DIR}/CMakeLists.txt)
    add_subdirectory(${RAYLIB_DIR} ${CMAKE_BINARY_DIR}/raylib)
else()
    message(FATAL_ERROR "raylib not found at ${RAYLIB_DIR}. Ensure you're building from the salamander directory.")
endif()

# Salamander sources
set(SALAMANDER_SOURCES
    src/main.c
    src/ssh_manager.c
    src/plugin_browser.c
)

add_executable(salamander ${SALAMANDER_SOURCES})

target_include_directories(salamander PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${RAYLIB_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../fonts
)

target_link_libraries(salamander
    raylib
    m
    pthread
)

target_compile_options(salamander PRIVATE -Wall -Wextra)

# Copy fonts to build directory for easy access
add_custom_command(TARGET salamander POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../../fonts/ZegoeUI-U.ttf
        ${CMAKE_BINARY_DIR}/fonts/ 2>/dev/null || true
    COMMENT "Copying fonts to build directory"
)
