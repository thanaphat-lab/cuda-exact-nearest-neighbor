cmake_minimum_required(VERSION 3.21)

project(cuda_exact_nearest_neighbor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# S5.2 establishes infrastructure only. CPU/CUDA targets are added only after
# their design and implementation milestones are approved.

include(CTest)

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

if(BUILD_TESTING)
    add_executable(s6_cpu_tests tests/cpu_baseline_tests.cpp)
    target_link_libraries(s6_cpu_tests PRIVATE s6_cpu_baseline)

    add_executable(s6_cpu_matrix_tests tests/cpu_matrix_tests.cpp)
    target_link_libraries(s6_cpu_matrix_tests PRIVATE s6_cpu_baseline)

    if(MSVC)
        target_compile_options(s6_cpu_tests PRIVATE /W4 /WX /fp:strict /permissive-)
        target_compile_options(s6_cpu_matrix_tests PRIVATE /W4 /WX /fp:strict /permissive-)
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

    add_test(NAME s6.cpu.fixtures COMMAND s6_cpu_tests)
    add_test(NAME s6.cpu.required_matrix COMMAND s6_cpu_matrix_tests)
    set_tests_properties(s6.cpu.fixtures s6.cpu.required_matrix
        PROPERTIES TIMEOUT 900
    )
endif()
