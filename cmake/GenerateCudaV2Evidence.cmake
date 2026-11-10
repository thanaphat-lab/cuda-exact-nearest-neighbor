cmake_minimum_required(VERSION 3.21)

file(MAKE_DIRECTORY "${S7_OUTPUT_ROOT}")
set(sass_path "${S7_OUTPUT_ROOT}/cuda-v2.sass.txt")
execute_process(
    COMMAND "${S7_CUOBJDUMP}" --dump-sass "${S7_EXECUTABLE}"
    OUTPUT_FILE "${sass_path}"
    ERROR_VARIABLE sass_error
    RESULT_VARIABLE sass_result)
if(NOT sass_result EQUAL 0)
    message(FATAL_ERROR "cuobjdump failed for CUDA V2: ${sass_error}")
endif()

set(required_paths
    "${S7_EXECUTABLE}"
    "${S7_MATRIX_EXECUTABLE}"
    "${S7_LIBRARY}"
    "${S7_NVCC}"
    "${S7_BUILD_ROOT}/compile_commands.json"
    "${S7_BUILD_ROOT}/generated/s6/source_identity.txt"
    "${S7_SOURCE_ROOT}/CMakeLists.txt"
    "${S7_SOURCE_ROOT}/include/s6/cuda_v2.hpp"
    "${S7_SOURCE_ROOT}/src/cuda_v2.cu"
    "${S7_SOURCE_ROOT}/tests/cuda_v2_tests.cpp"
    "${S7_SOURCE_ROOT}/tests/cuda_v2_matrix_evidence_tests.cpp"
    "${S7_SOURCE_ROOT}/tests/cuda_v4_batch_matrix_tests.cpp"
    "${S7_SOURCE_ROOT}/cmake/GenerateCudaV2Evidence.cmake"
    "${S7_SOURCE_ROOT}/cmake/FinalizeCudaV2Evidence.cmake"
    "${S7_SOURCE_ROOT}/docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md"
    "${sass_path}")
foreach(path IN LISTS required_paths)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Required CUDA V2 evidence input missing: ${path}")
    endif()
endforeach()

file(READ "${S7_BUILD_ROOT}/compile_commands.json" compile_commands_text)
string(JSON compile_command_count LENGTH "${compile_commands_text}")
math(EXPR compile_command_last "${compile_command_count} - 1")
set(cuda_v2_compile_command "")
foreach(index RANGE 0 ${compile_command_last})
    string(JSON command_file GET "${compile_commands_text}" ${index} file)
    file(TO_CMAKE_PATH "${command_file}" normalized_file)
    if(normalized_file MATCHES "/src/cuda_v2[.]cu$")
        if(NOT cuda_v2_compile_command STREQUAL "")
            message(FATAL_ERROR "Multiple CUDA V2 compile commands were found")
        endif()
        string(JSON cuda_v2_compile_command GET "${compile_commands_text}" ${index} command)
    endif()
endforeach()
if(cuda_v2_compile_command STREQUAL "")
    message(FATAL_ERROR "CUDA V2 compile command was not found")
endif()
set(compile_policy_status "passed")
foreach(required_option IN ITEMS
        "--ftz=false" "--fmad=true" "--prec-div=true" "--prec-sqrt=true")
    string(FIND "${cuda_v2_compile_command}" "${required_option}" position)
    if(position EQUAL -1)
        set(compile_policy_status "failed")
    endif()
endforeach()
string(FIND "${cuda_v2_compile_command}" "--use_fast_math" fast_math_position)
if(NOT fast_math_position EQUAL -1)
    set(compile_policy_status "failed")
endif()
if(compile_policy_status STREQUAL "failed")
    message(FATAL_ERROR "CUDA V2 compile policy is nonconforming")
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
    message(FATAL_ERROR "CUDA V2 source identity changed after configuration")
endif()

file(READ "${sass_path}" sass_text)
foreach(marker IN ITEMS
        "cuda_v2_distance_block_reduce_kernel"
        "cuda_v2_record_block_reduce_kernel"
        "cuda_v4_batch_distance_block_reduce_kernel"
        "cuda_v4_batch_record_block_reduce_kernel")
    string(FIND "${sass_text}" "${marker}" marker_position)
    if(marker_position EQUAL -1)
        message(FATAL_ERROR "CUDA V2 kernel symbol missing from SASS: ${marker}")
    endif()
