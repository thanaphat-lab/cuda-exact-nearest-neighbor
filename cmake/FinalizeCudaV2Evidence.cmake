cmake_minimum_required(VERSION 3.21)

foreach(variable IN ITEMS
        S7_SOURCE_ROOT S7_BUILD_ROOT S7_GIT S7_OUTPUT_ROOT S7_MANIFEST
        S7_FUNCTIONAL_EXECUTABLE S7_MATRIX_EXECUTABLE S7_LIBRARY
        S7_CONFIGURE_COMMAND S7_BUILD_COMMAND S7_CTEST_COMMAND
        S7_MEMCHECK_COMMAND S7_INITCHECK_COMMAND S7_RACECHECK_COMMAND)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "Missing CUDA V2 consolidated-evidence input: ${variable}")
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
    "${S7_OUTPUT_ROOT}/cuda-v2.sass.txt"
    "${S7_SOURCE_ROOT}/CMakeLists.txt"
    "${S7_SOURCE_ROOT}/cmake/GenerateCudaV2Evidence.cmake"
    "${S7_SOURCE_ROOT}/cmake/FinalizeCudaV2Evidence.cmake"
    "${S7_SOURCE_ROOT}/include/s6/cuda_v2.hpp"
    "${S7_SOURCE_ROOT}/src/cuda_v2.cu"
    "${S7_SOURCE_ROOT}/tests/cuda_v2_tests.cpp"
    "${S7_SOURCE_ROOT}/tests/cuda_v2_matrix_evidence_tests.cpp")
foreach(path IN LISTS required_paths)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Required CUDA V2 consolidated evidence missing: ${path}")
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
if(working_tree_status STREQUAL "")
    set(current_state "clean")
    set(evidence_class "clean-candidate")
else()
    set(current_state "dirty-authorized-local")
    set(evidence_class "local-uncommitted-not-acceptance")
endif()

function(require_field input_path field_name output_name)
    file(STRINGS "${input_path}" lines REGEX "^${field_name}=")
    list(LENGTH lines count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Expected one ${field_name} in ${input_path}")
    endif()
    string(REPLACE "${field_name}=" "" value "${lines}")
    set(${output_name} "${value}" PARENT_SCOPE)
endfunction()

function(require_equal actual expected description)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "CUDA V2 evidence correlation failed for ${description}: '${actual}' != '${expected}'")
    endif()
endfunction()

set(identity_path "${S7_BUILD_ROOT}/generated/s6/source_identity.txt")
set(binary_path "${S7_OUTPUT_ROOT}/binary.manifest.txt")
set(functional_path "${S7_OUTPUT_ROOT}/functional.txt")
set(matrix_path "${S7_OUTPUT_ROOT}/matrix.txt")
require_field("${identity_path}" revision configured_revision)
require_field("${identity_path}" working_tree_state configured_state)
require_field("${identity_path}" working_tree_fingerprint configured_fingerprint)
require_field("${identity_path}" source_manifest_sha256 configured_manifest)
require_equal("${configured_revision}" "${revision}" "configured revision")
require_equal("${configured_state}" "${current_state}" "configured tree state")
require_equal("${configured_fingerprint}" "${working_tree_fingerprint}"
    "configured tree fingerprint")

foreach(evidence_path IN ITEMS "${binary_path}" "${functional_path}" "${matrix_path}")
    require_field("${evidence_path}" revision observed_revision)
    require_field("${evidence_path}" working_tree_state observed_state)
    require_field("${evidence_path}" working_tree_fingerprint observed_fingerprint)
    require_field("${evidence_path}" source_manifest_sha256 observed_manifest)
    require_equal("${observed_revision}" "${revision}" "runtime revision")
    require_equal("${observed_state}" "${current_state}" "runtime tree state")
    require_equal("${observed_fingerprint}" "${working_tree_fingerprint}"
        "runtime tree fingerprint")
    require_equal("${observed_manifest}" "${configured_manifest}"
        "runtime source manifest")
endforeach()

foreach(field IN ITEMS
        compile_policy_status distance_kernel_symbol_status
        record_kernel_symbol_status barrier_instruction_status
        distance_kernel_instruction_status distance_kernel_dependency_status
        distance_kernel_default_rn_no_ftz_status serial_reducer_absence_status
        binary_graph_structure_status)
    require_field("${binary_path}" "${field}" status)
    require_equal("${status}" "passed" "binary/SASS ${field}")
endforeach()

