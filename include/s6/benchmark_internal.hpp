#pragma once

#include "s6/cpu_baseline.hpp"
#include "s6/cuda_v0.hpp"
#include "s6/cuda_v1.hpp"
#include "s6/cuda_v2.hpp"
#include "s6/cuda_v3.hpp"

#include <cstdint>
#include <string>

namespace s6::detail {

struct S7CudaStageTiming {
    std::uint64_t validation_ns = 0;
    std::uint64_t conversion_ns = 0;
    std::uint64_t allocation_ns = 0;
    std::uint64_t h2d_ns = 0;
    std::uint64_t workspace_reset_ns = 0;
    std::uint64_t kernel_ns = 0;
    std::uint64_t d2h_ns = 0;
    std::uint64_t synchronize_ns = 0;
    bool device_timing_available = false;
    std::string timing_detail;
};

struct S7StagedOutcome {
    ProductionOutcome production;
    S7CudaStageTiming stages;
};

S7StagedOutcome cuda_v0_search_float32_staged_for_benchmark(
    DatasetView dataset, QueryView query, const CudaV0Options& options = {});
S7StagedOutcome cuda_v1_search_float32_staged_for_benchmark(
    DatasetView dataset, QueryView query, const CudaV1Options& options = {});
S7StagedOutcome cuda_v2_search_float32_staged_for_benchmark(
    DatasetView dataset, QueryView query, const CudaV2Options& options = {});
S7StagedOutcome cuda_v3_search_float32_staged_for_benchmark(
    DatasetView dataset, QueryView query, const CudaV3Options& options = {});

}  // namespace s6::detail
