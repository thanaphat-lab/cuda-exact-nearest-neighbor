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
    "${S7_OUTPUT_ROOT}/cuda-v1.sass.txt"
    "${S7_SOURCE_ROOT}/CMakeLists.txt"
    "${S7_SOURCE_ROOT}/cmake/GenerateCudaV1Evidence.cmake"
    "${S7_SOURCE_ROOT}/cmake/FinalizeCudaV1Evidence.cmake"
    "${S7_SOURCE_ROOT}/include/s6/cuda_v1.hpp"
    "${S7_SOURCE_ROOT}/src/cuda_v1.cu"
    "${S7_SOURCE_ROOT}/tests/cuda_v1_tests.cpp"
    "${S7_SOURCE_ROOT}/tests/cuda_v1_matrix_evidence_tests.cpp")
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
file(STRINGS "${S7_BUILD_ROOT}/generated/s6/source_identity.txt"
    identity_state_line REGEX "^working_tree_state=")
string(REPLACE "working_tree_state=" "" configured_state
    "${identity_state_line}")
if(working_tree_status STREQUAL "")
    set(current_state "clean")
    set(evidence_class "clean-candidate")
else()
    set(current_state "dirty-authorized-local")
    set(evidence_class "local-uncommitted-not-acceptance")
endif()
if(NOT working_tree_fingerprint STREQUAL configured_fingerprint OR
   NOT current_state STREQUAL configured_state)
    message(FATAL_ERROR
        "Working-tree identity changed after the evidence-producing build")
endif()

function(require_field input_path field_name output_name)
    file(STRINGS "${input_path}" matching_lines REGEX "^${field_name}=")
    list(LENGTH matching_lines matching_count)
    if(NOT matching_count EQUAL 1)
        message(FATAL_ERROR
            "Expected exactly one ${field_name} field in ${input_path}")
    endif()
    string(REPLACE "${field_name}=" "" field_value "${matching_lines}")
    set(${output_name} "${field_value}" PARENT_SCOPE)
endfunction()

function(require_equal actual expected description)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "CUDA V1 evidence correlation failed for ${description}: '${actual}' != '${expected}'")
    endif()
endfunction()

set(identity_path "${S7_BUILD_ROOT}/generated/s6/source_identity.txt")
set(binary_path "${S7_OUTPUT_ROOT}/binary.manifest.txt")
set(functional_path "${S7_OUTPUT_ROOT}/functional.txt")
set(matrix_path "${S7_OUTPUT_ROOT}/matrix.txt")
require_field("${identity_path}" revision configured_revision)
require_field("${identity_path}" source_manifest_sha256 configured_source_manifest)
require_equal("${configured_revision}" "${revision}" "configured revision")

foreach(evidence_path IN ITEMS "${binary_path}" "${functional_path}" "${matrix_path}")
    require_field("${evidence_path}" revision evidence_revision)
    require_field("${evidence_path}" working_tree_state evidence_state)
    require_field("${evidence_path}" working_tree_fingerprint evidence_fingerprint)
    require_field("${evidence_path}" source_manifest_sha256 evidence_source_manifest)
    require_field("${evidence_path}" evidence_class observed_evidence_class)
    require_equal("${evidence_revision}" "${revision}" "runtime revision")
    require_equal("${evidence_state}" "${current_state}" "runtime working-tree state")
    require_equal("${evidence_fingerprint}" "${working_tree_fingerprint}"
        "runtime working-tree fingerprint")
    require_equal("${evidence_source_manifest}" "${configured_source_manifest}"
        "runtime source manifest")
    require_equal("${observed_evidence_class}" "${evidence_class}"
        "runtime evidence class")
endforeach()

foreach(status_field IN ITEMS
        compile_policy_status distance_kernel_symbol_status
        distance_kernel_instruction_status distance_kernel_dependency_status
        distance_kernel_default_rn_no_ftz_status binary_graph_structure_status)
    require_field("${binary_path}" "${status_field}" binary_status)
    require_equal("${binary_status}" "passed" "binary/SASS ${status_field}")
endforeach()

set(binary_hash_pairs
    "functional_executable_sha256|${S7_FUNCTIONAL_EXECUTABLE}"
    "matrix_executable_sha256|${S7_MATRIX_EXECUTABLE}"
    "library_sha256|${S7_LIBRARY}"
    "sass_sha256|${S7_OUTPUT_ROOT}/cuda-v1.sass.txt"
    "cuda_source_sha256|${S7_SOURCE_ROOT}/src/cuda_v1.cu"
    "cuda_header_sha256|${S7_SOURCE_ROOT}/include/s6/cuda_v1.hpp")
