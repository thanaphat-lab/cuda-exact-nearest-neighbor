#pragma once

#include "s6/cpu_baseline.hpp"

#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace s6 {

inline constexpr std::uint32_t kCudaV2DefaultThreadsPerBlock = 256U;

struct CudaV2Options {
    int device_ordinal = 0;
    cudaStream_t stream = nullptr;
    std::uint32_t threads_per_block = kCudaV2DefaultThreadsPerBlock;
};

struct CudaV2DeviceInfo {
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

struct CudaV2AbiInfo {
    EvidenceStatus status = EvidenceStatus::blocked;
    std::size_t host_size = 0;
    std::size_t host_alignment = 0;
    std::uint64_t device_size = 0;
    std::uint64_t device_alignment = 0;
    std::string detail;
};

struct CudaV2ExecutionEvidence {
    EvidenceStatus mapping_status = EvidenceStatus::blocked;
    EvidenceStatus overwrite_status = EvidenceStatus::blocked;
    EvidenceStatus candidate_distance_status = EvidenceStatus::blocked;
    std::uint64_t expected_candidate_count = 0;
    std::uint64_t observed_mapping_count = 0;
    std::vector<float> candidate_distances;
    std::string detail;
};

struct CudaV2EvidencedOutcome {
    ProductionOutcome production;
    CudaV2ExecutionEvidence evidence;
};

CudaV2DeviceInfo inspect_cuda_v2_device(int device_ordinal = 0);
CudaV2AbiInfo inspect_cuda_v2_reduction_record_abi(
    const CudaV2Options& options = {});
ProductionOutcome cuda_v2_search_float32(
    DatasetView dataset, QueryView query, const CudaV2Options& options = {});
CudaV2EvidencedOutcome cuda_v2_search_float32_evidenced(
    DatasetView dataset, QueryView query, const CudaV2Options& options = {});

std::size_t cuda_v2_reduction_record_size() noexcept;
std::size_t cuda_v2_reduction_record_alignment() noexcept;

namespace detail {

enum class CudaV2InjectedFailure {
    none,
    resource_allocation,
    backend_copy,
    backend_launch,
    backend_synchronize,
};

struct CudaV2ReductionInput {
    float distance = 0.0F;
    std::uint64_t index = 0;
    bool logical_candidate_present = true;
};

struct CudaV2ReductionDiagnostics {
    ProductionOutcome production;
    bool logical_candidate_present = false;
    bool finite_minimum_present = false;
    bool nonfinite_arithmetic_seen = false;
    bool domain_failure_seen = false;
    float finite_minimum_distance = 0.0F;
    std::uint64_t finite_minimum_index = 0;
};

CudaV2EvidencedOutcome cuda_v2_search_float32_for_testing(
    DatasetView dataset, QueryView query, const CudaV2Options& options,
    CudaV2InjectedFailure injected_failure);
CudaV2ReductionDiagnostics cuda_v2_reduce_diagnostics_for_testing(
    const std::vector<CudaV2ReductionInput>& records,
    const CudaV2Options& options = {});

std::vector<float> cuda_v2_transform_aos_to_soa_for_testing(DatasetView dataset);

}  // namespace detail

}  // namespace s6
