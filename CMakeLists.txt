# Minimum required CMake version
cmake_minimum_required(VERSION 3.28)

# Project name
project(EF_LIB_DRAFT LANGUAGES C)

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
target_link_libraries(${PROJECT_NAME} PRIVATE c czmq zmq)

target_include_directories(generator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(generator PRIVATE c czmq zmq)

add_custom_target(remove_generated ALL
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

add_dependencies(${PROJECT_NAME} remove_generated)
add_dependencies(generator remove_generated)

add_custom_target(create_generated_dir ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/generated
    DEPENDS remove_generated
)

add_custom_target(run_generator ALL
    COMMAND generator ${CMAKE_CURRENT_SOURCE_DIR}/main.c ${CMAKE_CURRENT_SOURCE_DIR}/generated/
    DEPENDS create_generated_dir ${PROJECT_NAME} generator
    )


