#pragma once

#include "s6/cpu_baseline.hpp"

#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace s6 {

inline constexpr std::uint32_t kCudaV3DefaultThreadsPerBlock = 256U;

struct CudaV3Options {
    int device_ordinal = 0;
    cudaStream_t stream = nullptr;
    std::uint32_t threads_per_block = kCudaV3DefaultThreadsPerBlock;
};

struct CudaV3DeviceInfo {
    EvidenceStatus status = EvidenceStatus::blocked;
    int device_ordinal = -1;
    std::string name;
    std::string uuid;
    int compute_major = 0;
    int compute_minor = 0;
    std::size_t total_global_memory = 0;
    int maximum_threads_per_block = 0;
    int maximum_grid_x = 0;
    int warp_size = 0;
    int driver_version = 0;
    int runtime_version = 0;
    std::string detail;
};

struct CudaV3AbiInfo {
    EvidenceStatus status = EvidenceStatus::blocked;
    std::size_t host_size = 0;
    std::size_t host_alignment = 0;
    std::size_t host_distance_offset = 0;
    std::size_t host_flags_offset = 0;
    std::size_t host_index_offset = 0;
    std::uint64_t device_size = 0;
    std::uint64_t device_alignment = 0;
    std::uint64_t device_distance_offset = 0;
    std::uint64_t device_flags_offset = 0;
    std::uint64_t device_index_offset = 0;
    std::string detail;
};

struct CudaV3ExecutionEvidence {
    EvidenceStatus mapping_status = EvidenceStatus::blocked;
    EvidenceStatus overwrite_status = EvidenceStatus::blocked;
    EvidenceStatus candidate_distance_status = EvidenceStatus::blocked;
    std::uint64_t expected_candidate_count = 0;
    std::uint64_t observed_mapping_count = 0;
    std::vector<float> candidate_distances;
    std::string detail;
};

struct CudaV3EvidencedOutcome {
    ProductionOutcome production;
    CudaV3ExecutionEvidence evidence;
};

CudaV3DeviceInfo inspect_cuda_v3_device(int device_ordinal = 0);
CudaV3AbiInfo inspect_cuda_v3_reduction_record_abi(
    const CudaV3Options& options = {});
ProductionOutcome cuda_v3_search_float32(
    DatasetView dataset, QueryView query, const CudaV3Options& options = {});
CudaV3EvidencedOutcome cuda_v3_search_float32_evidenced(
    DatasetView dataset, QueryView query, const CudaV3Options& options = {});

std::size_t cuda_v3_reduction_record_size() noexcept;
std::size_t cuda_v3_reduction_record_alignment() noexcept;

namespace detail {

enum class CudaV3InjectedFailure {
    none,
    resource_allocation,
    backend_copy,
    backend_launch,
    backend_synchronize,
};

struct CudaV3ReductionInput {
    float distance = 0.0F;
    std::uint64_t index = 0;
    bool logical_candidate_present = true;
};

struct CudaV3ReductionDiagnostics {
    ProductionOutcome production;
    bool logical_candidate_present = false;
    bool finite_minimum_present = false;
    bool nonfinite_arithmetic_seen = false;
    bool domain_failure_seen = false;
    float finite_minimum_distance = 0.0F;
    std::uint64_t finite_minimum_index = 0;
};

CudaV3EvidencedOutcome cuda_v3_search_float32_for_testing(
    DatasetView dataset, QueryView query, const CudaV3Options& options,
    CudaV3InjectedFailure injected_failure);
CudaV3ReductionDiagnostics cuda_v3_reduce_diagnostics_for_testing(
    const std::vector<CudaV3ReductionInput>& records,
    const CudaV3Options& options = {});

std::vector<float> cuda_v3_transform_aos_to_soa_for_testing(DatasetView dataset);

}  // namespace detail

}  // namespace s6
