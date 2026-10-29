get_filename_component(output_directory "${S6_PREFIX}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

set(object_disassembly "${S6_PREFIX}.object.disasm.txt")
set(executable_disassembly "${S6_PREFIX}.executable.disasm.txt")
set(symbols "${S6_PREFIX}.symbols.txt")
set(library_symbols "${S6_PREFIX}.library.symbols.txt")

execute_process(
    COMMAND "${S6_DUMPBIN}" /DISASM "${S6_OBJECT}"
    OUTPUT_FILE "${object_disassembly}"
    RESULT_VARIABLE object_disassembly_result
)
execute_process(
    COMMAND "${S6_DUMPBIN}" /DISASM "${S6_EXECUTABLE}"
    OUTPUT_FILE "${executable_disassembly}"
    RESULT_VARIABLE executable_disassembly_result
)
execute_process(
    COMMAND "${S6_DUMPBIN}" /SYMBOLS "${S6_OBJECT}"
    OUTPUT_FILE "${symbols}"
    RESULT_VARIABLE symbols_result
)
execute_process(
    COMMAND "${S6_DUMPBIN}" /LINKERMEMBER:1 "${S6_LIBRARY}"
    OUTPUT_FILE "${library_symbols}"
    RESULT_VARIABLE library_symbols_result
)
if(NOT object_disassembly_result EQUAL 0 OR
   NOT executable_disassembly_result EQUAL 0 OR
   NOT symbols_result EQUAL 0 OR
   NOT library_symbols_result EQUAL 0)
    message(FATAL_ERROR "dumpbin failed while generating CPU evidence")
endif()

set(baseline_header "${S6_SOURCE_ROOT}/include/s6/cpu_baseline.hpp")
set(evidence_header "${S6_SOURCE_ROOT}/include/s6/cpu_evidence.hpp")
set(baseline_source "${S6_SOURCE_ROOT}/src/cpu_baseline.cpp")
set(evidence_source "${S6_SOURCE_ROOT}/src/cpu_evidence.cpp")
set(cmake_source "${S6_SOURCE_ROOT}/CMakeLists.txt")
set(policy "${S6_SOURCE_ROOT}/docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md")
set(compile_commands "${S6_BUILD_ROOT}/compile_commands.json")

foreach(path IN ITEMS
        S6_EXECUTABLE S6_LIBRARY S6_OBJECT S6_MAP S6_COMPILER
        baseline_header evidence_header baseline_source evidence_source
        cmake_source policy compile_commands object_disassembly
        executable_disassembly symbols)
    if(NOT EXISTS "${${path}}")
        message(FATAL_ERROR "Required CPU evidence artifact missing: ${${path}}")
    endif()
endforeach()
if(NOT EXISTS "${library_symbols}")
    message(FATAL_ERROR "Required CPU evidence artifact missing: ${library_symbols}")
endif()

file(SHA256 "${S6_EXECUTABLE}" executable_sha256)
file(SHA256 "${S6_LIBRARY}" library_sha256)
file(SHA256 "${S6_OBJECT}" object_sha256)
file(SHA256 "${S6_MAP}" map_sha256)
file(SHA256 "${S6_COMPILER}" compiler_sha256)
file(SHA256 "${baseline_header}" baseline_header_sha256)
file(SHA256 "${evidence_header}" evidence_header_sha256)
file(SHA256 "${baseline_source}" baseline_source_sha256)
file(SHA256 "${evidence_source}" evidence_source_sha256)
file(SHA256 "${cmake_source}" cmake_sha256)
file(SHA256 "${policy}" policy_sha256)
file(SHA256 "${compile_commands}" compile_commands_sha256)
file(SHA256 "${object_disassembly}" object_disassembly_sha256)
file(SHA256 "${executable_disassembly}" executable_disassembly_sha256)
file(SHA256 "${symbols}" symbols_sha256)
file(SHA256 "${library_symbols}" library_symbols_sha256)

file(WRITE "${S6_MANIFEST}"
"schema=S6-CPU-PRODUCTION-EVIDENCE-v1.1\n"
"target=${S6_TARGET_NAME}\n"
"revision=${S6_REVISION}\n"
"working_tree_state=${S6_WORKTREE_STATE}\n"
"compiler_id=${S6_COMPILER_ID}\n"
"compiler_version=${S6_COMPILER_VERSION}\n"
"required_compile_flags=${S6_REQUIRED_FLAGS}\n"
"policy_sha256=${policy_sha256}\n"
"policy_path=${policy}\n"
"compiler_path=${S6_COMPILER}\n"
"compiler_sha256=${compiler_sha256}\n"
"baseline_header_path=${baseline_header}\n"
"baseline_header_sha256=${baseline_header_sha256}\n"
"evidence_header_path=${evidence_header}\n"
"evidence_header_sha256=${evidence_header_sha256}\n"
"baseline_source_path=${baseline_source}\n"
"baseline_source_sha256=${baseline_source_sha256}\n"
"evidence_source_path=${evidence_source}\n"
"evidence_source_sha256=${evidence_source_sha256}\n"
"cmake_path=${cmake_source}\n"
"cmake_sha256=${cmake_sha256}\n"
"compile_commands_path=${compile_commands}\n"
"compile_commands_sha256=${compile_commands_sha256}\n"
"object_path=${S6_OBJECT}\n"
"object_sha256=${object_sha256}\n"
"library_path=${S6_LIBRARY}\n"
"library_sha256=${library_sha256}\n"
"executable_path=${S6_EXECUTABLE}\n"
"executable_sha256=${executable_sha256}\n"
"map_path=${S6_MAP}\n"
"map_sha256=${map_sha256}\n"
"object_disassembly_path=${object_disassembly}\n"
"object_disassembly_sha256=${object_disassembly_sha256}\n"
"executable_disassembly_path=${executable_disassembly}\n"
"executable_disassembly_sha256=${executable_disassembly_sha256}\n"
"symbols_path=${symbols}\n"
"symbols_sha256=${symbols_sha256}\n"
"library_symbols_path=${library_symbols}\n"
"library_symbols_sha256=${library_symbols_sha256}\n")
