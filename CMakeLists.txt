# Minimum required CMake version
cmake_minimum_required(VERSION 3.28)

# Project name
project(EF_LIB_DRAFT LANGUAGES C)

file(GLOB_RECURSE SOURCES "*.c" "*.h")

list(FILTER SOURCES EXCLUDE REGEX "build/*")

# Add source files
add_executable(${PROJECT_NAME} 
    ${SOURCES}
    )

# Link ZeroMQ library
target_link_libraries(${PROJECT_NAME} PRIVATE c czmq zmq)
