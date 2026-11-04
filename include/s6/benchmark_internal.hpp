#pragma once

#include "s6/cpu_baseline.hpp"
#include "s6/cuda_v0.hpp"
#include "s6/cuda_v1.hpp"
#include "s6/cuda_v2.hpp"
#include "s6/cuda_v3.hpp"

#include <cstdint>
#include <string>

namespace s6::detail {

enum class S8TransposeImplementation {
    original,
    tile_32x32,
    tile_64x32,
    tile_64x64,
};

inline const char* s8_transpose_implementation_name(
    S8TransposeImplementation implementation) noexcept {
    switch (implementation) {
        case S8TransposeImplementation::original: return "original";
        case S8TransposeImplementation::tile_32x32: return "32x32";
        case S8TransposeImplementation::tile_64x32: return "64x32";
        case S8TransposeImplementation::tile_64x64: return "64x64";
    }
    return "unknown";
}

struct S7CudaStageTiming {
    std::uint64_t validation_ns = 0;
    std::uint64_t conversion_ns = 0;
    std::uint64_t transpose_ns = 0;
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

ProductionOutcome cuda_v1_search_float32_for_s8_benchmark(
    DatasetView dataset, QueryView query, S8TransposeImplementation implementation,
    const CudaV1Options& options = {});
CudaV1EvidencedOutcome cuda_v1_search_float32_evidenced_for_s8_testing(
    DatasetView dataset, QueryView query, S8TransposeImplementation implementation,
    const CudaV1Options& options = {});
S7StagedOutcome cuda_v1_search_float32_staged_for_s8_benchmark(
    DatasetView dataset, QueryView query, S8TransposeImplementation implementation,
    const CudaV1Options& options = {});

ProductionOutcome cuda_v2_search_float32_for_s8_benchmark(
    DatasetView dataset, QueryView query, S8TransposeImplementation implementation,
    const CudaV2Options& options = {});
CudaV2EvidencedOutcome cuda_v2_search_float32_evidenced_for_s8_testing(
    DatasetView dataset, QueryView query, S8TransposeImplementation implementation,
    const CudaV2Options& options = {});
S7StagedOutcome cuda_v2_search_float32_staged_for_s8_benchmark(
    DatasetView dataset, QueryView query, S8TransposeImplementation implementation,
    const CudaV2Options& options = {});

ProductionOutcome cuda_v3_search_float32_for_s8_benchmark(
    DatasetView dataset, QueryView query, S8TransposeImplementation implementation,
    const CudaV3Options& options = {});
CudaV3EvidencedOutcome cuda_v3_search_float32_evidenced_for_s8_testing(
    DatasetView dataset, QueryView query, S8TransposeImplementation implementation,
    const CudaV3Options& options = {});
S7StagedOutcome cuda_v3_search_float32_staged_for_s8_benchmark(
    DatasetView dataset, QueryView query, S8TransposeImplementation implementation,
    const CudaV3Options& options = {});

}  // namespace s6::detail
