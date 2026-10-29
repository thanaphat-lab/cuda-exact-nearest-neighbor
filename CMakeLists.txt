cmake_minimum_required(VERSION 3.21)

project(cuda_exact_nearest_neighbor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# S5.2 establishes infrastructure only. CPU/CUDA targets are added only after
# their design and implementation milestones are approved.

include(CTest)
find_package(Git REQUIRED)

add_library(s6_cpu_baseline STATIC
    src/cpu_baseline.cpp
)
target_include_directories(s6_cpu_baseline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(s6_cpu_baseline PRIVATE /W4 /WX /fp:strict /permissive-)
else()
    target_compile_options(s6_cpu_baseline PRIVATE
        -Wall -Wextra -Wpedantic -Werror -fno-fast-math -frounding-math
        -ffp-contract=off
    )
endif()

set(S6_EVIDENCE_INPUTS
    CMakeLists.txt
    cmake/GenerateCpuEvidence.cmake
    cmake/SourceIdentityTests.cmake
    cmake/VerifySourceIdentity.cmake
    cmake/cpu_build_identity.hpp.in
    include/s6/cpu_baseline.hpp
    include/s6/cpu_evidence.hpp
    src/cpu_baseline.cpp
    src/cpu_evidence.cpp
    src/cpu_evidence_internal.hpp
    tests/cpu_baseline_tests.cpp
    tests/cpu_evidence_tests.cpp
    tests/cpu_matrix_tests.cpp
    docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md
)
foreach(relative_path IN LISTS S6_EVIDENCE_INPUTS)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}")
endforeach()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE S6_BUILD_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" status --porcelain=v1 --untracked-files=all
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE S6_BUILD_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
if(S6_BUILD_STATUS STREQUAL "")
    set(S6_WORKTREE_STATE "clean")
else()
    set(S6_WORKTREE_STATE "dirty")
endif()
string(SHA256 S6_WORKTREE_FINGERPRINT "${S6_BUILD_STATUS}")
set(S6_SOURCE_IDENTITY_CONTENT "")
set(S6_EVIDENCE_INPUT_ENTRIES "")
foreach(relative_path IN LISTS S6_EVIDENCE_INPUTS)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}" source_sha256)
    string(APPEND S6_SOURCE_IDENTITY_CONTENT
        "source=${relative_path}|${source_sha256}\n")
    string(APPEND S6_EVIDENCE_INPUT_ENTRIES
        "    {R\"s6(${relative_path})s6\", \"${source_sha256}\"},\n")
endforeach()
string(SHA256 S6_SOURCE_MANIFEST_SHA256 "${S6_SOURCE_IDENTITY_CONTENT}")
file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/include/s6/cpu_baseline.hpp S6_BASELINE_HEADER_SHA256)
file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/include/s6/cpu_evidence.hpp S6_EVIDENCE_HEADER_SHA256)
file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/src/cpu_baseline.cpp S6_BASELINE_SOURCE_SHA256)
file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/src/cpu_evidence.cpp S6_EVIDENCE_SOURCE_SHA256)
file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt S6_CMAKE_SHA256)
file(SHA256
    ${CMAKE_CURRENT_SOURCE_DIR}/docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md
    S6_POLICY_SHA256
)
set(S6_REQUIRED_COMPILE_FLAGS "/O2|/Ob2|/DNDEBUG|-std:c++17|/W4|/WX|/fp:strict|/permissive-")
get_filename_component(S6_COMPILER_DIRECTORY ${CMAKE_CXX_COMPILER} DIRECTORY)
set(S6_DUMPBIN "${S6_COMPILER_DIRECTORY}/dumpbin.exe")
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" S6_CONFIGURED_SOURCE_ROOT)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_BINARY_DIR}" S6_CONFIGURED_BUILD_ROOT)
file(TO_CMAKE_PATH "${CMAKE_CXX_COMPILER}" S6_CONFIGURED_COMPILER_PATH)
file(TO_CMAKE_PATH "${GIT_EXECUTABLE}" S6_CONFIGURED_GIT_PATH)
if(NOT EXISTS ${S6_DUMPBIN})
    message(FATAL_ERROR "MSVC dumpbin.exe is required for CPU graph evidence")
