cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES OR
   CMAKE_CUDA_ARCHITECTURES STREQUAL "")
    message(FATAL_ERROR
        "Set CMAKE_CUDA_ARCHITECTURES explicitly from the observed target GPU; "
        "the project does not approve a guessed hardware default")
endif()

project(cuda_exact_nearest_neighbor LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(S7_ALLOW_DIRTY_LOCAL_BUILD
    "Allow an explicitly labeled dirty local S7 build that cannot pass clean acceptance evidence"
    OFF)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "S6 acceptance evidence requires CMAKE_BUILD_TYPE=Release")
endif()

# S5.2 establishes infrastructure only. CPU/CUDA targets are added only after
# their design and implementation milestones are approved.

include(CTest)
find_package(Git REQUIRED)
find_package(CUDAToolkit REQUIRED)

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
    cmake/GenerateCudaV0Evidence.cmake
    cmake/FinalizeCudaV0Evidence.cmake
    include/s6/cuda_v0.hpp
    src/cuda_v0.cu
    tests/cuda_v0_tests.cpp
    tests/cuda_v0_matrix_tests.cpp
    tests/cuda_v0_matrix_evidence_tests.cpp
    cmake/GenerateCudaV1Evidence.cmake
    cmake/FinalizeCudaV1Evidence.cmake
    include/s6/cuda_v1.hpp
    src/cuda_v1.cu
    tests/cuda_v1_tests.cpp
    tests/cuda_v1_matrix_evidence_tests.cpp
    cmake/GenerateCudaV2Evidence.cmake
    cmake/FinalizeCudaV2Evidence.cmake
    include/s6/cuda_v2.hpp
    src/cuda_v2.cu
    tests/cuda_v2_tests.cpp
    tests/cuda_v2_matrix_evidence_tests.cpp
    cmake/GenerateCudaV3Evidence.cmake
    cmake/FinalizeCudaV3Evidence.cmake
    include/s6/cuda_v3.hpp
    src/cuda_v3.cu
    tests/cuda_v3_tests.cpp
    tests/cuda_v3_matrix_evidence_tests.cpp
    tests/s7_tuning_freeze.hpp
    include/s6/benchmark_internal.hpp
    src/s7_benchmark_cuda_internal.cuh
    benchmarks/s7_benchmark.cpp
    benchmarks/config/s7-accept-20261103-01.json
    benchmarks/config/s7-environment-approval.template.json
    scripts/run-s7-benchmarks.ps1
    scripts/capture-s7-telemetry.ps1
    scripts/analyze-s7-benchmarks.mjs
    benchmarks/s8_benchmark.cpp
    benchmarks/config/s8-transpose-ab-20261104-01.json
    scripts/run-s8-benchmarks.ps1
    scripts/analyze-s8-benchmarks.mjs
    benchmarks/s13_benchmark.cpp
    benchmarks/config/s13-minimal-v2.json
    scripts/run-s13-benchmarks.ps1
    scripts/analyze-s13-benchmarks.mjs
    benchmarks/s10_sift1m_loader.hpp
    benchmarks/s10_sift1m_loader.cpp
    benchmarks/s10_sift1m.cpp
    benchmarks/config/s10-sift1m.json
    tests/s10_sift1m_loader_tests.cpp
    scripts/run-s10-sift1m.ps1
    scripts/analyze-s10-sift1m.mjs
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
elseif(S7_ALLOW_DIRTY_LOCAL_BUILD)
    set(S6_WORKTREE_STATE "dirty-authorized-local")
    message(WARNING
        "S7 dirty local build enabled. Generated evidence is local-uncommitted "
        "and cannot satisfy clean acceptance gates.")
else()
    message(FATAL_ERROR
        "S6 acceptance evidence requires a clean working tree at configure time. "
        "S7 authorized local work may opt in with -DS7_ALLOW_DIRTY_LOCAL_BUILD=ON; "
        "such evidence remains explicitly non-acceptance.")
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
set(S6_BUILD_CONFIGURATION "${CMAKE_BUILD_TYPE}")
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
        -DS6_ALLOW_DIRTY_LOCAL_BUILD=${S7_ALLOW_DIRTY_LOCAL_BUILD}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifySourceIdentity.cmake
    VERBATIM
)
add_dependencies(s6_cpu_baseline s6_verify_source_identity)

