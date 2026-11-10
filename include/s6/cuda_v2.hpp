#pragma once

#include "s6/cpu_baseline.hpp"

#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace s6 {

inline constexpr std::uint32_t kCudaV2DefaultThreadsPerBlock = 256U;

struct CudaV2Options {
    int device_ordinal = 0;
    cudaStream_t stream = nullptr;
    std::uint32_t threads_per_block = kCudaV2DefaultThreadsPerBlock;
};

namespace detail {
struct CudaV2PreparedDatasetState;
struct CudaV2PreparedSearchAccess;
struct CudaV2WorkspaceState;
}  // namespace detail

class CudaV2PreparedDataset;
class CudaV2Workspace;
struct CudaV2PreparedDatasetOutcome;
struct CudaV2WorkspaceOutcome;

class CudaV2PreparedDataset {
  public:
    CudaV2PreparedDataset() noexcept;
    ~CudaV2PreparedDataset();
    CudaV2PreparedDataset(CudaV2PreparedDataset&&) noexcept;
    CudaV2PreparedDataset& operator=(CudaV2PreparedDataset&&) noexcept;
    CudaV2PreparedDataset(const CudaV2PreparedDataset&) = delete;
    CudaV2PreparedDataset& operator=(const CudaV2PreparedDataset&) = delete;

    bool valid() const noexcept;
    std::uint64_t candidate_count() const noexcept;
    std::uint64_t dimension() const noexcept;
    int device_ordinal() const noexcept;

  private:
    std::unique_ptr<detail::CudaV2PreparedDatasetState> state_;

    friend CudaV2PreparedDatasetOutcome cuda_v2_prepare_dataset_float32(
        DatasetView, const CudaV2Options&);
    friend CudaV2WorkspaceOutcome cuda_v2_create_workspace(
        const CudaV2PreparedDataset&, const CudaV2Options&);
    friend ProductionOutcome cuda_v2_search_prepared_float32(
        const CudaV2PreparedDataset&, QueryView, CudaV2Workspace&,
        const CudaV2Options&);
    friend struct detail::CudaV2PreparedSearchAccess;
};

class CudaV2Workspace {
  public:
    CudaV2Workspace() noexcept;
    ~CudaV2Workspace();
    CudaV2Workspace(CudaV2Workspace&&) noexcept;
    CudaV2Workspace& operator=(CudaV2Workspace&&) noexcept;
    CudaV2Workspace(const CudaV2Workspace&) = delete;
    CudaV2Workspace& operator=(const CudaV2Workspace&) = delete;

    bool valid() const noexcept;

  private:
    std::unique_ptr<detail::CudaV2WorkspaceState> state_;

    friend CudaV2WorkspaceOutcome cuda_v2_create_workspace(
        const CudaV2PreparedDataset&, const CudaV2Options&);
    friend ProductionOutcome cuda_v2_search_prepared_float32(
        const CudaV2PreparedDataset&, QueryView, CudaV2Workspace&,
        const CudaV2Options&);
    friend struct detail::CudaV2PreparedSearchAccess;
};

struct CudaV2PreparedDatasetOutcome {
    InvocationStatus invocation_status = InvocationStatus::not_executed;
    InputOutcome input_outcome = InputOutcome::not_applicable;
    DomainStatus domain_status = DomainStatus::not_applicable;
    std::optional<CudaV2PreparedDataset> prepared_dataset;
    std::string backend_detail;
};

struct CudaV2WorkspaceOutcome {
    InvocationStatus invocation_status = InvocationStatus::not_executed;
    std::optional<CudaV2Workspace> workspace;
    std::string backend_detail;
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
CudaV2PreparedDatasetOutcome cuda_v2_prepare_dataset_float32(
    DatasetView dataset, const CudaV2Options& options = {});
CudaV2WorkspaceOutcome cuda_v2_create_workspace(
    const CudaV2PreparedDataset& dataset,
    const CudaV2Options& options = {});
ProductionOutcome cuda_v2_search_prepared_float32(
    const CudaV2PreparedDataset& dataset, QueryView query,
    CudaV2Workspace& workspace, const CudaV2Options& options = {});

std::size_t cuda_v2_reduction_record_size() noexcept;
std::size_t cuda_v2_reduction_record_alignment() noexcept;

namespace detail {

enum class CudaV2TransposeTileCandidate {
    tile_32x32,
    tile_64x32,
    tile_64x64,
};

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
ProductionOutcome cuda_v2_search_prepared_float32_for_testing(
    const CudaV2PreparedDataset& dataset, QueryView query,
    CudaV2Workspace& workspace, const CudaV2Options& options,
    CudaV2InjectedFailure injected_failure);
CudaV2ReductionDiagnostics cuda_v2_reduce_diagnostics_for_testing(
    const std::vector<CudaV2ReductionInput>& records,
    const CudaV2Options& options = {});

std::vector<float> cuda_v2_transform_aos_to_soa_for_testing(DatasetView dataset);
std::vector<float> cuda_v2_transform_aos_to_soa_for_testing(
    DatasetView dataset, CudaV2TransposeTileCandidate tile);

}  // namespace detail

}  // namespace s6