set(hash_pairs
    "functional_executable_sha256|${S7_FUNCTIONAL_EXECUTABLE}"
    "matrix_executable_sha256|${S7_MATRIX_EXECUTABLE}"
    "library_sha256|${S7_LIBRARY}"
    "sass_sha256|${S7_OUTPUT_ROOT}/cuda-v2.sass.txt"
    "cuda_source_sha256|${S7_SOURCE_ROOT}/src/cuda_v2.cu"
    "cuda_header_sha256|${S7_SOURCE_ROOT}/include/s6/cuda_v2.hpp")
foreach(pair IN LISTS hash_pairs)
    string(REPLACE "|" ";" parts "${pair}")
    list(GET parts 0 field)
    list(GET parts 1 path)
    require_field("${binary_path}" "${field}" recorded_hash)
    file(SHA256 "${path}" actual_hash)
    require_equal("${recorded_hash}" "${actual_hash}" "${field}")
endforeach()

require_field("${functional_path}" overall functional_status)
require_equal("${functional_status}" "passed" "functional status")
file(READ "${functional_path}" functional_text)
foreach(token IN ITEMS
        "abi=passed" "block_candidates=passed" "hierarchy_tail=passed"
        "identity_tails=passed" "reduction=passed"
        "hierarchy_associativity=passed" "full_width_index=passed"
        "exact_dyadic_oracle=passed" "interval_boundary_fixture=passed"
        "boundary_uncertain_retention=passed" "valid_outside_domain=passed"
        "cpu_v2_comparison=passed" "v0_v2_comparison=passed"
        "v1_v2_comparison=passed" "nondefault_stream=passed")
    string(FIND "${functional_text}" "${token}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Required CUDA V2 functional token missing: ${token}")
    endif()
endforeach()

file(STRINGS "${matrix_path}" case_lines REGEX "^case=N")
list(LENGTH case_lines case_count)
if(NOT case_count EQUAL 12)
    message(FATAL_ERROR "CUDA V2 matrix evidence does not contain 12 cases")
