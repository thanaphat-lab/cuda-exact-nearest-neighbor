#pragma once

#include "s6/cpu_baseline.hpp"

#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace s6 {

inline constexpr std::uint32_t kCudaV1ThreadsPerBlock = 256U;

struct CudaV1Options {
    int device_ordinal = 0;
    cudaStream_t stream = nullptr;
};

struct CudaV1DeviceInfo {
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

struct CudaV1AbiInfo {
    EvidenceStatus status = EvidenceStatus::blocked;
    std::size_t host_size = 0;
    std::size_t host_alignment = 0;
    std::uint64_t device_size = 0;
    std::uint64_t device_alignment = 0;
    std::string detail;
};

struct CudaV1ExecutionEvidence {
    EvidenceStatus mapping_status = EvidenceStatus::blocked;
    EvidenceStatus overwrite_status = EvidenceStatus::blocked;
    EvidenceStatus candidate_distance_status = EvidenceStatus::blocked;
    std::uint64_t expected_candidate_count = 0;
    std::uint64_t observed_mapping_count = 0;
    std::vector<float> candidate_distances;
    std::string detail;
};

struct CudaV1EvidencedOutcome {
    ProductionOutcome production;
    CudaV1ExecutionEvidence evidence;
};

CudaV1DeviceInfo inspect_cuda_v1_device(int device_ordinal = 0);
CudaV1AbiInfo inspect_cuda_v1_final_record_abi(
    const CudaV1Options& options = {});
ProductionOutcome cuda_v1_search_float32(
    DatasetView dataset, QueryView query, const CudaV1Options& options = {});
CudaV1EvidencedOutcome cuda_v1_search_float32_evidenced(
    DatasetView dataset, QueryView query, const CudaV1Options& options = {});

std::size_t cuda_v1_final_record_size() noexcept;
std::size_t cuda_v1_final_record_alignment() noexcept;

namespace detail {

enum class CudaV1InjectedFailure {
    none,
    resource_allocation,
    backend_copy,
    backend_launch,
    backend_synchronize,
};

struct CudaV1ReductionDiagnostics {
    ProductionOutcome production;
    bool logical_candidate_present = false;
    bool finite_minimum_present = false;
    bool nonfinite_arithmetic_seen = false;
    bool domain_failure_seen = false;
    float finite_minimum_distance = 0.0F;
    std::uint64_t finite_minimum_index = 0;
};

CudaV1EvidencedOutcome cuda_v1_search_float32_for_testing(
    DatasetView dataset, QueryView query, const CudaV1Options& options,
    CudaV1InjectedFailure injected_failure);
ProductionOutcome cuda_v1_reduce_fixture_for_testing(
    const std::vector<float>& distances, const CudaV1Options& options = {});
CudaV1ReductionDiagnostics cuda_v1_reduce_diagnostics_for_testing(
    const std::vector<float>& distances, const CudaV1Options& options = {});

std::vector<float> cuda_v1_transform_aos_to_soa_for_testing(DatasetView dataset);

}  // namespace detail

}  // namespace s6