foreach(hash_pair IN LISTS binary_hash_pairs)
    string(REPLACE "|" ";" hash_parts "${hash_pair}")
    list(GET hash_parts 0 hash_field)
    list(GET hash_parts 1 hash_path)
    require_field("${binary_path}" "${hash_field}" recorded_hash)
    file(SHA256 "${hash_path}" observed_hash)
    require_equal("${recorded_hash}" "${observed_hash}" "${hash_field}")
endforeach()

require_field("${functional_path}" overall functional_overall)
require_equal("${functional_overall}" "passed" "functional overall status")
file(READ "${functional_path}" functional_text)
foreach(functional_token IN ITEMS
        "reduction=passed" "exact_dyadic_oracle=passed"
        "interval_boundary_fixture=passed" "inclusive_boundary=passed"
        "boundary_uncertain_retention=passed" "valid_outside_domain=passed"
        "transpose_mapping=passed" "bit_preservation=passed"
        "cpu_v1_comparison=passed" "v0_v1_comparison=passed")
    string(FIND "${functional_text}" "${functional_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR
            "Required CUDA functional evidence token missing: ${functional_token}")
    endif()
endforeach()

file(STRINGS "${matrix_path}" matrix_case_lines REGEX "^case=N")
list(LENGTH matrix_case_lines matrix_case_count)
if(NOT matrix_case_count EQUAL 12)
    message(FATAL_ERROR "CUDA V1 matrix evidence does not contain 12 cases")
endif()
foreach(case_line IN LISTS matrix_case_lines)
    foreach(case_token IN ITEMS
            "ud001=passed" "ud002=passed" "candidate_retention=passed"
            "layout_transform=passed" "cpu_v1=passed" "v0_v1=passed"
            "graph_environment_status=passed" "correlation=passed"
            "nn_verdict=accepted-unambiguous")
        string(FIND "${case_line}" "${case_token}" case_token_position)
        if(case_token_position EQUAL -1)
            message(FATAL_ERROR
                "CUDA V1 matrix case is missing ${case_token}: ${case_line}")
        endif()
    endforeach()
endforeach()
file(STRINGS "${matrix_path}" matrix_acceptance_line
    REGEX "^matrix_nn_acceptance=passed accepted_unambiguous=12 accepted_ambiguous=0 failed=0 blocked=0$")
if(matrix_acceptance_line STREQUAL "")
    message(FATAL_ERROR "CUDA V1 matrix acceptance summary did not pass")
endif()

set(artifact_lines "")
set(matrix_candidate_total 0)
set(matrix_candidate_bytes 0)
foreach(candidate_count IN ITEMS 1024 16384 262144 1048576)
    foreach(dimension IN ITEMS 8 32 128)
        set(case_id "cuda-v1-N${candidate_count}-D${dimension}")
        set(metadata_path
            "${S7_OUTPUT_ROOT}/matrix-report/${case_id}.metadata.txt")
        set(candidate_path
            "${S7_OUTPUT_ROOT}/matrix-report/${case_id}.candidates.bin")
        if(NOT EXISTS "${metadata_path}" OR NOT EXISTS "${candidate_path}")
            message(FATAL_ERROR "Required CUDA V1 case report missing: ${case_id}")
        endif()
        require_field("${metadata_path}" schema case_schema)
        require_equal("${case_schema}" "S7-CUDA-V1-CASE-EVIDENCE-v1.0"
            "case schema")
        require_field("${metadata_path}" implementation_variant case_variant)
        require_equal("${case_variant}" "cuda-v1" "${case_id} variant")
        require_field("${metadata_path}" workload_kind case_workload)
        require_equal("${case_workload}" "single-query-primary"
            "${case_id} workload")
        foreach(case_identity_field IN ITEMS
                revision working_tree_state working_tree_fingerprint
                source_manifest_sha256)
            require_field("${metadata_path}" "${case_identity_field}" case_identity)
            if(case_identity_field STREQUAL "revision")
                set(expected_identity "${revision}")
            elseif(case_identity_field STREQUAL "working_tree_state")
                set(expected_identity "${current_state}")
            elseif(case_identity_field STREQUAL "working_tree_fingerprint")
                set(expected_identity "${working_tree_fingerprint}")
            else()
                set(expected_identity "${configured_source_manifest}")
            endif()
            require_equal("${case_identity}" "${expected_identity}"
                "${case_id} ${case_identity_field}")
        endforeach()
        foreach(case_status_field IN ITEMS
                mapping_status overwrite_status layout_transform_status
                cpu_v1_comparison_status v0_v1_comparison_status
                candidate_coverage_status
                production_path_status graph_status arithmetic_environment_status
                float32_lexicographic_status ud001_status ud002_status
                fixture_status oracle_status tie_status reduction_status)
            require_field("${metadata_path}" "${case_status_field}" case_status)
            require_equal("${case_status}" "passed"
                "${case_id} ${case_status_field}")
        endforeach()
        require_field("${metadata_path}" nn_verdict case_verdict)
        require_equal("${case_verdict}" "accepted-unambiguous"
            "${case_id} verdict")
        require_field("${metadata_path}" exact_membership_resolved exact_resolved)
        require_equal("${exact_resolved}" "true" "${case_id} exact membership")
        require_field("${metadata_path}" exact_ambiguity exact_ambiguity)
        require_equal("${exact_ambiguity}" "false" "${case_id} exact ambiguity")
        require_field("${metadata_path}" boundary_uncertain_indices boundary_indices)
        require_equal("${boundary_indices}" "" "${case_id} boundary uncertainty")
        math(EXPR expected_index "${candidate_count} / 2")
        require_field("${metadata_path}" exact_contenders exact_contenders)
        require_equal("${exact_contenders}" "${expected_index}"
            "${case_id} exact contenders")
        require_field("${metadata_path}" conservative_contenders conservative_contenders)
        require_equal("${conservative_contenders}" "${expected_index}"
            "${case_id} conservative contenders")
        require_field("${metadata_path}" decision_trace decision_trace)
        foreach(trace_token IN ITEMS "1:" "2:" "3:" "4:" "5:" "6:" "7:" "8:" "9:")
            string(FIND "${decision_trace}" "${trace_token}" trace_position)
            if(trace_position EQUAL -1)
                message(FATAL_ERROR
                    "${case_id} ordered decision trace missing ${trace_token}")
            endif()
        endforeach()
        require_field("${metadata_path}" candidate_count recorded_candidate_count)
        require_equal("${recorded_candidate_count}" "${candidate_count}"
            "${case_id} candidate count")
        require_field("${metadata_path}" candidate_bytes recorded_candidate_bytes)
        file(SIZE "${candidate_path}" observed_candidate_bytes)
        require_equal("${recorded_candidate_bytes}" "${observed_candidate_bytes}"
            "${case_id} candidate report size")
        math(EXPR matrix_candidate_total
            "${matrix_candidate_total} + ${candidate_count}")
        math(EXPR matrix_candidate_bytes
            "${matrix_candidate_bytes} + ${observed_candidate_bytes}")
        file(SHA256 "${metadata_path}" metadata_hash)
        file(SHA256 "${candidate_path}" candidate_hash)
        string(APPEND artifact_lines
            "artifact=${metadata_path}|${metadata_hash}\n"
            "artifact=${candidate_path}|${candidate_hash}\n")
    endforeach()
