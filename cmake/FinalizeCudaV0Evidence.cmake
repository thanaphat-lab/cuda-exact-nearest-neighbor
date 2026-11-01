cmake_minimum_required(VERSION 3.21)

foreach(required_variable IN ITEMS
        S7_SOURCE_ROOT S7_BUILD_ROOT S7_GIT S7_OUTPUT_ROOT S7_MANIFEST
        S7_FUNCTIONAL_EXECUTABLE S7_MATRIX_EXECUTABLE S7_LIBRARY
        S7_CONFIGURE_COMMAND S7_BUILD_COMMAND S7_CTEST_COMMAND
        S7_MEMCHECK_COMMAND S7_INITCHECK_COMMAND S7_RACECHECK_COMMAND)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "Missing consolidated-evidence input: ${required_variable}")
    endif()
endforeach()

set(required_paths
    "${S7_FUNCTIONAL_EXECUTABLE}"
    "${S7_MATRIX_EXECUTABLE}"
    "${S7_LIBRARY}"
    "${S7_OUTPUT_ROOT}/functional.txt"
    "${S7_OUTPUT_ROOT}/matrix.txt"
    "${S7_OUTPUT_ROOT}/sanitizer-memcheck.log"
    "${S7_OUTPUT_ROOT}/sanitizer-initcheck.log"
    "${S7_OUTPUT_ROOT}/sanitizer-racecheck.log"
    "${S7_OUTPUT_ROOT}/sanitizer-memcheck-functional.txt"
    "${S7_OUTPUT_ROOT}/sanitizer-initcheck-functional.txt"
    "${S7_OUTPUT_ROOT}/sanitizer-racecheck-functional.txt"
    "${S7_BUILD_ROOT}/compile_commands.json"
    "${S7_BUILD_ROOT}/generated/s6/source_identity.txt"
    "${S7_OUTPUT_ROOT}/binary.manifest.txt"
    "${S7_OUTPUT_ROOT}/cuda-v0.sass.txt"
    "${S7_SOURCE_ROOT}/CMakeLists.txt"
    "${S7_SOURCE_ROOT}/cmake/GenerateCudaV0Evidence.cmake"
    "${S7_SOURCE_ROOT}/cmake/FinalizeCudaV0Evidence.cmake"
    "${S7_SOURCE_ROOT}/include/s6/cuda_v0.hpp"
    "${S7_SOURCE_ROOT}/src/cuda_v0.cu"
    "${S7_SOURCE_ROOT}/tests/cuda_v0_tests.cpp"
    "${S7_SOURCE_ROOT}/tests/cuda_v0_matrix_tests.cpp")
foreach(path IN LISTS required_paths)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Required consolidated-evidence input missing: ${path}")
    endif()
endforeach()

execute_process(
    COMMAND "${S7_GIT}" rev-parse HEAD
    WORKING_DIRECTORY "${S7_SOURCE_ROOT}"
    OUTPUT_VARIABLE revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${S7_GIT}" status --porcelain=v1 --untracked-files=all
    WORKING_DIRECTORY "${S7_SOURCE_ROOT}"
    OUTPUT_VARIABLE working_tree_status
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
string(SHA256 working_tree_fingerprint "${working_tree_status}")
file(STRINGS "${S7_BUILD_ROOT}/generated/s6/source_identity.txt"
    identity_fingerprint_line REGEX "^working_tree_fingerprint=")
string(REPLACE "working_tree_fingerprint=" "" configured_fingerprint
    "${identity_fingerprint_line}")
if(NOT working_tree_fingerprint STREQUAL configured_fingerprint)
    message(FATAL_ERROR
        "Working-tree identity changed after the evidence-producing build")
endif()

set(artifact_lines "")
foreach(path IN LISTS required_paths)
    file(SHA256 "${path}" artifact_hash)
    string(APPEND artifact_lines "artifact=${path}|${artifact_hash}\n")
endforeach()
string(TIMESTAMP generated_at_utc "%Y-%m-%dT%H:%M:%SZ" UTC)
string(CONCAT finalize_command
    "\"${CMAKE_COMMAND}\" \"-DS7_SOURCE_ROOT=${S7_SOURCE_ROOT}\""
    " \"-DS7_BUILD_ROOT=${S7_BUILD_ROOT}\" \"-DS7_GIT=${S7_GIT}\""
    " \"-DS7_OUTPUT_ROOT=${S7_OUTPUT_ROOT}\" \"-DS7_MANIFEST=${S7_MANIFEST}\""
    " \"-DS7_FUNCTIONAL_EXECUTABLE=${S7_FUNCTIONAL_EXECUTABLE}\""
    " \"-DS7_MATRIX_EXECUTABLE=${S7_MATRIX_EXECUTABLE}\""
    " \"-DS7_LIBRARY=${S7_LIBRARY}\""
    " \"-DS7_CONFIGURE_COMMAND=${S7_CONFIGURE_COMMAND}\""
    " \"-DS7_BUILD_COMMAND=${S7_BUILD_COMMAND}\""
    " \"-DS7_CTEST_COMMAND=${S7_CTEST_COMMAND}\""
    " \"-DS7_MEMCHECK_COMMAND=${S7_MEMCHECK_COMMAND}\""
    " \"-DS7_INITCHECK_COMMAND=${S7_INITCHECK_COMMAND}\""
    " \"-DS7_RACECHECK_COMMAND=${S7_RACECHECK_COMMAND}\""
    " -P \"${S7_SOURCE_ROOT}/cmake/FinalizeCudaV0Evidence.cmake\"")

file(WRITE "${S7_MANIFEST}"
"schema=S7-CUDA-V0-CONSOLIDATED-EVIDENCE-v1.0\n"
"evidence_class=local-uncommitted-not-acceptance\n"
"overall_acceptance=blocked/unverified\n"
"blocked_gate=independent-runtime-correlation-and-complete-acceptance-review\n"
"generated_at_utc=${generated_at_utc}\n"
"revision=${revision}\n"
"working_tree_state=dirty-authorized-local\n"
"working_tree_fingerprint=${working_tree_fingerprint}\n"
"command.configure=${S7_CONFIGURE_COMMAND}\n"
"command.build=${S7_BUILD_COMMAND}\n"
"command.ctest=${S7_CTEST_COMMAND}\n"
"command.memcheck=${S7_MEMCHECK_COMMAND}\n"
"command.initcheck=${S7_INITCHECK_COMMAND}\n"
"command.racecheck=${S7_RACECHECK_COMMAND}\n"
"command.finalize=${finalize_command}\n"
"${artifact_lines}")