endif()
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cpu_build_identity.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/s6/cpu_build_identity.hpp
    @ONLY
)
set(S6_SOURCE_IDENTITY_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/s6/source_identity.txt")
file(WRITE "${S6_SOURCE_IDENTITY_FILE}"
    "schema=S6-SOURCE-IDENTITY-v1.0\n"
    "revision=${S6_BUILD_REVISION}\n"
    "working_tree_state=${S6_WORKTREE_STATE}\n"
    "working_tree_fingerprint=${S6_WORKTREE_FINGERPRINT}\n"
    "source_manifest_sha256=${S6_SOURCE_MANIFEST_SHA256}\n"
    "${S6_SOURCE_IDENTITY_CONTENT}")

add_custom_target(s6_verify_source_identity ALL
    COMMAND ${CMAKE_COMMAND}
        -DS6_IDENTITY_FILE=${S6_SOURCE_IDENTITY_FILE}
        -DS6_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DS6_GIT=${GIT_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifySourceIdentity.cmake
    VERBATIM
)
add_dependencies(s6_cpu_baseline s6_verify_source_identity)

add_library(s6_cpu_evidence STATIC
    src/cpu_evidence.cpp
)
add_dependencies(s6_cpu_evidence s6_verify_source_identity)
target_include_directories(s6_cpu_evidence PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(s6_cpu_evidence PUBLIC s6_cpu_baseline)
if(MSVC)
    target_compile_options(s6_cpu_evidence PRIVATE /W4 /WX /fp:strict /permissive-)
    target_link_libraries(s6_cpu_evidence PRIVATE bcrypt)
else()
    message(FATAL_ERROR "S6 CPU evidence closure currently requires the approved MSVC toolchain")
endif()

if(BUILD_TESTING)
    add_executable(s6_cpu_tests tests/cpu_baseline_tests.cpp)
    target_link_libraries(s6_cpu_tests PRIVATE s6_cpu_evidence)

    add_executable(s6_cpu_matrix_tests tests/cpu_matrix_tests.cpp)
    target_link_libraries(s6_cpu_matrix_tests PRIVATE s6_cpu_evidence)

    add_executable(s6_cpu_evidence_tests tests/cpu_evidence_tests.cpp)
    target_link_libraries(s6_cpu_evidence_tests PRIVATE s6_cpu_evidence)
    add_dependencies(s6_cpu_tests s6_verify_source_identity)
    add_dependencies(s6_cpu_matrix_tests s6_verify_source_identity)
    add_dependencies(s6_cpu_evidence_tests s6_verify_source_identity)

    if(MSVC)
        target_compile_options(s6_cpu_tests PRIVATE /W4 /WX /fp:strict /permissive-)
        target_compile_options(s6_cpu_matrix_tests PRIVATE /W4 /WX /fp:strict /permissive-)
        target_compile_options(s6_cpu_evidence_tests PRIVATE /W4 /WX /fp:strict /permissive-)
    else()
        target_compile_options(s6_cpu_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror -fno-fast-math -frounding-math
            -ffp-contract=off
        )
        target_compile_options(s6_cpu_matrix_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror -fno-fast-math -frounding-math
            -ffp-contract=off
        )
    endif()

    function(s6_add_cpu_evidence_artifacts target)
        set(map_file "${CMAKE_CURRENT_BINARY_DIR}/${target}.map")
        set(prefix "${CMAKE_CURRENT_BINARY_DIR}/evidence/${target}")
        set(manifest "${prefix}.manifest.txt")
        target_link_options(${target} PRIVATE "/MAP:${map_file}")
        add_custom_command(
            OUTPUT ${manifest}
            COMMAND ${CMAKE_COMMAND}
                -DS6_DUMPBIN=${S6_DUMPBIN}
                -DS6_TARGET_NAME=${target}
                -DS6_EXECUTABLE=$<TARGET_FILE:${target}>
                -DS6_LIBRARY=$<TARGET_FILE:s6_cpu_baseline>
                -DS6_OBJECT=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/s6_cpu_baseline.dir/src/cpu_baseline.cpp.obj
                -DS6_MAP=${map_file}
                -DS6_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
                -DS6_BUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}
                -DS6_COMPILER=${CMAKE_CXX_COMPILER}
                -DS6_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}
                -DS6_COMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}
                -DS6_REVISION=${S6_BUILD_REVISION}
                -DS6_WORKTREE_STATE=${S6_WORKTREE_STATE}
                -DS6_REQUIRED_FLAGS=${S6_REQUIRED_COMPILE_FLAGS}
                -DS6_IDENTITY_FILE=${S6_SOURCE_IDENTITY_FILE}
                -DS6_GIT=${GIT_EXECUTABLE}
                -DS6_MANIFEST=${manifest}
                -DS6_PREFIX=${prefix}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCpuEvidence.cmake
            DEPENDS ${target} s6_cpu_baseline
                ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCpuEvidence.cmake
                ${CMAKE_CURRENT_SOURCE_DIR}/src/cpu_baseline.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/include/s6/cpu_baseline.hpp
                ${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
                ${S6_SOURCE_IDENTITY_FILE}
            VERBATIM
        )
        add_custom_target(${target}_evidence_artifacts ALL DEPENDS ${manifest})
        add_dependencies(${target}_evidence_artifacts s6_verify_source_identity)
        set_property(TARGET ${target}_evidence_artifacts PROPERTY FOLDER evidence)
        set(${target}_EVIDENCE_MANIFEST ${manifest} PARENT_SCOPE)
    endfunction()

    s6_add_cpu_evidence_artifacts(s6_cpu_tests)
    s6_add_cpu_evidence_artifacts(s6_cpu_matrix_tests)
    s6_add_cpu_evidence_artifacts(s6_cpu_evidence_tests)

    set(S6_FIXTURE_EVIDENCE
        "${CMAKE_CURRENT_BINARY_DIR}/evidence/s6_cpu_fixtures.status.txt")
    set(S6_MATRIX_REPORT_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/evidence/matrix-report")
    add_test(NAME s6.cpu.fixtures
        COMMAND s6_cpu_tests ${s6_cpu_tests_EVIDENCE_MANIFEST}
            ${S6_FIXTURE_EVIDENCE})
    add_test(NAME s6.cpu.evidence
        COMMAND s6_cpu_evidence_tests ${s6_cpu_evidence_tests_EVIDENCE_MANIFEST})
    add_test(NAME s6.cpu.source_identity
        COMMAND ${CMAKE_COMMAND}
            -DS6_IDENTITY_FILE=${S6_SOURCE_IDENTITY_FILE}
            -DS6_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -DS6_GIT=${GIT_EXECUTABLE}
            -DS6_OUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/evidence/identity-negative
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/SourceIdentityTests.cmake)
    add_test(NAME s6.cpu.required_matrix
        COMMAND s6_cpu_matrix_tests ${s6_cpu_matrix_tests_EVIDENCE_MANIFEST}
            ${S6_FIXTURE_EVIDENCE} ${S6_MATRIX_REPORT_ROOT})
    set_tests_properties(s6.cpu.fixtures PROPERTIES
        FIXTURES_SETUP S6_CPU_FIXTURES)
    set_tests_properties(s6.cpu.required_matrix PROPERTIES
        FIXTURES_REQUIRED S6_CPU_FIXTURES)
    set_tests_properties(s6.cpu.fixtures s6.cpu.evidence s6.cpu.source_identity
        s6.cpu.required_matrix
        PROPERTIES TIMEOUT 900
    )
endif()
