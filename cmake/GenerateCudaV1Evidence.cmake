file(MAKE_DIRECTORY "${S7_OUTPUT_ROOT}")

set(sass_path "${S7_OUTPUT_ROOT}/cuda-v1.sass.txt")
execute_process(
    COMMAND "${S7_CUOBJDUMP}" --dump-sass "${S7_EXECUTABLE}"
    OUTPUT_FILE "${sass_path}"
    ERROR_VARIABLE sass_error
    RESULT_VARIABLE sass_result)
if(NOT sass_result EQUAL 0)
    message(FATAL_ERROR "cuobjdump failed: ${sass_error}")
endif()

set(required_paths
    "${S7_EXECUTABLE}"
    "${S7_MATRIX_EXECUTABLE}"
    "${S7_LIBRARY}"
    "${S7_NVCC}"
    "${S7_BUILD_ROOT}/compile_commands.json"
    "${S7_BUILD_ROOT}/generated/s6/source_identity.txt"
    "${S7_SOURCE_ROOT}/CMakeLists.txt"
    "${S7_SOURCE_ROOT}/include/s6/cuda_v1.hpp"
    "${S7_SOURCE_ROOT}/src/cuda_v1.cu"
    "${S7_SOURCE_ROOT}/tests/cuda_v1_tests.cpp"
    "${S7_SOURCE_ROOT}/tests/cuda_v1_matrix_evidence_tests.cpp"
    "${S7_SOURCE_ROOT}/cmake/GenerateCudaV1Evidence.cmake"
    "${S7_SOURCE_ROOT}/cmake/FinalizeCudaV1Evidence.cmake"
    "${S7_SOURCE_ROOT}/docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md"
    "${sass_path}")
foreach(path IN LISTS required_paths)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Required CUDA V1 evidence input missing: ${path}")
    endif()
endforeach()

file(READ "${S7_BUILD_ROOT}/compile_commands.json" compile_commands_text)
string(JSON compile_command_count LENGTH "${compile_commands_text}")
math(EXPR compile_command_last "${compile_command_count} - 1")
set(cuda_v1_compile_command "")
foreach(compile_command_index RANGE 0 ${compile_command_last})
    string(JSON compile_command_file GET "${compile_commands_text}"
        ${compile_command_index} file)
    file(TO_CMAKE_PATH "${compile_command_file}" compile_command_file_normalized)
    if(compile_command_file_normalized MATCHES "/src/cuda_v1[.]cu$")
        if(NOT cuda_v1_compile_command STREQUAL "")
            message(FATAL_ERROR
                "Multiple CUDA V1 compile commands were found")
        endif()
        string(JSON cuda_v1_compile_command GET "${compile_commands_text}"
            ${compile_command_index} command)
    endif()
endforeach()
if(cuda_v1_compile_command STREQUAL "")
    message(FATAL_ERROR "CUDA V1 compile command was not found")
endif()
set(compile_policy_status "passed")
foreach(required_option IN ITEMS
        "--ftz=false" "--fmad=true" "--prec-div=true" "--prec-sqrt=true")
    string(FIND "${cuda_v1_compile_command}" "${required_option}"
        required_option_position)
    if(required_option_position EQUAL -1)
        set(compile_policy_status "failed")
    endif()
endforeach()
string(FIND "${cuda_v1_compile_command}" "--use_fast_math" fast_math_position)
if(NOT fast_math_position EQUAL -1)
    set(compile_policy_status "failed")
endif()
if(compile_policy_status STREQUAL "failed")
    message(FATAL_ERROR
        "CUDA V1 compile commands do not preserve the required arithmetic policy")
endif()

