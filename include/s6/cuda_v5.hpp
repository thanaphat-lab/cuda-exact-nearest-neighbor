#pragma once

#include "s6/cpu_baseline.hpp"

#include <cuda_runtime_api.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace s6 {

struct CudaV5Options {
    int device_ordinal = 0;
    cudaStream_t stream = nullptr;
    std::uint32_t threads_per_block = 256U;
    std::uint32_t query_tile = 4U;
};

constexpr bool cuda_v5_is_supported_block_size(std::uint32_t value) noexcept {
    return value == 128U || value == 256U || value == 512U;
}

constexpr bool cuda_v5_is_supported_query_tile(std::uint32_t value) noexcept {
    return value == 1U || value == 4U || value == 8U;
}

namespace detail {
struct CudaV5PreparedDatasetState;
struct CudaV5WorkspaceState;
struct CudaV5SearchAccess;
enum class CudaV5InjectedFailure {
    none,
    resource_allocation,
    backend_copy,
    backend_launch,
    backend_synchronize,
};

struct CudaV5BenchmarkOutcome {
    BatchProductionOutcome production;
    std::uint64_t kernel_ns = 0U;
    bool device_timing_available = false;
    std::string timing_detail;
};
}  // namespace detail

class CudaV5PreparedDataset;
class CudaV5Workspace;
struct CudaV5PreparedDatasetOutcome;
struct CudaV5WorkspaceOutcome;

class CudaV5PreparedDataset {
  public:
    CudaV5PreparedDataset() noexcept;
    ~CudaV5PreparedDataset();
    CudaV5PreparedDataset(CudaV5PreparedDataset&&) noexcept;
    CudaV5PreparedDataset& operator=(CudaV5PreparedDataset&&) noexcept;
    CudaV5PreparedDataset(const CudaV5PreparedDataset&) = delete;
    CudaV5PreparedDataset& operator=(const CudaV5PreparedDataset&) = delete;

    bool valid() const noexcept;
    std::uint64_t candidate_count() const noexcept;
    std::uint64_t dimension() const noexcept;
    int device_ordinal() const noexcept;

  private:
    std::unique_ptr<detail::CudaV5PreparedDatasetState> state_;
    friend CudaV5PreparedDatasetOutcome cuda_v5_prepare_dataset_float32(
        DatasetView, const CudaV5Options&);
    friend CudaV5WorkspaceOutcome cuda_v5_create_workspace(
        const CudaV5PreparedDataset&, std::uint64_t, const CudaV5Options&);
    friend struct detail::CudaV5SearchAccess;
};

class CudaV5Workspace {
  public:
    CudaV5Workspace() noexcept;
    ~CudaV5Workspace();
    CudaV5Workspace(CudaV5Workspace&&) noexcept;
    CudaV5Workspace& operator=(CudaV5Workspace&&) noexcept;
    CudaV5Workspace(const CudaV5Workspace&) = delete;
    CudaV5Workspace& operator=(const CudaV5Workspace&) = delete;

    bool valid() const noexcept;
    std::uint64_t capacity() const noexcept;

  private:
    std::unique_ptr<detail::CudaV5WorkspaceState> state_;
    friend CudaV5WorkspaceOutcome cuda_v5_create_workspace(
        const CudaV5PreparedDataset&, std::uint64_t, const CudaV5Options&);
    friend struct detail::CudaV5SearchAccess;
};

struct CudaV5PreparedDatasetOutcome {
    InvocationStatus invocation_status = InvocationStatus::not_executed;
    InputOutcome input_outcome = InputOutcome::not_applicable;
    DomainStatus domain_status = DomainStatus::not_applicable;
    std::optional<CudaV5PreparedDataset> prepared_dataset;
    std::string backend_detail;
};

struct CudaV5WorkspaceOutcome {
    InvocationStatus invocation_status = InvocationStatus::not_executed;
    std::optional<CudaV5Workspace> workspace;
    std::string backend_detail;
};

BatchProductionOutcome cuda_v5_search_batch_float32(
    DatasetView dataset, QueryBatchView queries,
    const CudaV5Options& options = {});
CudaV5PreparedDatasetOutcome cuda_v5_prepare_dataset_float32(
    DatasetView dataset, const CudaV5Options& options = {});
CudaV5WorkspaceOutcome cuda_v5_create_workspace(
    const CudaV5PreparedDataset& dataset, std::uint64_t maximum_query_count,
    const CudaV5Options& options = {});
BatchProductionOutcome cuda_v5_search_prepared_batch_float32(
    const CudaV5PreparedDataset& dataset, QueryBatchView queries,
    CudaV5Workspace& workspace, const CudaV5Options& options = {});

namespace detail {
CudaV5BenchmarkOutcome cuda_v5_search_prepared_batch_float32_for_benchmark(
    const CudaV5PreparedDataset& dataset, QueryBatchView queries,
    CudaV5Workspace& workspace, const CudaV5Options& options);
BatchProductionOutcome cuda_v5_search_prepared_batch_float32_for_testing(
    const CudaV5PreparedDataset& dataset, QueryBatchView queries,
    CudaV5Workspace& workspace, const CudaV5Options& options,
    CudaV5InjectedFailure injected_failure);
}  // namespace detail

}  // namespace s6