endif()
foreach(line IN LISTS case_lines)
    foreach(token IN ITEMS
            "block_size=256" "cpu_v2=passed" "v0_v2=passed" "v1_v2=passed"
            "ud001=passed" "ud002=passed" "candidate_retention=passed"
            "graph_environment_status=passed" "correlation=passed"
            "nn_verdict=accepted-unambiguous")
        string(FIND "${line}" "${token}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "CUDA V2 matrix case is missing ${token}: ${line}")
        endif()
    endforeach()
endforeach()
file(STRINGS "${matrix_path}" acceptance_line
    REGEX "^matrix_nn_acceptance=passed accepted_unambiguous=12 accepted_ambiguous=0 failed=0 blocked=0$")
if(acceptance_line STREQUAL "")
    message(FATAL_ERROR "CUDA V2 matrix acceptance summary did not pass")
endif()

set(candidate_total 0)
set(candidate_bytes 0)
set(artifact_lines "")
foreach(n IN ITEMS 1024 16384 262144 1048576)
    foreach(d IN ITEMS 8 32 128)
        set(case_id "cuda-v2-N${n}-D${d}")
        set(metadata "${S7_OUTPUT_ROOT}/matrix-report/${case_id}.metadata.txt")
        set(candidates "${S7_OUTPUT_ROOT}/matrix-report/${case_id}.candidates.bin")
        if(NOT EXISTS "${metadata}" OR NOT EXISTS "${candidates}")
            message(FATAL_ERROR "CUDA V2 case report missing: ${case_id}")
        endif()
        require_field("${metadata}" schema schema)
        require_equal("${schema}" "S7-CUDA-V2-CASE-EVIDENCE-v1.0" "case schema")
        require_field("${metadata}" implementation_variant variant)
        require_equal("${variant}" "cuda-v2" "case variant")
        require_field("${metadata}" block_size block_size)
        require_equal("${block_size}" "256" "case block size")
        foreach(field IN ITEMS
                mapping_status overwrite_status layout_transform_status
                cpu_v2_comparison_status v0_v2_comparison_status
                v1_v2_comparison_status candidate_coverage_status
                production_path_status graph_status arithmetic_environment_status
                float32_lexicographic_status ud001_status ud002_status
                fixture_status oracle_status tie_status reduction_status)
            require_field("${metadata}" "${field}" status)
            require_equal("${status}" "passed" "${case_id} ${field}")
        endforeach()
        require_field("${metadata}" nn_verdict verdict)
        require_equal("${verdict}" "accepted-unambiguous" "${case_id} verdict")
        require_field("${metadata}" candidate_count recorded_count)
        require_equal("${recorded_count}" "${n}" "${case_id} candidate count")
        require_field("${metadata}" candidate_bytes recorded_bytes)
        file(SIZE "${candidates}" actual_bytes)
        require_equal("${recorded_bytes}" "${actual_bytes}" "${case_id} bytes")
        require_field("${metadata}" decision_trace decision_trace)
        foreach(trace_token IN ITEMS "1:" "2:" "3:" "4:" "5:" "6:" "7:" "8:" "9:")
            string(FIND "${decision_trace}" "${trace_token}" trace_position)
            if(trace_position EQUAL -1)
                message(FATAL_ERROR
                    "${case_id} ordered decision trace missing ${trace_token}")
            endif()
        endforeach()
        math(EXPR candidate_total "${candidate_total} + ${n}")
        math(EXPR candidate_bytes "${candidate_bytes} + ${actual_bytes}")
        file(SHA256 "${metadata}" metadata_hash)
        file(SHA256 "${candidates}" candidate_hash)
        string(APPEND artifact_lines
            "artifact=${metadata}|${metadata_hash}\n"
            "artifact=${candidates}|${candidate_hash}\n")
    endforeach()
endforeach()

file(READ "${S7_OUTPUT_ROOT}/sanitizer-memcheck.log" memcheck)
file(READ "${S7_OUTPUT_ROOT}/sanitizer-initcheck.log" initcheck)
file(READ "${S7_OUTPUT_ROOT}/sanitizer-racecheck.log" racecheck)
if(NOT memcheck MATCHES "ERROR SUMMARY: 0 errors" OR
   NOT initcheck MATCHES "ERROR SUMMARY: 0 errors" OR
   NOT racecheck MATCHES "RACECHECK SUMMARY: 0 hazards displayed \\(0 errors, 0 warnings\\)")
    message(FATAL_ERROR "CUDA V2 Compute Sanitizer evidence did not pass")
endif()
foreach(kind IN ITEMS memcheck initcheck racecheck)
    set(path "${S7_OUTPUT_ROOT}/sanitizer-${kind}-functional.txt")
    require_field("${path}" overall status)
    require_equal("${status}" "passed" "${kind} functional status")
    require_field("${path}" working_tree_fingerprint fingerprint)
    require_equal("${fingerprint}" "${working_tree_fingerprint}"
        "${kind} tree fingerprint")
endforeach()

foreach(path IN LISTS required_paths)
    file(SHA256 "${path}" hash)
    string(APPEND artifact_lines "artifact=${path}|${hash}\n")
endforeach()
string(TIMESTAMP generated_at_utc "%Y-%m-%dT%H:%M:%SZ" UTC)
if(current_state STREQUAL "clean")
    set(overall_acceptance "passed")
    set(blocked_gate "none")
else()
    set(overall_acceptance "blocked/unverified")
    set(blocked_gate "clean-revision-evidence-required")
endif()

file(WRITE "${S7_MANIFEST}"
"schema=S7-CUDA-V2-CONSOLIDATED-EVIDENCE-v1.0\n"
"evidence_class=${evidence_class}\n"
"overall_acceptance=${overall_acceptance}\n"
"local_correctness_status=passed\n"
"blocked_gate=${blocked_gate}\n"
"source_binary_sass_runtime_correlation=passed\n"
"gpu_sanitizer_evidence=passed\n"
"ac005_argmin_validation=passed\n"
"ac006_distance_error=passed\n"
"ac007_ambiguity_classification=passed\n"
"ac008_ordered_verdict=passed\n"
"ac011_required_coverage=passed\n"
"ec015_interval_boundaries=passed\n"
"ec016_valid_outside_domain=passed\n"
"matrix_case_count=${case_count}\n"
"matrix_candidate_count=${candidate_total}\n"
"matrix_candidate_bytes=${candidate_bytes}\n"
"generated_at_utc=${generated_at_utc}\n"
"revision=${revision}\n"
"working_tree_state=${current_state}\n"
"working_tree_fingerprint=${working_tree_fingerprint}\n"
"command.configure=${S7_CONFIGURE_COMMAND}\n"
"command.build=${S7_BUILD_COMMAND}\n"
"command.ctest=${S7_CTEST_COMMAND}\n"
"command.memcheck=${S7_MEMCHECK_COMMAND}\n"
"command.initcheck=${S7_INITCHECK_COMMAND}\n"
"command.racecheck=${S7_RACECHECK_COMMAND}\n"
"${artifact_lines}")
