# Minimum required CMake version
cmake_minimum_required(VERSION 3.28)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmakelists_generator.cmake)

# Project name
project(vic_project LANGUAGES C)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(COMMON_DEPENDENCIES
    c
    czmq
    zmq
)

file(GLOB_RECURSE LIB_SOURCES 
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.c 
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.h)

file(GLOB_RECURSE THIRD_PARTY_SOURCES 
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/*.h)


# Add source files
add_executable(${PROJECT_NAME} 
    ${LIB_SOURCES}
    ${THIRD_PARTY_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
)

add_executable(generator 
    ${LIB_SOURCES}
    ${THIRD_PARTY_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/generator.c
)

# Link ZeroMQ library
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE ${COMMON_DEPENDENCIES})

target_include_directories(generator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(generator PRIVATE ${COMMON_DEPENDENCIES})

add_custom_target(remove_generated ALL
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/generated
)

add_dependencies(${PROJECT_NAME} remove_generated)
add_dependencies(generator remove_generated)

add_custom_target(create_generated_dir ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/tmp/CMakeLists.txt ${CMAKE_BINARY_DIR}/generated/CMakeLists.txt
    DEPENDS remove_generated
)

add_custom_target(run_generator ALL
    COMMAND generator ${CMAKE_CURRENT_SOURCE_DIR}/main.c ${CMAKE_BINARY_DIR}/generated
    DEPENDS create_generated_dir ${PROJECT_NAME} generator
)

generate_cmakelists_header(OUTPUT_DIR ${CMAKE_BINARY_DIR}/tmp PROJECT_NAME ${PROJECT_NAME}_generated)

generate_cmakelists_list(OUTPUT_DIR ${CMAKE_BINARY_DIR}/tmp
    LIST
    ${LIB_SOURCES}
    LIST_NAME
    LIB_SOURCES
)

generate_cmakelists_list(OUTPUT_DIR ${CMAKE_BINARY_DIR}/tmp
    LIST
    ${CMAKE_CURRENT_SOURCE_DIR}
    LIST_NAME
    ROOT_SOURCE_DIR
)

generate_cmakelists_list(OUTPUT_DIR ${CMAKE_BINARY_DIR}/tmp
    LIST
    ${THIRD_PARTY_SOURCES}
    LIST_NAME
    THIRD_PARTY_SOURCES
)

# Get the list of include directories
get_target_property(TARGET_INCLUDE_DIRECTORIES ${PROJECT_NAME} INCLUDE_DIRECTORIES)

# Get the list of linked libraries
get_target_property(TARGET_LINK_LIBRARIES ${PROJECT_NAME} LINK_LIBRARIES)

generate_cmakelists_list(OUTPUT_DIR ${CMAKE_BINARY_DIR}/tmp
    LIST
    ${TARGET_INCLUDE_DIRECTORIES}
    LIST_NAME
    TARGET_INCLUDE_DIRECTORIES
)

generate_cmakelists_list(OUTPUT_DIR ${CMAKE_BINARY_DIR}/tmp
    LIST
    ${TARGET_LINK_LIBRARIES}
    LIST_NAME
    TARGET_LINK_LIBRARIES
)

add_custom_target(run_post_generator ALL
    COMMAND 
    ${CMAKE_COMMAND}
    -DROOT_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    -DROOT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -DGENERATED_SOURCE_DIR="${CMAKE_CURRENT_BINARY_DIR}/generated"
    -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
    -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmakelist_post_generate.cmake 
    DEPENDS run_generator
    COMMENT "Running post generator"
)

add_custom_target(run_post_generator_cmake ALL
    COMMAND ${CMAKE_COMMAND} -B${CMAKE_BINARY_DIR}/generated/build -S${CMAKE_BINARY_DIR}/generated -G ${CMAKE_GENERATOR}
    DEPENDS run_post_generator
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/generated/build
    COMMENT "Running post generator cmake"
)