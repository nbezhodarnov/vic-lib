if(NOT ROOT_BUILD_DIR)
    message(FATAL_ERROR "ROOT_BUILD_DIR is not defined")
    return()
endif()

if(NOT ROOT_SOURCE_DIR)
    message(FATAL_ERROR "ROOT_SOURCE_DIR is not defined")
    return()
endif()

if(NOT GENERATED_SOURCE_DIR)
    message(FATAL_ERROR "GENERATED_SOURCE_DIR is not defined")
    return()
endif()

file(REMOVE ${ROOT_BUILD_DIR}/CMakeCache.txt)

include(${ROOT_SOURCE_DIR}/cmake/cmakelists_generator.cmake)

file(GLOB_RECURSE GENERATED_SOURCES 
    ${ROOT_BUILD_DIR}/generated/*.c
)

foreach(GENERATED_SOURCE ${GENERATED_SOURCES})
    get_filename_component(GENERATED_SOURCE_NAME ${GENERATED_SOURCE} NAME_WE)

    generate_cmakelists_executable(
        OUTPUT_DIR ${GENERATED_SOURCE_DIR}
        EXECUTABLE_NAME ${GENERATED_SOURCE_NAME}
        SOURCES ${GENERATED_SOURCE} [=[${LIB_SOURCES}]=] [=[${THIRD_PARTY_SOURCES}]=]
    )
    
    generate_cmakelists_target_link(
        OUTPUT_DIR ${GENERATED_SOURCE_DIR}
        TARGET_NAME ${GENERATED_SOURCE_NAME}
        LINK_TARGETS [=[${TARGET_LINK_LIBRARIES}]=]
    )
    
    generate_cmakelists_target_include_directories(
        OUTPUT_DIR ${GENERATED_SOURCE_DIR}
        TARGET_NAME ${GENERATED_SOURCE_NAME}
        INCLUDE_DIRECTORIES [=[${TARGET_INCLUDE_DIRECTORIES}]=] [=[${ROOT_SOURCE_DIR}]=]
    )

    generate_cmakelists_set_target_properties(
        OUTPUT_DIR ${GENERATED_SOURCE_DIR}
        TARGET_NAME ${GENERATED_SOURCE_NAME}
        PROPERTIES 
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
    
endforeach()