endforeach()
set(distance_kernel_symbol_status "passed")
set(record_kernel_symbol_status "passed")
set(batch_distance_kernel_symbol_status "passed")
set(batch_record_kernel_symbol_status "passed")

string(FIND "${sass_text}" "cuda_v2_distance_block_reduce_kernel" distance_position)
string(SUBSTRING "${sass_text}" ${distance_position} -1 distance_tail)
string(FIND "${distance_tail}" "\n\t\tFunction :" next_function_position)
if(next_function_position EQUAL -1)
    set(distance_sass "${distance_tail}")
else()
    string(SUBSTRING "${distance_tail}" 0 ${next_function_position} distance_sass)
endif()
string(REGEX MATCHALL "FADD[^\n]*" distance_fadd_lines "${distance_sass}")
string(REGEX MATCHALL "FFMA[^\n]*" distance_ffma_lines "${distance_sass}")
string(REGEX MATCHALL "FMUL[^\n]*" distance_fmul_lines "${distance_sass}")
list(LENGTH distance_fadd_lines distance_fadd_count)
list(LENGTH distance_ffma_lines distance_ffma_count)
list(LENGTH distance_fmul_lines distance_fmul_count)
if(distance_fadd_count LESS 1 OR distance_ffma_count LESS 1 OR
   NOT distance_fmul_count EQUAL 0)
    message(FATAL_ERROR
        "CUDA V2 distance SASS lacks the required subtraction/FMA structure")
endif()
set(distance_kernel_instruction_status "passed")
set(distance_kernel_dependency_status "passed")
string(REGEX MATCHALL "(FADD|FFMA)[^\n]*(FTZ|RM|RP|RZ)[^\n]*"
    nondefault_rounding_lines "${distance_sass}")
list(LENGTH nondefault_rounding_lines nondefault_rounding_count)
if(NOT nondefault_rounding_count EQUAL 0)
    message(FATAL_ERROR "CUDA V2 distance SASS uses FTZ or non-RN modifiers")
endif()
set(distance_kernel_default_rn_no_ftz_status "passed")

string(FIND "${sass_text}" "cuda_v4_batch_distance_block_reduce_kernel"
    batch_distance_position)
string(SUBSTRING "${sass_text}" ${batch_distance_position} -1 batch_distance_tail)
string(FIND "${batch_distance_tail}" "\n\t\tFunction :" batch_next_function_position)
if(batch_next_function_position EQUAL -1)
    set(batch_distance_sass "${batch_distance_tail}")
else()
    string(SUBSTRING "${batch_distance_tail}" 0 ${batch_next_function_position}
        batch_distance_sass)
endif()
string(REGEX MATCHALL "FADD[^\n]*" batch_distance_fadd_lines
    "${batch_distance_sass}")
string(REGEX MATCHALL "FFMA[^\n]*" batch_distance_ffma_lines
    "${batch_distance_sass}")
string(REGEX MATCHALL "FMUL[^\n]*" batch_distance_fmul_lines
    "${batch_distance_sass}")
list(LENGTH batch_distance_fadd_lines batch_distance_fadd_count)
list(LENGTH batch_distance_ffma_lines batch_distance_ffma_count)
list(LENGTH batch_distance_fmul_lines batch_distance_fmul_count)
if(batch_distance_fadd_count LESS 1 OR batch_distance_ffma_count LESS 1 OR
   NOT batch_distance_fmul_count EQUAL 0)
    message(FATAL_ERROR
        "CUDA V4 batch distance SASS lacks the required subtraction/FMA structure")
endif()
set(batch_distance_kernel_instruction_status "passed")
set(batch_distance_kernel_dependency_status "passed")
string(REGEX MATCHALL "(FADD|FFMA)[^\n]*(FTZ|RM|RP|RZ)[^\n]*"
    batch_nondefault_rounding_lines "${batch_distance_sass}")
list(LENGTH batch_nondefault_rounding_lines batch_nondefault_rounding_count)
if(NOT batch_nondefault_rounding_count EQUAL 0)
    message(FATAL_ERROR
        "CUDA V4 batch distance SASS uses FTZ or non-RN modifiers")
endif()
set(batch_distance_kernel_default_rn_no_ftz_status "passed")

string(FIND "${sass_text}" "cuda_v4_batch_record_block_reduce_kernel"
    batch_record_position)