execute_process(
    COMMAND "${S7_GIT}" rev-parse HEAD
    WORKING_DIRECTORY "${S7_SOURCE_ROOT}"
    OUTPUT_VARIABLE revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${S7_GIT}" status --porcelain=v1 --untracked-files=all
    WORKING_DIRECTORY "${S7_SOURCE_ROOT}"
    OUTPUT_VARIABLE current_status
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
string(SHA256 current_fingerprint "${current_status}")
if(NOT current_fingerprint STREQUAL S7_WORKING_TREE_FINGERPRINT)
    message(FATAL_ERROR "CUDA V1 source identity changed after configuration")
endif()

file(READ "${sass_path}" sass_text)
set(distance_symbol_marker "cuda_v1_distance_kernel")
string(FIND "${sass_text}" "${distance_symbol_marker}" distance_symbol_position)
string(FIND "${sass_text}" "cuda_v1_serial_reduce_kernel" reduction_symbol_position)
if(distance_symbol_position EQUAL -1 OR reduction_symbol_position EQUAL -1)
    set(kernel_symbol_status "failed")
else()
    set(kernel_symbol_status "passed")
endif()

set(distance_instruction_status "failed")
set(distance_dependency_status "failed")
set(distance_default_rounding_status "failed")
set(distance_fadd_count 0)
set(distance_ffma_count 0)
set(distance_fmul_count 0)
set(distance_ftz_modifier_count 0)
if(NOT distance_symbol_position EQUAL -1)
    string(SUBSTRING "${sass_text}" ${distance_symbol_position} -1 distance_sass_tail)
    string(FIND "${distance_sass_tail}" "\n\t\tFunction :" next_function_position)
    if(next_function_position EQUAL -1)
        set(distance_sass "${distance_sass_tail}")
    else()
        string(SUBSTRING "${distance_sass_tail}" 0 ${next_function_position}
            distance_sass)
    endif()

    string(REGEX MATCHALL "[ \t]FADD[.A-Z0-9]*[ \t]" distance_fadd_matches
        "${distance_sass}")
    string(REGEX MATCHALL "[ \t]FFMA[.A-Z0-9]*[ \t]" distance_ffma_matches
        "${distance_sass}")
    string(REGEX MATCHALL "[ \t]FMUL[.A-Z0-9]*[ \t]" distance_fmul_matches
        "${distance_sass}")
    string(REGEX MATCHALL "[.](FTZ|RZ|RM|RP)([.]|[ \t])"
        distance_ftz_or_non_rn_matches "${distance_sass}")
    list(LENGTH distance_fadd_matches distance_fadd_count)
    list(LENGTH distance_ffma_matches distance_ffma_count)
    list(LENGTH distance_fmul_matches distance_fmul_count)
    list(LENGTH distance_ftz_or_non_rn_matches distance_ftz_modifier_count)

    if(distance_fadd_count EQUAL 1 AND distance_ffma_count EQUAL 1 AND
       distance_fmul_count EQUAL 0)
        set(distance_instruction_status "passed")
    endif()
    if(distance_ftz_modifier_count EQUAL 0)
        set(distance_default_rounding_status "passed")
    endif()

    string(REGEX MATCH
        "FADD[.A-Z0-9]*[ \t]+(R[0-9]+),[ \t]+-?(R[0-9]+),[ \t]+(R[0-9]+)"
        distance_fadd_line "${distance_sass}")
    set(difference_register "${CMAKE_MATCH_1}")
    string(REGEX MATCH
        "FFMA[.A-Z0-9]*[ \t]+(R[0-9]+),[ \t]+(R[0-9]+),[ \t]+(R[0-9]+),[ \t]+(R[0-9]+)"
        distance_ffma_line "${distance_sass}")
    set(accumulator_output_register "${CMAKE_MATCH_1}")
    set(ffma_left_register "${CMAKE_MATCH_2}")
    set(ffma_right_register "${CMAKE_MATCH_3}")
    set(accumulator_input_register "${CMAKE_MATCH_4}")
    if(NOT difference_register STREQUAL "" AND
       difference_register STREQUAL ffma_left_register AND
       difference_register STREQUAL ffma_right_register AND
       NOT accumulator_output_register STREQUAL "" AND
       accumulator_output_register STREQUAL accumulator_input_register)
        set(accumulator_zero_pattern
            "IMAD[.]MOV[.]U32[ \t]+${accumulator_output_register},[ \t]+RZ,[ \t]+RZ,[ \t]+RZ")
        if(distance_sass MATCHES "${accumulator_zero_pattern}")
            set(distance_dependency_status "passed")
        endif()
    endif()
endif()

if(distance_instruction_status STREQUAL "failed" OR
   distance_dependency_status STREQUAL "failed" OR
   distance_default_rounding_status STREQUAL "failed")
    message(FATAL_ERROR
        "CUDA V1 distance-kernel SASS does not preserve the required sequential subtraction/FMA graph")
endif()

foreach(path IN LISTS required_paths)
    file(SHA256 "${path}" hash)
    string(APPEND artifact_lines "artifact=${path}|${hash}\n")
endforeach()

if(S7_WORKING_TREE_STATE STREQUAL "clean")
    set(evidence_class "clean-candidate")
else()
    set(evidence_class "local-uncommitted-not-acceptance")
endif()

file(SHA256 "${S7_EXECUTABLE}" functional_executable_sha256)
file(SHA256 "${S7_MATRIX_EXECUTABLE}" matrix_executable_sha256)
file(SHA256 "${S7_LIBRARY}" library_sha256)
file(SHA256 "${sass_path}" sass_sha256)
file(SHA256 "${S7_SOURCE_ROOT}/src/cuda_v1.cu" cuda_source_sha256)
file(SHA256 "${S7_SOURCE_ROOT}/include/s6/cuda_v1.hpp" cuda_header_sha256)

file(WRITE "${S7_MANIFEST}"
"schema=S7-CUDA-V1-BINARY-EVIDENCE-v1.0\n"
"evidence_class=${evidence_class}\n"
"revision=${revision}\n"
"working_tree_state=${S7_WORKING_TREE_STATE}\n"
"working_tree_fingerprint=${S7_WORKING_TREE_FINGERPRINT}\n"
"source_manifest_sha256=${S7_SOURCE_MANIFEST_SHA256}\n"
"functional_executable_sha256=${functional_executable_sha256}\n"
"matrix_executable_sha256=${matrix_executable_sha256}\n"
"library_sha256=${library_sha256}\n"
"sass_sha256=${sass_sha256}\n"
"cuda_source_sha256=${cuda_source_sha256}\n"
"cuda_header_sha256=${cuda_header_sha256}\n"
"nvcc_version=${S7_NVCC_VERSION}\n"
"cuda_architectures=${S7_CUDA_ARCHITECTURES}\n"
"compile_policy=ftz-false,fmad-true,prec-div-true,prec-sqrt-true,no-fast-math\n"
"compile_policy_status=${compile_policy_status}\n"
"distance_kernel_symbol_status=${kernel_symbol_status}\n"
"distance_kernel_instruction_status=${distance_instruction_status}\n"
"distance_kernel_dependency_status=${distance_dependency_status}\n"
"distance_kernel_default_rn_no_ftz_status=${distance_default_rounding_status}\n"
"distance_kernel_fadd_count=${distance_fadd_count}\n"
"distance_kernel_ffma_count=${distance_ffma_count}\n"
"distance_kernel_fmul_count=${distance_fmul_count}\n"
"distance_kernel_ftz_or_non_rn_modifier_count=${distance_ftz_modifier_count}\n"
"binary_graph_structure_status=passed\n"
"graph_verdict=blocked-pending-independent-runtime-correlation-and-complete-acceptance-review\n"
"${artifact_lines}")