add_library(s7_cuda_v0 STATIC
    src/cuda_v0.cu
)
add_dependencies(s7_cuda_v0 s6_verify_source_identity)
target_include_directories(s7_cuda_v0 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(s7_cuda_v0 PUBLIC CUDA::cudart)
target_compile_options(s7_cuda_v0 PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:
        --ftz=false
        --fmad=true
        --prec-div=true
        --prec-sqrt=true
        --generate-line-info
        -Xcompiler=/W4
        -Xcompiler=/WX
        -Xcompiler=/wd4211
        -Xcompiler=/fp:strict
        -Xcompiler=/permissive->
)
set_target_properties(s7_cuda_v0 PROPERTIES
    CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}"
)

add_library(s7_cuda_v1 STATIC
    src/cuda_v1.cu
)
add_dependencies(s7_cuda_v1 s6_verify_source_identity)
target_include_directories(s7_cuda_v1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(s7_cuda_v1 PUBLIC CUDA::cudart)
target_compile_options(s7_cuda_v1 PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:
        --ftz=false
        --fmad=true
        --prec-div=true
        --prec-sqrt=true
        --generate-line-info
        -Xcompiler=/W4
        -Xcompiler=/WX
        -Xcompiler=/wd4211
        -Xcompiler=/fp:strict
        -Xcompiler=/permissive->
)
set_target_properties(s7_cuda_v1 PROPERTIES
    CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}"
)

add_library(s7_cuda_v2 STATIC
    src/cuda_v2.cu
)
add_dependencies(s7_cuda_v2 s6_verify_source_identity)
target_include_directories(s7_cuda_v2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(s7_cuda_v2 PUBLIC CUDA::cudart)
target_compile_options(s7_cuda_v2 PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:
        --ftz=false
        --fmad=true
        --prec-div=true
        --prec-sqrt=true
        --generate-line-info
        -Xcompiler=/W4
        -Xcompiler=/WX
        -Xcompiler=/wd4211
        -Xcompiler=/fp:strict
        -Xcompiler=/permissive->
)
set_target_properties(s7_cuda_v2 PROPERTIES
    CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}"
)

add_library(s7_cuda_v3 STATIC
    src/cuda_v3.cu
)
add_dependencies(s7_cuda_v3 s6_verify_source_identity)
target_include_directories(s7_cuda_v3 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(s7_cuda_v3 PUBLIC CUDA::cudart)
target_compile_options(s7_cuda_v3 PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:
        --ftz=false
        --fmad=true
        --prec-div=true
        --prec-sqrt=true
        --generate-line-info
        -Xcompiler=/W4
        -Xcompiler=/WX
        -Xcompiler=/wd4211
        -Xcompiler=/fp:strict
        -Xcompiler=/permissive->
)
set_target_properties(s7_cuda_v3 PROPERTIES
    CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}"
)

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

