function(generate_cmakelists_header)
    set(_OPTIONS_ARGS)
    set(_ONE_VALUE_ARGS OUTPUT_DIR PROJECT_NAME)
    set(_MULTI_VALUE_ARGS)

    cmake_parse_arguments(
        _GENERATE_CMAKELISTS_HEADER
        "${_OPTIONS_ARGS}"
        "${_ONE_VALUE_ARGS}"
        "${_MULTI_VALUE_ARGS}"
        "${ARGN}"
    )

    file(MAKE_DIRECTORY ${_GENERATE_CMAKELISTS_HEADER_OUTPUT_DIR})
    
    file(WRITE ${_GENERATE_CMAKELISTS_HEADER_OUTPUT_DIR}/CMakeLists.txt
        "cmake_minimum_required(VERSION 3.28)\n\n"
        "project(${_GENERATE_CMAKELISTS_HEADER_PROJECT_NAME} LANGUAGES C)\n\n"
    )
endfunction()

function(generate_cmakelists_list)
    set(_OPTIONS_ARGS)
    set(_ONE_VALUE_ARGS OUTPUT_DIR)
    set(_MULTI_VALUE_ARGS LIST LIST_NAME)

    cmake_parse_arguments(
        _GENERATE_CMAKELISTS_LIST
        "${_OPTIONS_ARGS}"
        "${_ONE_VALUE_ARGS}"
        "${_MULTI_VALUE_ARGS}"
        "${ARGN}"
    )
    
    file(APPEND ${_GENERATE_CMAKELISTS_LIST_OUTPUT_DIR}/CMakeLists.txt
        "set(${_GENERATE_CMAKELISTS_LIST_LIST_NAME}\n"
        "  ${_GENERATE_CMAKELISTS_LIST_LIST}\n"
        ")\n\n"
    )
endfunction()

function(generate_cmakelists_executable)
    set(_OPTIONS_ARGS)
    set(_ONE_VALUE_ARGS OUTPUT_DIR EXECUTABLE_NAME)
    set(_MULTI_VALUE_ARGS SOURCES)

    cmake_parse_arguments(
        _GENERATE_CMAKELISTS_EXECUTABLE
        "${_OPTIONS_ARGS}"
        "${_ONE_VALUE_ARGS}"
        "${_MULTI_VALUE_ARGS}"
        "${ARGN}"
    )
    
    file(APPEND ${_GENERATE_CMAKELISTS_EXECUTABLE_OUTPUT_DIR}/CMakeLists.txt
        "add_executable(${_GENERATE_CMAKELISTS_EXECUTABLE_EXECUTABLE_NAME}\n"
        "  ${_GENERATE_CMAKELISTS_EXECUTABLE_SOURCES}\n"
        ")\n\n"
    )
endfunction()

function(generate_cmakelists_target_link)
    set(_OPTIONS_ARGS)
    set(_ONE_VALUE_ARGS OUTPUT_DIR TARGET_NAME)
    set(_MULTI_VALUE_ARGS LINK_TARGETS)

    cmake_parse_arguments(
        _GENERATE_CMAKELISTS_TARGET_LINK
        "${_OPTIONS_ARGS}"
        "${_ONE_VALUE_ARGS}"
        "${_MULTI_VALUE_ARGS}"
        "${ARGN}"
    )
    
    file(APPEND ${_GENERATE_CMAKELISTS_TARGET_LINK_OUTPUT_DIR}/CMakeLists.txt
        "target_link_libraries(${_GENERATE_CMAKELISTS_TARGET_LINK_TARGET_NAME} PRIVATE\n"
        "  ${_GENERATE_CMAKELISTS_TARGET_LINK_LINK_TARGETS}\n"
        ")\n\n"
    )
endfunction()

function(generate_cmakelists_target_include_directories)
    set(_OPTIONS_ARGS)
    set(_ONE_VALUE_ARGS OUTPUT_DIR TARGET_NAME)
    set(_MULTI_VALUE_ARGS INCLUDE_DIRECTORIES)

    cmake_parse_arguments(
        _GENERATE_CMAKELISTS_TARGET_INCLUDE_DIRECTORIES
        "${_OPTIONS_ARGS}"
        "${_ONE_VALUE_ARGS}"
        "${_MULTI_VALUE_ARGS}"
        "${ARGN}"
    )
    
    file(APPEND ${_GENERATE_CMAKELISTS_TARGET_INCLUDE_DIRECTORIES_OUTPUT_DIR}/CMakeLists.txt
        "target_include_directories(${_GENERATE_CMAKELISTS_TARGET_INCLUDE_DIRECTORIES_TARGET_NAME} PRIVATE\n"
        "  ${_GENERATE_CMAKELISTS_TARGET_INCLUDE_DIRECTORIES_INCLUDE_DIRECTORIES}\n"
        ")\n\n"
    )
endfunction()

function(generate_cmakelists_set_target_properties)
    set(_OPTIONS_ARGS)
    set(_ONE_VALUE_ARGS OUTPUT_DIR TARGET_NAME)
    set(_MULTI_VALUE_ARGS PROPERTIES)

    cmake_parse_arguments(
        _GENERATE_CMAKELISTS_SET_TARGET_PROPS
        "${_OPTIONS_ARGS}"
        "${_ONE_VALUE_ARGS}"
        "${_MULTI_VALUE_ARGS}"
        "${ARGN}"
    )
    
    file(APPEND ${_GENERATE_CMAKELISTS_SET_TARGET_PROPS_OUTPUT_DIR}/CMakeLists.txt
        "set_target_properties(${_GENERATE_CMAKELISTS_SET_TARGET_PROPS_TARGET_NAME} PROPERTIES\n"
        "  ${_GENERATE_CMAKELISTS_SET_TARGET_PROPS_PROPERTIES}\n"
        ")\n\n"
    )
endfunction()