string(SUBSTRING "${sass_text}" ${batch_record_position} -1 batch_record_tail)
string(FIND "${batch_record_tail}" "\n\t\tFunction :" batch_record_next_position)
if(batch_record_next_position EQUAL -1)
    set(batch_record_sass "${batch_record_tail}")
else()
    string(SUBSTRING "${batch_record_tail}" 0 ${batch_record_next_position}
        batch_record_sass)
endif()
string(REGEX MATCHALL "BAR[.](SYNC|SYNC[.]DEFER_BLOCKING)"
    batch_distance_barrier_lines "${batch_distance_sass}")
string(REGEX MATCHALL "BAR[.](SYNC|SYNC[.]DEFER_BLOCKING)"
    batch_record_barrier_lines "${batch_record_sass}")
list(LENGTH batch_distance_barrier_lines batch_distance_barrier_count)
list(LENGTH batch_record_barrier_lines batch_record_barrier_count)
if(batch_distance_barrier_count LESS 1 OR batch_record_barrier_count LESS 1)
    message(FATAL_ERROR
        "CUDA V4 batch SASS lacks per-kernel shared-reduction barriers")
endif()
set(batch_barrier_instruction_status "passed")

string(REGEX MATCHALL "BAR[.](SYNC|SYNC[.]DEFER_BLOCKING)" barrier_lines "${sass_text}")
list(LENGTH barrier_lines barrier_count)
if(barrier_count LESS 2)
    message(FATAL_ERROR "CUDA V2 SASS lacks shared-reduction barriers")
endif()
set(barrier_instruction_status "passed")
string(FIND "${sass_text}" "cuda_v2_serial_reduce_kernel" serial_position)
if(NOT serial_position EQUAL -1)
    message(FATAL_ERROR "CUDA V2 binary contains a V2 serial reducer")
endif()
set(serial_reducer_absence_status "passed")

set(artifact_lines "")
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
file(SHA256 "${S7_SOURCE_ROOT}/src/cuda_v2.cu" cuda_source_sha256)
file(SHA256 "${S7_SOURCE_ROOT}/include/s6/cuda_v2.hpp" cuda_header_sha256)

file(WRITE "${S7_MANIFEST}"
"schema=S7-CUDA-V2-BINARY-EVIDENCE-v1.0\n"
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
"distance_kernel_symbol_status=${distance_kernel_symbol_status}\n"
"record_kernel_symbol_status=${record_kernel_symbol_status}\n"
"batch_distance_kernel_symbol_status=${batch_distance_kernel_symbol_status}\n"
"batch_record_kernel_symbol_status=${batch_record_kernel_symbol_status}\n"
"batch_barrier_instruction_status=${batch_barrier_instruction_status}\n"
"batch_distance_barrier_count=${batch_distance_barrier_count}\n"
"batch_record_barrier_count=${batch_record_barrier_count}\n"
"barrier_instruction_status=${barrier_instruction_status}\n"
"barrier_instruction_count=${barrier_count}\n"
"distance_kernel_instruction_status=${distance_kernel_instruction_status}\n"
"distance_kernel_dependency_status=${distance_kernel_dependency_status}\n"
"distance_kernel_default_rn_no_ftz_status=${distance_kernel_default_rn_no_ftz_status}\n"
"distance_kernel_fadd_count=${distance_fadd_count}\n"
"distance_kernel_ffma_count=${distance_ffma_count}\n"
"distance_kernel_fmul_count=${distance_fmul_count}\n"
"batch_distance_kernel_instruction_status=${batch_distance_kernel_instruction_status}\n"
"batch_distance_kernel_dependency_status=${batch_distance_kernel_dependency_status}\n"
"batch_distance_kernel_default_rn_no_ftz_status=${batch_distance_kernel_default_rn_no_ftz_status}\n"
"batch_distance_kernel_fadd_count=${batch_distance_fadd_count}\n"
"batch_distance_kernel_ffma_count=${batch_distance_ffma_count}\n"
"batch_distance_kernel_fmul_count=${batch_distance_fmul_count}\n"
"serial_reducer_absence_status=${serial_reducer_absence_status}\n"
"binary_graph_structure_status=passed\n"
"graph_verdict=blocked-pending-independent-runtime-correlation-and-complete-acceptance-review\n"
"${artifact_lines}")