add_executable(s7_benchmark benchmarks/s7_benchmark.cpp)
target_include_directories(s7_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(s7_benchmark PRIVATE
    s6_cpu_evidence s7_cuda_v0 s7_cuda_v1 s7_cuda_v2 s7_cuda_v3)
target_compile_definitions(s7_benchmark PRIVATE
    S7_BENCHMARK_REVISION="${S6_BUILD_REVISION}"
    S7_BENCHMARK_TREE_STATE="${S6_WORKTREE_STATE}")
target_compile_options(s7_benchmark PRIVATE /W4 /WX /fp:strict /permissive-)
add_dependencies(s7_benchmark s6_verify_source_identity)

add_executable(s8_benchmark benchmarks/s8_benchmark.cpp)
target_include_directories(s8_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(s8_benchmark PRIVATE
    s6_cpu_evidence s7_cuda_v1 s7_cuda_v2 s7_cuda_v3)
target_compile_definitions(s8_benchmark PRIVATE
    S8_BENCHMARK_REVISION="${S6_BUILD_REVISION}"
    S8_BENCHMARK_TREE_STATE="${S6_WORKTREE_STATE}")
target_compile_options(s8_benchmark PRIVATE /W4 /WX /fp:strict /permissive-)
add_dependencies(s8_benchmark s6_verify_source_identity)

add_executable(s13_benchmark benchmarks/s13_benchmark.cpp)
target_include_directories(s13_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(s13_benchmark PRIVATE s6_cpu_evidence s7_cuda_v2)
target_compile_definitions(s13_benchmark PRIVATE
    S13_BENCHMARK_REVISION="${S6_BUILD_REVISION}"
    S13_BENCHMARK_TREE_STATE="${S6_WORKTREE_STATE}")
target_compile_options(s13_benchmark PRIVATE /W4 /WX /fp:strict /permissive-)
add_dependencies(s13_benchmark s6_verify_source_identity)

add_library(s10_sift1m_io STATIC
    benchmarks/s10_sift1m_loader.cpp
)
target_include_directories(s10_sift1m_io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(s10_sift1m_io PRIVATE
    S10_REPOSITORY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(s10_sift1m_io PRIVATE /W4 /WX /fp:strict /permissive-)
add_dependencies(s10_sift1m_io s6_verify_source_identity)

add_executable(s10_sift1m benchmarks/s10_sift1m.cpp)
target_include_directories(s10_sift1m PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(s10_sift1m PRIVATE
    s10_sift1m_io s6_cpu_evidence
    s7_cuda_v0 s7_cuda_v1 s7_cuda_v2 s7_cuda_v3)
target_compile_definitions(s10_sift1m PRIVATE
    S10_BENCHMARK_REVISION="${S6_BUILD_REVISION}"
    S10_BENCHMARK_TREE_STATE="${S6_WORKTREE_STATE}")
target_compile_options(s10_sift1m PRIVATE /W4 /WX /fp:strict /permissive-)
add_dependencies(s10_sift1m s6_verify_source_identity)

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
                -DS6_BUILD_CONFIGURATION=${S6_BUILD_CONFIGURATION}
                -DS6_REVISION=${S6_BUILD_REVISION}
                -DS6_WORKTREE_STATE=${S6_WORKTREE_STATE}
                -DS6_REQUIRED_FLAGS=${S6_REQUIRED_COMPILE_FLAGS}
                -DS6_IDENTITY_FILE=${S6_SOURCE_IDENTITY_FILE}
                -DS6_GIT=${GIT_EXECUTABLE}
                -DS6_ALLOW_DIRTY_LOCAL_BUILD=${S7_ALLOW_DIRTY_LOCAL_BUILD}
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
    s6_add_cpu_evidence_artifacts(s10_sift1m)

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

    add_executable(s7_cuda_v0_tests tests/cuda_v0_tests.cpp)
    target_link_libraries(s7_cuda_v0_tests PRIVATE s7_cuda_v0 s6_cpu_evidence)
    target_compile_options(s7_cuda_v0_tests PRIVATE /W4 /WX /fp:strict /permissive-)
    add_dependencies(s7_cuda_v0_tests s6_verify_source_identity)

    add_executable(s7_cuda_v0_matrix_tests tests/cuda_v0_matrix_evidence_tests.cpp)
    target_link_libraries(s7_cuda_v0_matrix_tests PRIVATE s7_cuda_v0 s6_cpu_evidence)
    target_compile_options(s7_cuda_v0_matrix_tests PRIVATE
        /W4 /WX /fp:strict /permissive-)
    add_dependencies(s7_cuda_v0_matrix_tests s6_verify_source_identity)

    set(S7_CUDA_EVIDENCE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/evidence/cuda-v0")
    set(S7_CUDA_MATRIX_REPORT_ROOT
        "${S7_CUDA_EVIDENCE_ROOT}/matrix-report")
    set(S7_CUDA_EVIDENCE_MANIFEST
        "${S7_CUDA_EVIDENCE_ROOT}/binary.manifest.txt")
    add_test(NAME s7.cuda_v0.functional
        COMMAND s7_cuda_v0_tests
            ${S7_CUDA_EVIDENCE_ROOT}/functional.txt)
    add_test(NAME s7.cuda_v0.required_matrix
        COMMAND s7_cuda_v0_matrix_tests
            ${S7_CUDA_EVIDENCE_ROOT}/matrix.txt
            ${S7_CUDA_MATRIX_REPORT_ROOT}
            ${S7_CUDA_EVIDENCE_MANIFEST}
            ${S7_CUDA_EVIDENCE_ROOT}/functional.txt)
    set_tests_properties(s7.cuda_v0.functional s7.cuda_v0.required_matrix
        PROPERTIES TIMEOUT 1800)
    set_tests_properties(s7.cuda_v0.required_matrix PROPERTIES
        DEPENDS s7.cuda_v0.functional)

    find_program(S7_CUOBJDUMP cuobjdump
        HINTS "${CUDAToolkit_BIN_DIR}" "${CUDAToolkit_ROOT}/bin"
        REQUIRED)
    add_custom_command(
        OUTPUT ${S7_CUDA_EVIDENCE_MANIFEST}
        COMMAND ${CMAKE_COMMAND}
            -DS7_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -DS7_BUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}
            -DS7_GIT=${GIT_EXECUTABLE}
            -DS7_CUOBJDUMP=${S7_CUOBJDUMP}
            -DS7_EXECUTABLE=$<TARGET_FILE:s7_cuda_v0_tests>
            -DS7_MATRIX_EXECUTABLE=$<TARGET_FILE:s7_cuda_v0_matrix_tests>
            -DS7_LIBRARY=$<TARGET_FILE:s7_cuda_v0>
            -DS7_NVCC=${CMAKE_CUDA_COMPILER}
            -DS7_NVCC_VERSION=${CMAKE_CUDA_COMPILER_VERSION}
            -DS7_CUDA_ARCHITECTURES=${CMAKE_CUDA_ARCHITECTURES}
            -DS7_WORKING_TREE_STATE=${S6_WORKTREE_STATE}
            -DS7_WORKING_TREE_FINGERPRINT=${S6_WORKTREE_FINGERPRINT}
            -DS7_SOURCE_MANIFEST_SHA256=${S6_SOURCE_MANIFEST_SHA256}
            -DS7_OUTPUT_ROOT=${S7_CUDA_EVIDENCE_ROOT}
            -DS7_MANIFEST=${S7_CUDA_EVIDENCE_MANIFEST}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCudaV0Evidence.cmake
        DEPENDS s7_cuda_v0_tests s7_cuda_v0_matrix_tests s7_cuda_v0
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCudaV0Evidence.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/src/cuda_v0.cu
            ${CMAKE_CURRENT_SOURCE_DIR}/include/s6/cuda_v0.hpp
            ${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
        VERBATIM)
    add_custom_target(s7_cuda_v0_evidence ALL
        DEPENDS ${S7_CUDA_EVIDENCE_MANIFEST})
    add_dependencies(s7_cuda_v0_evidence s6_verify_source_identity)

    add_executable(s7_cuda_v1_tests tests/cuda_v1_tests.cpp)
    target_link_libraries(s7_cuda_v1_tests PRIVATE
        s7_cuda_v1 s7_cuda_v0 s6_cpu_evidence)
    target_compile_options(s7_cuda_v1_tests PRIVATE /W4 /WX /fp:strict /permissive-)
    add_dependencies(s7_cuda_v1_tests s6_verify_source_identity)

    add_executable(s7_cuda_v1_matrix_tests
        tests/cuda_v1_matrix_evidence_tests.cpp)
    target_link_libraries(s7_cuda_v1_matrix_tests PRIVATE
        s7_cuda_v1 s7_cuda_v0 s6_cpu_evidence)
    target_compile_options(s7_cuda_v1_matrix_tests PRIVATE
        /W4 /WX /fp:strict /permissive-)
    add_dependencies(s7_cuda_v1_matrix_tests s6_verify_source_identity)

    set(S7_CUDA_V1_EVIDENCE_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/evidence/cuda-v1")
    set(S7_CUDA_V1_MATRIX_REPORT_ROOT
        "${S7_CUDA_V1_EVIDENCE_ROOT}/matrix-report")
    set(S7_CUDA_V1_EVIDENCE_MANIFEST
        "${S7_CUDA_V1_EVIDENCE_ROOT}/binary.manifest.txt")
    add_test(NAME s7.cuda_v1.functional
        COMMAND s7_cuda_v1_tests
            ${S7_CUDA_V1_EVIDENCE_ROOT}/functional.txt)
    add_test(NAME s7.cuda_v1.required_matrix
        COMMAND s7_cuda_v1_matrix_tests
            ${S7_CUDA_V1_EVIDENCE_ROOT}/matrix.txt
            ${S7_CUDA_V1_MATRIX_REPORT_ROOT}
            ${S7_CUDA_V1_EVIDENCE_MANIFEST}
            ${S7_CUDA_V1_EVIDENCE_ROOT}/functional.txt)
    set_tests_properties(s7.cuda_v1.functional s7.cuda_v1.required_matrix
        PROPERTIES TIMEOUT 1800)
    set_tests_properties(s7.cuda_v1.required_matrix PROPERTIES
        DEPENDS s7.cuda_v1.functional)

    add_custom_command(
        OUTPUT ${S7_CUDA_V1_EVIDENCE_MANIFEST}
        COMMAND ${CMAKE_COMMAND}
            -DS7_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -DS7_BUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}
            -DS7_GIT=${GIT_EXECUTABLE}
            -DS7_CUOBJDUMP=${S7_CUOBJDUMP}
            -DS7_EXECUTABLE=$<TARGET_FILE:s7_cuda_v1_tests>
            -DS7_MATRIX_EXECUTABLE=$<TARGET_FILE:s7_cuda_v1_matrix_tests>
            -DS7_LIBRARY=$<TARGET_FILE:s7_cuda_v1>
            -DS7_NVCC=${CMAKE_CUDA_COMPILER}
            -DS7_NVCC_VERSION=${CMAKE_CUDA_COMPILER_VERSION}
            -DS7_CUDA_ARCHITECTURES=${CMAKE_CUDA_ARCHITECTURES}
            -DS7_WORKING_TREE_STATE=${S6_WORKTREE_STATE}
            -DS7_WORKING_TREE_FINGERPRINT=${S6_WORKTREE_FINGERPRINT}
            -DS7_SOURCE_MANIFEST_SHA256=${S6_SOURCE_MANIFEST_SHA256}
            -DS7_OUTPUT_ROOT=${S7_CUDA_V1_EVIDENCE_ROOT}
            -DS7_MANIFEST=${S7_CUDA_V1_EVIDENCE_MANIFEST}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCudaV1Evidence.cmake
        DEPENDS s7_cuda_v1_tests s7_cuda_v1_matrix_tests s7_cuda_v1
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCudaV1Evidence.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FinalizeCudaV1Evidence.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/src/cuda_v1.cu
            ${CMAKE_CURRENT_SOURCE_DIR}/include/s6/cuda_v1.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cuda_v1_tests.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cuda_v1_matrix_evidence_tests.cpp
            ${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
        VERBATIM)
    add_custom_target(s7_cuda_v1_evidence ALL
        DEPENDS ${S7_CUDA_V1_EVIDENCE_MANIFEST})
    add_dependencies(s7_cuda_v1_evidence s6_verify_source_identity)

    add_executable(s7_cuda_v2_tests tests/cuda_v2_tests.cpp)
    target_link_libraries(s7_cuda_v2_tests PRIVATE
        s7_cuda_v2 s7_cuda_v1 s7_cuda_v0 s6_cpu_evidence)
    target_compile_options(s7_cuda_v2_tests PRIVATE /W4 /WX /fp:strict /permissive-)
    add_dependencies(s7_cuda_v2_tests s6_verify_source_identity)

    add_executable(s7_cuda_v2_matrix_tests
        tests/cuda_v2_matrix_evidence_tests.cpp)
    target_link_libraries(s7_cuda_v2_matrix_tests PRIVATE
        s7_cuda_v2 s7_cuda_v1 s7_cuda_v0 s6_cpu_evidence)
    target_compile_options(s7_cuda_v2_matrix_tests PRIVATE
        /W4 /WX /fp:strict /permissive-)
    add_dependencies(s7_cuda_v2_matrix_tests s6_verify_source_identity)

    set(S7_CUDA_V2_EVIDENCE_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/evidence/cuda-v2")
    set(S7_CUDA_V2_MATRIX_REPORT_ROOT
        "${S7_CUDA_V2_EVIDENCE_ROOT}/matrix-report")
    set(S7_CUDA_V2_EVIDENCE_MANIFEST
        "${S7_CUDA_V2_EVIDENCE_ROOT}/binary.manifest.txt")
    add_test(NAME s7.cuda_v2.functional
        COMMAND s7_cuda_v2_tests
            ${S7_CUDA_V2_EVIDENCE_ROOT}/functional.txt)
    add_test(NAME s7.cuda_v2.required_matrix
        COMMAND s7_cuda_v2_matrix_tests
            ${S7_CUDA_V2_EVIDENCE_ROOT}/matrix.txt
            ${S7_CUDA_V2_MATRIX_REPORT_ROOT}
            ${S7_CUDA_V2_EVIDENCE_MANIFEST}
            ${S7_CUDA_V2_EVIDENCE_ROOT}/functional.txt)
    set_tests_properties(s7.cuda_v2.functional s7.cuda_v2.required_matrix
        PROPERTIES TIMEOUT 2400)
    set_tests_properties(s7.cuda_v2.required_matrix PROPERTIES
        DEPENDS s7.cuda_v2.functional)

    add_custom_command(
        OUTPUT ${S7_CUDA_V2_EVIDENCE_MANIFEST}
        COMMAND ${CMAKE_COMMAND}
            -DS7_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -DS7_BUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}
            -DS7_GIT=${GIT_EXECUTABLE}
            -DS7_CUOBJDUMP=${S7_CUOBJDUMP}
            -DS7_EXECUTABLE=$<TARGET_FILE:s7_cuda_v2_tests>
            -DS7_MATRIX_EXECUTABLE=$<TARGET_FILE:s7_cuda_v2_matrix_tests>
            -DS7_LIBRARY=$<TARGET_FILE:s7_cuda_v2>
            -DS7_NVCC=${CMAKE_CUDA_COMPILER}
            -DS7_NVCC_VERSION=${CMAKE_CUDA_COMPILER_VERSION}
            -DS7_CUDA_ARCHITECTURES=${CMAKE_CUDA_ARCHITECTURES}
            -DS7_WORKING_TREE_STATE=${S6_WORKTREE_STATE}
            -DS7_WORKING_TREE_FINGERPRINT=${S6_WORKTREE_FINGERPRINT}
            -DS7_SOURCE_MANIFEST_SHA256=${S6_SOURCE_MANIFEST_SHA256}
            -DS7_OUTPUT_ROOT=${S7_CUDA_V2_EVIDENCE_ROOT}
            -DS7_MANIFEST=${S7_CUDA_V2_EVIDENCE_MANIFEST}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCudaV2Evidence.cmake
        DEPENDS s7_cuda_v2_tests s7_cuda_v2_matrix_tests s7_cuda_v2
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCudaV2Evidence.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FinalizeCudaV2Evidence.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/src/cuda_v2.cu
            ${CMAKE_CURRENT_SOURCE_DIR}/include/s6/cuda_v2.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cuda_v2_tests.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cuda_v2_matrix_evidence_tests.cpp
            ${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
        VERBATIM)
    add_custom_target(s7_cuda_v2_evidence ALL
        DEPENDS ${S7_CUDA_V2_EVIDENCE_MANIFEST})
    add_dependencies(s7_cuda_v2_evidence s6_verify_source_identity)

    add_executable(s7_cuda_v3_tests tests/cuda_v3_tests.cpp)
    target_link_libraries(s7_cuda_v3_tests PRIVATE
        s7_cuda_v3 s7_cuda_v2 s7_cuda_v1 s7_cuda_v0 s6_cpu_evidence)
    target_compile_options(s7_cuda_v3_tests PRIVATE /W4 /WX /fp:strict /permissive-)
    add_dependencies(s7_cuda_v3_tests s6_verify_source_identity)

    add_executable(s7_cuda_v3_matrix_tests
        tests/cuda_v3_matrix_evidence_tests.cpp)
    target_link_libraries(s7_cuda_v3_matrix_tests PRIVATE
        s7_cuda_v3 s7_cuda_v2 s7_cuda_v1 s7_cuda_v0 s6_cpu_evidence)
    target_compile_options(s7_cuda_v3_matrix_tests PRIVATE
        /W4 /WX /fp:strict /permissive-)
    add_dependencies(s7_cuda_v3_matrix_tests s6_verify_source_identity)

    set(S7_CUDA_V3_EVIDENCE_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/evidence/cuda-v3")
    set(S7_CUDA_V3_MATRIX_REPORT_ROOT
        "${S7_CUDA_V3_EVIDENCE_ROOT}/matrix-report")
    set(S7_CUDA_V3_EVIDENCE_MANIFEST
        "${S7_CUDA_V3_EVIDENCE_ROOT}/binary.manifest.txt")
    add_test(NAME s7.cuda_v3.functional
        COMMAND s7_cuda_v3_tests
            ${S7_CUDA_V3_EVIDENCE_ROOT}/functional.txt)
    add_test(NAME s7.cuda_v3.required_matrix
        COMMAND s7_cuda_v3_matrix_tests
            ${S7_CUDA_V3_EVIDENCE_ROOT}/matrix.txt
            ${S7_CUDA_V3_MATRIX_REPORT_ROOT}
            ${S7_CUDA_V3_EVIDENCE_MANIFEST}
            ${S7_CUDA_V3_EVIDENCE_ROOT}/functional.txt)
    set_tests_properties(s7.cuda_v3.functional s7.cuda_v3.required_matrix
        PROPERTIES TIMEOUT 2400)
    set_tests_properties(s7.cuda_v3.required_matrix PROPERTIES
        DEPENDS s7.cuda_v3.functional)

    add_custom_command(
        OUTPUT ${S7_CUDA_V3_EVIDENCE_MANIFEST}
        COMMAND ${CMAKE_COMMAND}
            -DS7_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -DS7_BUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}
            -DS7_GIT=${GIT_EXECUTABLE}
            -DS7_CUOBJDUMP=${S7_CUOBJDUMP}
            -DS7_EXECUTABLE=$<TARGET_FILE:s7_cuda_v3_tests>
            -DS7_MATRIX_EXECUTABLE=$<TARGET_FILE:s7_cuda_v3_matrix_tests>
            -DS7_LIBRARY=$<TARGET_FILE:s7_cuda_v3>
            -DS7_NVCC=${CMAKE_CUDA_COMPILER}
            -DS7_NVCC_VERSION=${CMAKE_CUDA_COMPILER_VERSION}
            -DS7_CUDA_ARCHITECTURES=${CMAKE_CUDA_ARCHITECTURES}
            -DS7_WORKING_TREE_STATE=${S6_WORKTREE_STATE}
            -DS7_WORKING_TREE_FINGERPRINT=${S6_WORKTREE_FINGERPRINT}
            -DS7_SOURCE_MANIFEST_SHA256=${S6_SOURCE_MANIFEST_SHA256}
            -DS7_OUTPUT_ROOT=${S7_CUDA_V3_EVIDENCE_ROOT}
            -DS7_MANIFEST=${S7_CUDA_V3_EVIDENCE_MANIFEST}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCudaV3Evidence.cmake
        DEPENDS s7_cuda_v3_tests s7_cuda_v3_matrix_tests s7_cuda_v3
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCudaV3Evidence.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FinalizeCudaV3Evidence.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/src/cuda_v3.cu
            ${CMAKE_CURRENT_SOURCE_DIR}/include/s6/cuda_v3.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cuda_v3_tests.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cuda_v3_matrix_evidence_tests.cpp
            ${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
        VERBATIM)
    add_custom_target(s7_cuda_v3_evidence ALL
        DEPENDS ${S7_CUDA_V3_EVIDENCE_MANIFEST})
    add_dependencies(s7_cuda_v3_evidence s6_verify_source_identity)
    set_tests_properties(s6.cpu.fixtures PROPERTIES
        FIXTURES_SETUP S6_CPU_FIXTURES)
    set_tests_properties(s6.cpu.required_matrix PROPERTIES
        FIXTURES_REQUIRED S6_CPU_FIXTURES)
    set_tests_properties(s6.cpu.fixtures s6.cpu.evidence s6.cpu.source_identity
        s6.cpu.required_matrix
        PROPERTIES TIMEOUT 900
    )
    set(S7_BENCHMARK_INTEGRITY_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/evidence/s7-benchmark-integrity")
    set(S7_BENCHMARK_DRY_RUN_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/evidence/s7-benchmark-dry-run")
    set(S7_EXPERIMENT_CONFIG
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/config/s7-accept-20261103-01.json")
    file(SHA256 "${S7_EXPERIMENT_CONFIG}" S7_EXPERIMENT_CONFIG_SHA256)
    find_program(S7_NODE_EXECUTABLE node REQUIRED)
    find_program(S10_PWSH_EXECUTABLE pwsh REQUIRED)
    set(S8_BENCHMARK_INTEGRITY_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/evidence/s8-benchmark-integrity")
    set(S13_BENCHMARK_INTEGRITY_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/evidence/s13-benchmark-integrity")
    add_executable(s10_sift1m_loader_tests tests/s10_sift1m_loader_tests.cpp)
    target_link_libraries(s10_sift1m_loader_tests PRIVATE s10_sift1m_io)
    target_compile_definitions(s10_sift1m_loader_tests PRIVATE
        S10_TEST_REPOSITORY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(s10_sift1m_loader_tests PRIVATE
        /W4 /WX /fp:strict /permissive-)
    add_dependencies(s10_sift1m_loader_tests s6_verify_source_identity)
    set(S10_BENCHMARK_INTEGRITY_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/evidence/s10-benchmark-integrity")
    add_test(NAME s10.loader.fixtures COMMAND s10_sift1m_loader_tests)
    add_test(NAME s10.benchmark.integrity
        COMMAND s10_sift1m --self-test ${S10_BENCHMARK_INTEGRITY_ROOT})
    add_test(NAME s10.analysis.integrity
        COMMAND ${S7_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/analyze-s10-sift1m.mjs
            --self-test)
    add_test(NAME s10.orchestration.integrity
        COMMAND ${S10_PWSH_EXECUTABLE} -NoProfile -File
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-s10-sift1m.ps1
            -Mode SelfTest)
    set_tests_properties(s10.loader.fixtures s10.benchmark.integrity
        s10.analysis.integrity s10.orchestration.integrity PROPERTIES TIMEOUT 60)
    add_test(NAME s8.benchmark.integrity
        COMMAND s8_benchmark --self-test ${S8_BENCHMARK_INTEGRITY_ROOT})
    add_test(NAME s8.analysis.integrity
        COMMAND ${S7_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/analyze-s8-benchmarks.mjs
            --self-test)
    set_tests_properties(s8.benchmark.integrity s8.analysis.integrity
        PROPERTIES TIMEOUT 60)
    add_test(NAME s13.benchmark.integrity
        COMMAND s13_benchmark --self-test ${S13_BENCHMARK_INTEGRITY_ROOT})
    add_test(NAME s13.analysis.integrity
        COMMAND ${S7_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/analyze-s13-benchmarks.mjs
            --self-test)
    add_test(NAME s13.orchestration.integrity
        COMMAND ${S10_PWSH_EXECUTABLE} -NoProfile -File
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-s13-benchmarks.ps1
            -Mode SelfTest)
    set_tests_properties(s13.benchmark.integrity s13.analysis.integrity
        s13.orchestration.integrity PROPERTIES TIMEOUT 60)
    add_test(NAME s7.benchmark.integrity
        COMMAND s7_benchmark --self-test ${S7_BENCHMARK_INTEGRITY_ROOT})
    add_test(NAME s7.benchmark.dry_run
        COMMAND s7_benchmark --dry-run ${S7_BENCHMARK_DRY_RUN_ROOT}
            --experiment-id S7-ACCEPT-20261103-01
            --experiment-config ${S7_EXPERIMENT_CONFIG}
            --config-sha256 ${S7_EXPERIMENT_CONFIG_SHA256}
            --expected-revision ${S6_BUILD_REVISION}
            --tuning-seed 11820040416388919760
            --final-seed 589684135938649225
            --captured-at 2026-11-03T00:00:00+07:00
            --timezone SE-Asia-Standard-Time
            --approved-design S6ApprovedImplementationDesign-v1.0
            --generator-id S7-DETERMINISTIC-SYNTHETIC-v1
            --cuda-architecture ${CMAKE_CUDA_ARCHITECTURES}
            --build-flags Release-fast-math-disabled
            --stream-policy default-stream-synchronous-one-process)
    add_test(NAME s7.benchmark.acceptance_negative
        COMMAND ${S7_NODE_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/analyze-s7-benchmarks.mjs
            --self-test ${S7_BENCHMARK_INTEGRITY_ROOT}/negative-tests)
    add_test(NAME s7.cuda_v2.frozen_matrix
        COMMAND s7_cuda_v2_matrix_tests
            ${S7_BENCHMARK_INTEGRITY_ROOT}/cuda-v2-frozen-matrix.txt
            ${S7_BENCHMARK_INTEGRITY_ROOT}/cuda-v2-frozen-reports
            ${S7_CUDA_V2_EVIDENCE_MANIFEST}
            ${S7_CUDA_V2_EVIDENCE_ROOT}/functional.txt
            ${S7_BENCHMARK_INTEGRITY_ROOT}/tuning-freeze.csv)
    add_test(NAME s7.cuda_v3.frozen_matrix
        COMMAND s7_cuda_v3_matrix_tests
            ${S7_BENCHMARK_INTEGRITY_ROOT}/cuda-v3-frozen-matrix.txt
            ${S7_BENCHMARK_INTEGRITY_ROOT}/cuda-v3-frozen-reports
            ${S7_CUDA_V3_EVIDENCE_MANIFEST}
            ${S7_CUDA_V3_EVIDENCE_ROOT}/functional.txt
            ${S7_BENCHMARK_INTEGRITY_ROOT}/tuning-freeze.csv)
    set_tests_properties(s7.benchmark.integrity s7.benchmark.acceptance_negative
        PROPERTIES TIMEOUT 60)
    set_tests_properties(s7.benchmark.dry_run PROPERTIES
        DEPENDS s7.benchmark.integrity TIMEOUT 300)
    set_tests_properties(s7.cuda_v2.frozen_matrix PROPERTIES
        DEPENDS "s7.cuda_v2.required_matrix;s7.benchmark.integrity"
        FIXTURES_REQUIRED S7_CUDA_V2_FUNCTIONAL TIMEOUT 2400)
    set_tests_properties(s7.cuda_v3.frozen_matrix PROPERTIES
        DEPENDS "s7.cuda_v3.required_matrix;s7.benchmark.integrity"
        FIXTURES_REQUIRED S7_CUDA_V3_FUNCTIONAL TIMEOUT 2400)
    set_tests_properties(s7.cuda_v2.functional PROPERTIES
        FIXTURES_SETUP S7_CUDA_V2_FUNCTIONAL)
    set_tests_properties(s7.cuda_v3.functional PROPERTIES
        FIXTURES_SETUP S7_CUDA_V3_FUNCTIONAL)
endif()
