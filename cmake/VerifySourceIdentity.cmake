if(NOT EXISTS "${S6_IDENTITY_FILE}")
    message(FATAL_ERROR "Source identity file is missing: ${S6_IDENTITY_FILE}")
endif()
file(STRINGS "${S6_IDENTITY_FILE}" identity_lines)
foreach(line IN LISTS identity_lines)
    if(line MATCHES "^([^=]+)=(.*)$")
        set("identity_${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
    endif()
endforeach()
if(NOT identity_schema STREQUAL "S6-SOURCE-IDENTITY-v1.0")
    message(FATAL_ERROR "Unsupported source identity schema")
endif()

execute_process(
    COMMAND "${S6_GIT}" rev-parse HEAD
    WORKING_DIRECTORY "${S6_SOURCE_ROOT}"
    OUTPUT_VARIABLE current_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${S6_GIT}" status --porcelain=v1 --untracked-files=all
    WORKING_DIRECTORY "${S6_SOURCE_ROOT}"
    OUTPUT_VARIABLE current_status
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
if(current_status STREQUAL "")
    set(current_state "clean")
else()
    set(current_state "dirty")
endif()
string(SHA256 current_fingerprint "${current_status}")
if(NOT current_revision STREQUAL identity_revision OR
   NOT current_state STREQUAL identity_working_tree_state OR
   NOT current_fingerprint STREQUAL identity_working_tree_fingerprint)
    message(FATAL_ERROR
        "Configured source identity is stale; rerun CMake before building evidence")
endif()

set(current_source_manifest "")
foreach(line IN LISTS identity_lines)
    if(line MATCHES "^source=([^|]+)[|]([0-9a-fA-F]+)$")
        set(relative_path "${CMAKE_MATCH_1}")
        set(expected_sha256 "${CMAKE_MATCH_2}")
        set(absolute_path "${S6_SOURCE_ROOT}/${relative_path}")
        if(NOT EXISTS "${absolute_path}")
            message(FATAL_ERROR "Evidence-driving source is missing: ${relative_path}")
        endif()
        file(SHA256 "${absolute_path}" actual_sha256)
        if(NOT actual_sha256 STREQUAL expected_sha256)
            message(FATAL_ERROR "Evidence-driving source changed after configure: ${relative_path}")
        endif()
        string(APPEND current_source_manifest
            "source=${relative_path}|${actual_sha256}\n")
    endif()
endforeach()
string(SHA256 current_source_manifest_sha256 "${current_source_manifest}")
if(NOT current_source_manifest_sha256 STREQUAL identity_source_manifest_sha256)
    message(FATAL_ERROR "Evidence-driving source manifest is stale")
endif()
