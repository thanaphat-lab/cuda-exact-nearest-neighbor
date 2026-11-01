#pragma once

#include "s6/cpu_baseline.hpp"

#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace s6 {

inline constexpr std::uint32_t kCudaV0ThreadsPerBlock = 256U;

struct CudaV0Options {
    int device_ordinal = 0;
    cudaStream_t stream = nullptr;
};

struct CudaV0DeviceInfo {
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

struct CudaV0AbiInfo {
    EvidenceStatus status = EvidenceStatus::blocked;
    std::size_t host_size = 0;
    std::size_t host_alignment = 0;
    std::uint64_t device_size = 0;
    std::uint64_t device_alignment = 0;
    std::string detail;
};

struct CudaV0ExecutionEvidence {
    EvidenceStatus mapping_status = EvidenceStatus::blocked;
    EvidenceStatus overwrite_status = EvidenceStatus::blocked;
    EvidenceStatus candidate_distance_status = EvidenceStatus::blocked;
    std::uint64_t expected_candidate_count = 0;
    std::uint64_t observed_mapping_count = 0;
    std::vector<float> candidate_distances;
    std::string detail;
};

struct CudaV0EvidencedOutcome {
    ProductionOutcome production;
    CudaV0ExecutionEvidence evidence;
};

CudaV0DeviceInfo inspect_cuda_v0_device(int device_ordinal = 0);
CudaV0AbiInfo inspect_cuda_v0_final_record_abi(
    const CudaV0Options& options = {});
ProductionOutcome cuda_v0_search_float32(
    DatasetView dataset, QueryView query, const CudaV0Options& options = {});
CudaV0EvidencedOutcome cuda_v0_search_float32_evidenced(
    DatasetView dataset, QueryView query, const CudaV0Options& options = {});

std::size_t cuda_v0_final_record_size() noexcept;
std::size_t cuda_v0_final_record_alignment() noexcept;

namespace detail {

enum class CudaV0InjectedFailure {
    none,
    resource_allocation,
    backend_copy,
    backend_launch,
    backend_synchronize,
};

struct CudaV0ReductionDiagnostics {
    ProductionOutcome production;
    bool logical_candidate_present = false;
    bool finite_minimum_present = false;
    bool nonfinite_arithmetic_seen = false;
    bool domain_failure_seen = false;
    float finite_minimum_distance = 0.0F;
    std::uint64_t finite_minimum_index = 0;
};

CudaV0EvidencedOutcome cuda_v0_search_float32_for_testing(
    DatasetView dataset, QueryView query, const CudaV0Options& options,
    CudaV0InjectedFailure injected_failure);
ProductionOutcome cuda_v0_reduce_fixture_for_testing(
    const std::vector<float>& distances, const CudaV0Options& options = {});
CudaV0ReductionDiagnostics cuda_v0_reduce_diagnostics_for_testing(
    const std::vector<float>& distances, const CudaV0Options& options = {});

}  // namespace detail

}  // namespace s6
