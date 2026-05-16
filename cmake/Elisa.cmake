include(CMakeParseArguments)

find_program(ELISA_GO_EXECUTABLE NAMES go)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(ELISACORE_COMPILER_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../compiler"
    CACHE PATH "Path to the Elisa-core compiler directory containing go.mod")

function(_elisa_infer_target_triple out_var)
    set(inferred "")
    if(APPLE)
        set(arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
        if(CMAKE_OSX_ARCHITECTURES)
            list(LENGTH CMAKE_OSX_ARCHITECTURES arch_count)
            if(NOT arch_count EQUAL 1)
                message(FATAL_ERROR "Elisa C ABI modules currently require a single CMAKE_OSX_ARCHITECTURES value")
            endif()
            list(GET CMAKE_OSX_ARCHITECTURES 0 arch)
        endif()

        set(deployment_target "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        if(NOT deployment_target)
            set(deployment_target "11.0")
        endif()

        if(arch STREQUAL "arm64")
            set(inferred "arm64-apple-macosx${deployment_target}")
        elseif(arch STREQUAL "x86_64" OR arch STREQUAL "amd64")
            set(inferred "x86_64-apple-macosx${deployment_target}")
        else()
            message(FATAL_ERROR "Unsupported Elisa C ABI macOS architecture '${arch}'")
        endif()
    endif()
    set("${out_var}" "${inferred}" PARENT_SCOPE)
endfunction()

function(elisa_add_module target_name)
    cmake_parse_arguments(ELISA "" "SOURCE;ABI_HEADER;OUTPUT_DIR;COMPILER_DIR;TARGET_TRIPLE" "" ${ARGN})

    if(NOT ELISA_SOURCE)
        message(FATAL_ERROR "elisa_add_module(${target_name}) requires SOURCE")
    endif()
    if(NOT ELISA_ABI_HEADER)
        message(FATAL_ERROR "elisa_add_module(${target_name}) requires ABI_HEADER")
    endif()
    if(NOT ELISA_GO_EXECUTABLE)
        message(FATAL_ERROR "Go is required to build Elisa C ABI modules")
    endif()

    set(compiler_dir "${ELISACORE_COMPILER_DIR}")
    if(ELISA_COMPILER_DIR)
        set(compiler_dir "${ELISA_COMPILER_DIR}")
    endif()
    if(NOT IS_ABSOLUTE "${compiler_dir}")
        get_filename_component(compiler_dir "${compiler_dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    set(target_triple "${ELISA_TARGET_TRIPLE}")
    if(NOT target_triple)
        _elisa_infer_target_triple(target_triple)
    endif()

    get_filename_component(source_abs "${ELISA_SOURCE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(header_abs "${ELISA_ABI_HEADER}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(header_dir "${header_abs}" DIRECTORY)

    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/elisa")
    if(ELISA_OUTPUT_DIR)
        set(output_dir "${ELISA_OUTPUT_DIR}")
        if(NOT IS_ABSOLUTE "${output_dir}")
            get_filename_component(output_dir "${output_dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
    endif()

    set(archive_path "${output_dir}/lib${target_name}.a")
    set(manifest_path "${output_dir}/lib${target_name}.elisa-abi.json")
    set(header_audit_path "${output_dir}/lib${target_name}.h")
    set(unsafe_report_path "${output_dir}/lib${target_name}.unsafe.txt")
    set(compiler_args run ./src -emit c-archive -o "${archive_path}")
    if(target_triple)
        list(APPEND compiler_args -target-triple "${target_triple}")
    endif()
    list(APPEND compiler_args "${source_abs}")

    add_custom_command(
        OUTPUT "${archive_path}"
        BYPRODUCTS "${manifest_path}" "${header_audit_path}" "${unsafe_report_path}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_dir}"
        COMMAND "${ELISA_GO_EXECUTABLE}" ${compiler_args}
        WORKING_DIRECTORY "${compiler_dir}"
        DEPENDS "${source_abs}" "${header_abs}"
        VERBATIM
        COMMENT "Building Elisa C ABI archive ${target_name}")

    add_custom_target("${target_name}_elisa_build" DEPENDS "${archive_path}")
    add_custom_target("${target_name}_abi_check"
        COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/elisa/native"
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/elisa/native/check_c_abi_header.py"
            "${header_abs}" "${header_audit_path}"
        DEPENDS "${archive_path}" "${header_abs}" "${CMAKE_CURRENT_SOURCE_DIR}/elisa/native/check_c_abi_header.py"
        VERBATIM
        COMMENT "Checking Elisa C ABI header drift for ${target_name}")
    add_library("${target_name}" STATIC IMPORTED GLOBAL)
    add_dependencies("${target_name}" "${target_name}_elisa_build")
    set_target_properties("${target_name}" PROPERTIES
        IMPORTED_LOCATION "${archive_path}"
        INTERFACE_INCLUDE_DIRECTORIES "${header_dir}"
    )
endfunction()