endforeach()

file(READ "${S7_OUTPUT_ROOT}/sanitizer-memcheck.log" memcheck_text)
file(READ "${S7_OUTPUT_ROOT}/sanitizer-initcheck.log" initcheck_text)
file(READ "${S7_OUTPUT_ROOT}/sanitizer-racecheck.log" racecheck_text)
if(NOT memcheck_text MATCHES "ERROR SUMMARY: 0 errors" OR
   NOT initcheck_text MATCHES "ERROR SUMMARY: 0 errors" OR
   NOT racecheck_text MATCHES "RACECHECK SUMMARY: 0 hazards displayed \\(0 errors, 0 warnings\\)")
    message(FATAL_ERROR "CUDA V1 compute-sanitizer evidence did not pass")
endif()
foreach(sanitizer_kind IN ITEMS memcheck initcheck racecheck)
    set(sanitizer_functional_path
        "${S7_OUTPUT_ROOT}/sanitizer-${sanitizer_kind}-functional.txt")
    require_field("${sanitizer_functional_path}" overall sanitizer_functional_status)
    require_equal("${sanitizer_functional_status}" "passed"
        "${sanitizer_kind} functional status")
    require_field("${sanitizer_functional_path}" revision sanitizer_revision)
    require_equal("${sanitizer_revision}" "${revision}"
        "${sanitizer_kind} revision")
    require_field("${sanitizer_functional_path}" working_tree_fingerprint
        sanitizer_fingerprint)
    require_equal("${sanitizer_fingerprint}" "${working_tree_fingerprint}"
        "${sanitizer_kind} working-tree fingerprint")
endforeach()

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
    " -P \"${S7_SOURCE_ROOT}/cmake/FinalizeCudaV1Evidence.cmake\"")

if(current_state STREQUAL "clean")
    set(overall_acceptance "passed")
    set(blocked_gate "none")
else()
    set(overall_acceptance "blocked/unverified")
    set(blocked_gate "clean-revision-evidence-required")
endif()

file(WRITE "${S7_MANIFEST}"
"schema=S7-CUDA-V1-CONSOLIDATED-EVIDENCE-v1.0\n"
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
"matrix_case_count=${matrix_case_count}\n"
"matrix_candidate_count=${matrix_candidate_total}\n"
"matrix_candidate_bytes=${matrix_candidate_bytes}\n"
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
"command.finalize=${finalize_command}\n"
"${artifact_lines}")


