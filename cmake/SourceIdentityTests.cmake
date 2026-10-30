file(READ "${S6_IDENTITY_FILE}" original_identity)
file(MAKE_DIRECTORY "${S6_OUTPUT_ROOT}")

function(expect_identity_rejected name forged_identity)
    set(path "${S6_OUTPUT_ROOT}/${name}.txt")
    file(WRITE "${path}" "${forged_identity}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DS6_IDENTITY_FILE=${path}
            -DS6_SOURCE_ROOT=${S6_SOURCE_ROOT}
            -DS6_GIT=${S6_GIT}
            -P "${S6_SOURCE_ROOT}/cmake/VerifySourceIdentity.cmake"
        RESULT_VARIABLE result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(result EQUAL 0)
        message(FATAL_ERROR "Invalid source identity was accepted: ${name}")
    endif()
endfunction()

string(REGEX REPLACE "revision=[0-9a-f]+"
    "revision=0000000000000000000000000000000000000000"
    stale_revision "${original_identity}")
expect_identity_rejected("stale-revision" "${stale_revision}")

string(REPLACE "working_tree_state=clean" "working_tree_state=dirty"
    dirty_configured_state "${original_identity}")
expect_identity_rejected("dirty-configured-state" "${dirty_configured_state}")

string(REGEX REPLACE "working_tree_fingerprint=[0-9a-f]+"
    "working_tree_fingerprint=0000000000000000000000000000000000000000000000000000000000000000"
    dirty_fingerprint "${original_identity}")
expect_identity_rejected("dirty-fingerprint" "${dirty_fingerprint}")

foreach(relative_path IN ITEMS
        "cmake/GenerateCpuEvidence.cmake"
        "tests/cpu_matrix_tests.cpp"
        "tests/cpu_baseline_tests.cpp")
    string(REGEX REPLACE
        "source=${relative_path}[|][0-9a-f]+"
        "source=${relative_path}|0000000000000000000000000000000000000000000000000000000000000000"
        forged_source "${original_identity}")
    string(REPLACE "/" "-" name "${relative_path}")
    string(REPLACE "." "-" name "${name}")
    expect_identity_rejected("stale-${name}" "${forged_source}")
endforeach()

message(STATUS "S6 source identity negative tests: PASS")
