#include "s6/cuda_v5.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace s6 {
namespace {

constexpr unsigned int kPresent = 1U;
constexpr unsigned int kNonfinite = 2U;

struct alignas(16) V5Record {
    float distance;
    unsigned int flags;
    std::uint64_t index;
};
static_assert(sizeof(V5Record) == 16U, "V5 record ABI must remain stable");

template <typename T>
class DeviceAllocation {
  public:
    DeviceAllocation() = default;
    ~DeviceAllocation() { reset(); }
    DeviceAllocation(const DeviceAllocation&) = delete;
    DeviceAllocation& operator=(const DeviceAllocation&) = delete;
    DeviceAllocation(DeviceAllocation&& other) noexcept : pointer_(other.pointer_) {
        other.pointer_ = nullptr;
    }
    DeviceAllocation& operator=(DeviceAllocation&& other) noexcept {
        if (this != &other) {
            reset();
            pointer_ = other.pointer_;
            other.pointer_ = nullptr;
        }
        return *this;
    }
    cudaError_t allocate(std::size_t count) {
        if (count == 0U || count > std::numeric_limits<std::size_t>::max() /
                                     sizeof(T)) {
            return cudaErrorInvalidValue;
        }
        return cudaMalloc(reinterpret_cast<void**>(&pointer_), count * sizeof(T));
    }
    T* get() const noexcept { return pointer_; }
    void reset() noexcept {
        if (pointer_ != nullptr) (void)cudaFree(pointer_);
        pointer_ = nullptr;
    }
  private:
    T* pointer_ = nullptr;
};

std::string cuda_detail(const char* operation, cudaError_t error) {
    return std::string(operation) + ": " + cudaGetErrorName(error) + " (" +
           cudaGetErrorString(error) + ")";
}

bool product_fits(std::uint64_t a, std::uint64_t b, std::size_t& result) {
    if (a == 0U || b == 0U || a > std::numeric_limits<std::size_t>::max() / b) {
        return false;
    }
    result = static_cast<std::size_t>(a * b);
    return true;
}

InputOutcome validate_dataset(DatasetView dataset, std::size_t& elements) {
    if (dataset.candidate_count == 0U) return InputOutcome::invalid_N_zero;
    if (dataset.dimension == 0U) return InputOutcome::invalid_D_zero;
    if (dataset.data == nullptr || !product_fits(dataset.candidate_count,
                                                 dataset.dimension, elements) ||
        dataset.element_count != elements) {
        return InputOutcome::not_applicable;
    }
    for (std::size_t i = 0; i < elements; ++i) {
        if (!std::isfinite(dataset.data[i])) return InputOutcome::invalid_nonfinite;
    }
    return InputOutcome::valid;
}

InputOutcome validate_queries(QueryBatchView queries, std::uint64_t dimension,
                              std::size_t& elements) {
    if (queries.query_count == 0U) return InputOutcome::not_applicable;
    if (queries.dimension == 0U) {
        return InputOutcome::invalid_D_zero;
    }
    if (queries.data == nullptr || queries.dimension != dimension ||
        !product_fits(queries.query_count, queries.dimension, elements) ||
        queries.element_count != elements) {
        return InputOutcome::not_applicable;
    }
    for (std::size_t i = 0; i < elements; ++i) {
        if (!std::isfinite(queries.data[i])) return InputOutcome::invalid_nonfinite;
    }
    return InputOutcome::valid;
}

__device__ V5Record choose(V5Record a, V5Record b) {
    if ((a.flags & kPresent) == 0U) return b;
    if ((b.flags & kPresent) == 0U) return a;
    a.flags |= b.flags & kNonfinite;
    if ((b.flags & kNonfinite) != 0U) return a;
    if ((a.flags & kNonfinite) != 0U || b.distance < a.distance ||
        (b.distance == a.distance && b.index < a.index)) {
        const unsigned int combined = a.flags | b.flags;
        a = b;
        a.flags |= combined & kNonfinite;
    }
    return a;
}

template <unsigned int QueryTile>
__global__ void v5_distance_tile_kernel(const float* dataset_soa,
                                         const float* queries,
                                         std::uint64_t candidate_count,
                                         std::uint64_t dimension,
                                         std::uint64_t query_count,
                                         V5Record* partials) {
    extern __shared__ V5Record shared[];
    const std::uint64_t candidate =
        static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const std::uint64_t query_base =
        static_cast<std::uint64_t>(blockIdx.y) * QueryTile;
    for (unsigned int local_q = 0; local_q < QueryTile; ++local_q) {
        const std::uint64_t query_index = query_base + local_q;
        V5Record value{0.0F, 0U, candidate};
        if (candidate < candidate_count && query_index < query_count) {
            value.flags = kPresent;
            float sum = 0.0F;
            for (std::uint64_t j = 0; j < dimension; ++j) {
                const float difference = dataset_soa[j * candidate_count + candidate] -
                                         queries[query_index * dimension + j];
                sum = fmaf(difference, difference, sum);
            }
            value.distance = sum;
            if (!isfinite(sum)) value.flags |= kNonfinite;
        }
        // Tile members are intentionally evaluated sequentially so every
        // approved {query tile, block size} pair stays within the ordinary
        // one-record-per-thread shared-memory requirement.
        V5Record* tile_shared = shared;
        tile_shared[threadIdx.x] = value;
        __syncthreads();
        for (unsigned int stride = blockDim.x / 2U; stride > 0U; stride >>= 1U) {
            if (threadIdx.x < stride) {
                tile_shared[threadIdx.x] =
                    choose(tile_shared[threadIdx.x], tile_shared[threadIdx.x + stride]);
            }
            __syncthreads();
        }
        if (threadIdx.x == 0U && query_index < query_count) {
            partials[query_index * gridDim.x + blockIdx.x] = tile_shared[0];
        }
        __syncthreads();
    }
}

__global__ void v5_final_reduce_kernel(const V5Record* partials,
                                       std::uint64_t partial_count,
                                       V5Record* results) {
    extern __shared__ V5Record shared[];
    const std::uint64_t query_index = blockIdx.x;
    V5Record value{0.0F, 0U, 0U};
    for (std::uint64_t i = threadIdx.x; i < partial_count; i += blockDim.x) {
        value = choose(value, partials[query_index * partial_count + i]);
    }
    shared[threadIdx.x] = value;
    __syncthreads();
    for (unsigned int stride = blockDim.x / 2U; stride > 0U; stride >>= 1U) {
        if (threadIdx.x < stride) {
            shared[threadIdx.x] = choose(shared[threadIdx.x], shared[threadIdx.x + stride]);
        }
        __syncthreads();
    }
    if (threadIdx.x == 0U) results[query_index] = shared[0];
}

cudaError_t launch_distance(const CudaV5Options& options, const float* dataset,
                            const float* queries, std::uint64_t n,
                            std::uint64_t d, std::uint64_t q,
                            std::uint64_t partial_count, V5Record* partials) {
    const dim3 grid(static_cast<unsigned int>(partial_count),
                    static_cast<unsigned int>((q + options.query_tile - 1U) /
                                              options.query_tile));
    const std::size_t shared = static_cast<std::size_t>(options.threads_per_block) *
                               sizeof(V5Record);
    switch (options.query_tile) {
        case 1U:
            v5_distance_tile_kernel<1U><<<grid, options.threads_per_block, shared,
                                           options.stream>>>(dataset, queries, n, d, q,
                                                            partials);
            break;
        case 4U:
            v5_distance_tile_kernel<4U><<<grid, options.threads_per_block, shared,
                                           options.stream>>>(dataset, queries, n, d, q,
                                                            partials);
            break;
        case 8U:
            v5_distance_tile_kernel<8U><<<grid, options.threads_per_block, shared,
                                           options.stream>>>(dataset, queries, n, d, q,
                                                            partials);
            break;
        default: return cudaErrorInvalidConfiguration;
    }
    return cudaPeekAtLastError();
}

}  // namespace

namespace detail {
struct CudaV5PreparedDatasetState {
    int device_ordinal = 0;
    std::uint64_t candidate_count = 0;
    std::uint64_t dimension = 0;
    DomainStatus domain_status = DomainStatus::not_applicable;
    DeviceAllocation<float> dataset_soa;
    ~CudaV5PreparedDatasetState() { (void)cudaSetDevice(device_ordinal); }
};

struct CudaV5WorkspaceState {
    int device_ordinal = 0;
    std::uint64_t candidate_count = 0;
    std::uint64_t dimension = 0;
    std::uint64_t capacity = 0;
    std::uint64_t partial_count = 0;
    std::uint32_t threads_per_block = 0;
    std::uint32_t query_tile = 0;
    DeviceAllocation<float> queries;
    DeviceAllocation<V5Record> partials;
    DeviceAllocation<V5Record> results;
    std::atomic_flag in_use = ATOMIC_FLAG_INIT;
    std::atomic<bool> disabled{false};
    ~CudaV5WorkspaceState() { (void)cudaSetDevice(device_ordinal); }
};

struct CudaV5SearchAccess {
    static BatchProductionOutcome run(const CudaV5PreparedDataset& dataset,
                                      QueryBatchView queries,
                                      CudaV5Workspace& workspace,
                                      const CudaV5Options& options,
                                      CudaV5InjectedFailure injected_failure,
                                      CudaV5BenchmarkOutcome* benchmark = nullptr);
};
}  // namespace detail

CudaV5PreparedDataset::CudaV5PreparedDataset() noexcept = default;
CudaV5PreparedDataset::~CudaV5PreparedDataset() = default;
CudaV5PreparedDataset::CudaV5PreparedDataset(CudaV5PreparedDataset&&) noexcept = default;
CudaV5PreparedDataset& CudaV5PreparedDataset::operator=(CudaV5PreparedDataset&&) noexcept = default;
bool CudaV5PreparedDataset::valid() const noexcept { return state_ != nullptr; }
std::uint64_t CudaV5PreparedDataset::candidate_count() const noexcept {
    return state_ ? state_->candidate_count : 0U;
}
std::uint64_t CudaV5PreparedDataset::dimension() const noexcept {
    return state_ ? state_->dimension : 0U;
}
int CudaV5PreparedDataset::device_ordinal() const noexcept {
    return state_ ? state_->device_ordinal : -1;
}

CudaV5Workspace::CudaV5Workspace() noexcept = default;
CudaV5Workspace::~CudaV5Workspace() = default;
CudaV5Workspace::CudaV5Workspace(CudaV5Workspace&&) noexcept = default;
CudaV5Workspace& CudaV5Workspace::operator=(CudaV5Workspace&&) noexcept = default;
bool CudaV5Workspace::valid() const noexcept { return state_ != nullptr; }
std::uint64_t CudaV5Workspace::capacity() const noexcept {
    return state_ ? state_->capacity : 0U;
}

CudaV5PreparedDatasetOutcome cuda_v5_prepare_dataset_float32(
    DatasetView dataset, const CudaV5Options& options) {
    CudaV5PreparedDatasetOutcome outcome;
    std::size_t elements = 0;
    outcome.input_outcome = validate_dataset(dataset, elements);
    if (outcome.input_outcome != InputOutcome::valid) {
        outcome.invocation_status = outcome.input_outcome == InputOutcome::not_applicable
                                        ? InvocationStatus::api_misuse
                                        : InvocationStatus::contract_invalid;
        return outcome;
    }
    if (!cuda_v5_is_supported_block_size(options.threads_per_block) ||
        !cuda_v5_is_supported_query_tile(options.query_tile)) {
        outcome.invocation_status = InvocationStatus::api_misuse;
        outcome.backend_detail = "unsupported V5 block size or query tile";
        return outcome;
    }
    std::vector<float> transposed;
    try {
        transposed.resize(elements);
    } catch (const std::bad_alloc&) {
        outcome.invocation_status = InvocationStatus::resource_failure;
        outcome.backend_detail = "V5 host transpose allocation failed";
        return outcome;
    }
    for (std::uint64_t i = 0; i < dataset.candidate_count; ++i) {
        for (std::uint64_t j = 0; j < dataset.dimension; ++j) {
            transposed[static_cast<std::size_t>(j * dataset.candidate_count + i)] =
                dataset.data[static_cast<std::size_t>(i * dataset.dimension + j)];
        }
    }
    cudaError_t error = cudaSetDevice(options.device_ordinal);
    std::unique_ptr<detail::CudaV5PreparedDatasetState> state;
    try {
        state = std::make_unique<detail::CudaV5PreparedDatasetState>();
    } catch (const std::bad_alloc&) {
        outcome.invocation_status = InvocationStatus::resource_failure;
        outcome.backend_detail = "V5 prepared state allocation failed";
        return outcome;
    }
    state->device_ordinal = options.device_ordinal;
    state->candidate_count = dataset.candidate_count;
    state->dimension = dataset.dimension;
    state->domain_status = dataset.dimension < (std::uint64_t{1} << 24U)
                               ? DomainStatus::core_eligible
                               : DomainStatus::valid_outside_approved_domain;
    if (error == cudaSuccess) error = state->dataset_soa.allocate(elements);
    if (error == cudaSuccess) {
        error = cudaMemcpyAsync(state->dataset_soa.get(), transposed.data(),
                                elements * sizeof(float), cudaMemcpyHostToDevice,
                                options.stream);
    }
    if (error == cudaSuccess) error = cudaStreamSynchronize(options.stream);
    if (error != cudaSuccess) {
        outcome.invocation_status = InvocationStatus::backend_failure;
        outcome.backend_detail = cuda_detail("V5 dataset preparation", error);
        return outcome;
    }
    CudaV5PreparedDataset prepared;
    prepared.state_ = std::move(state);
    outcome.prepared_dataset.emplace(std::move(prepared));
    outcome.invocation_status = InvocationStatus::completed;
    outcome.domain_status = outcome.prepared_dataset->state_->domain_status;
    return outcome;
}

CudaV5WorkspaceOutcome cuda_v5_create_workspace(
    const CudaV5PreparedDataset& dataset, std::uint64_t maximum_query_count,
    const CudaV5Options& options) {
    CudaV5WorkspaceOutcome outcome;
    if (!dataset.state_ || maximum_query_count == 0U ||
        dataset.state_->device_ordinal != options.device_ordinal ||
        !cuda_v5_is_supported_block_size(options.threads_per_block) ||
        !cuda_v5_is_supported_query_tile(options.query_tile)) {
        outcome.invocation_status = InvocationStatus::api_misuse;
        outcome.backend_detail = "invalid V5 workspace arguments";
        return outcome;
    }
    const std::uint64_t partial_count =
        (dataset.state_->candidate_count + options.threads_per_block - 1U) /
        options.threads_per_block;
    std::size_t query_elements = 0;
    std::size_t partial_elements = 0;
    if (!product_fits(maximum_query_count, dataset.state_->dimension, query_elements) ||
        !product_fits(maximum_query_count, partial_count, partial_elements) ||
        partial_count > std::numeric_limits<unsigned int>::max() ||
        (maximum_query_count + options.query_tile - 1U) / options.query_tile > 65535U) {
        outcome.invocation_status = InvocationStatus::api_misuse;
        outcome.backend_detail = "V5 workspace size is unsupported";
        return outcome;
    }
    cudaError_t error = cudaSetDevice(options.device_ordinal);
    if (error != cudaSuccess) {
        outcome.invocation_status = InvocationStatus::backend_failure;
        outcome.backend_detail = cuda_detail("cudaSetDevice", error);
        return outcome;
    }
    std::unique_ptr<detail::CudaV5WorkspaceState> state;
    try {
        state = std::make_unique<detail::CudaV5WorkspaceState>();
    } catch (const std::bad_alloc&) {
        outcome.invocation_status = InvocationStatus::resource_failure;
        outcome.backend_detail = "V5 workspace state allocation failed";
        return outcome;
    }
    state->device_ordinal = options.device_ordinal;
    state->candidate_count = dataset.state_->candidate_count;
    state->dimension = dataset.state_->dimension;
    state->capacity = maximum_query_count;
    state->partial_count = partial_count;
    state->threads_per_block = options.threads_per_block;
    state->query_tile = options.query_tile;
    error = state->queries.allocate(query_elements);
    if (error == cudaSuccess) error = state->partials.allocate(partial_elements);
    if (error == cudaSuccess) error = state->results.allocate(maximum_query_count);
    if (error != cudaSuccess) {
        outcome.invocation_status = InvocationStatus::resource_failure;
        outcome.backend_detail = cuda_detail("V5 workspace allocation", error);
        return outcome;
    }
    CudaV5Workspace workspace;
    workspace.state_ = std::move(state);
    outcome.workspace.emplace(std::move(workspace));
    outcome.invocation_status = InvocationStatus::completed;
    return outcome;
}

BatchProductionOutcome detail::CudaV5SearchAccess::run(
    const CudaV5PreparedDataset& dataset, QueryBatchView queries,
    CudaV5Workspace& workspace, const CudaV5Options& options,
    CudaV5InjectedFailure injected_failure,
    CudaV5BenchmarkOutcome* benchmark) {
    BatchProductionOutcome outcome;
    std::size_t query_elements = 0;
    if (!dataset.state_ || !workspace.state_) {
        outcome.invocation_status = InvocationStatus::api_misuse;
        outcome.backend_detail = "invalid V5 prepared dataset or workspace";
        return outcome;
    }
    outcome.input_outcome = validate_queries(queries, dataset.state_->dimension,
                                             query_elements);
    if (outcome.input_outcome != InputOutcome::valid) {
        outcome.invocation_status = outcome.input_outcome == InputOutcome::not_applicable
                                        ? InvocationStatus::api_misuse
                                        : InvocationStatus::contract_invalid;
        return outcome;
    }
    auto& state = *workspace.state_;
    if (queries.query_count > state.capacity || state.disabled.load() ||
        state.device_ordinal != options.device_ordinal ||
        dataset.state_->device_ordinal != options.device_ordinal ||
        state.candidate_count != dataset.state_->candidate_count ||
        state.dimension != dataset.state_->dimension ||
        state.threads_per_block != options.threads_per_block ||
        state.query_tile != options.query_tile) {
        outcome.invocation_status = InvocationStatus::api_misuse;
        outcome.backend_detail = "V5 workspace is incompatible or disabled";
        return outcome;
    }
    if (state.in_use.test_and_set(std::memory_order_acquire)) {
        outcome.invocation_status = InvocationStatus::api_misuse;
        outcome.backend_detail = "V5 workspace is already in use";
        return outcome;
    }
    struct Release { std::atomic_flag& flag; ~Release(){ flag.clear(std::memory_order_release); } } release{state.in_use};
    if (injected_failure == CudaV5InjectedFailure::resource_allocation) {
        outcome.invocation_status = InvocationStatus::resource_failure;
        outcome.backend_detail = "injected V5 resource failure";
        return outcome;
    }
    cudaEvent_t kernel_start = nullptr;
    cudaEvent_t kernel_stop = nullptr;
    cudaError_t error = cudaSetDevice(options.device_ordinal);
    if (benchmark != nullptr && error == cudaSuccess) {
        error = cudaEventCreateWithFlags(&kernel_start, cudaEventDefault);
        if (error == cudaSuccess) {
            error = cudaEventCreateWithFlags(&kernel_stop, cudaEventDefault);
        }
        if (error != cudaSuccess) {
            if (kernel_start != nullptr) (void)cudaEventDestroy(kernel_start);
            if (kernel_stop != nullptr) (void)cudaEventDestroy(kernel_stop);
            kernel_start = nullptr;
            kernel_stop = nullptr;
            benchmark->timing_detail = cuda_detail("V5 benchmark event creation", error);
        }
    }
    if (error == cudaSuccess) {
        error = cudaMemcpyAsync(state.queries.get(), queries.data,
                                query_elements * sizeof(float), cudaMemcpyHostToDevice,
                                options.stream);
    }
    if (injected_failure == CudaV5InjectedFailure::backend_copy) error = cudaErrorUnknown;
    if (error == cudaSuccess && benchmark != nullptr) {
        error = cudaEventRecord(kernel_start, options.stream);
    }
    if (error == cudaSuccess) {
        error = launch_distance(options, dataset.state_->dataset_soa.get(),
                                state.queries.get(), state.candidate_count,
                                state.dimension, queries.query_count,
                                state.partial_count, state.partials.get());
    }
    if (injected_failure == CudaV5InjectedFailure::backend_launch) error = cudaErrorUnknown;
    if (error == cudaSuccess) {
        v5_final_reduce_kernel<<<static_cast<unsigned int>(queries.query_count),
                                 options.threads_per_block,
                                 options.threads_per_block * sizeof(V5Record),
                                 options.stream>>>(state.partials.get(),
                                                  state.partial_count,
                                                  state.results.get());
        error = cudaPeekAtLastError();
    }
    if (error == cudaSuccess && benchmark != nullptr) {
        error = cudaEventRecord(kernel_stop, options.stream);
    }
    std::vector<V5Record> host_results;
    try { host_results.resize(static_cast<std::size_t>(queries.query_count)); }
    catch (const std::bad_alloc&) {
        (void)cudaStreamSynchronize(options.stream);
        if (kernel_start != nullptr) (void)cudaEventDestroy(kernel_start);
        if (kernel_stop != nullptr) (void)cudaEventDestroy(kernel_stop);
        outcome.invocation_status = InvocationStatus::resource_failure;
        outcome.backend_detail = "V5 host result allocation failed";
        return outcome;
    }
    if (error == cudaSuccess) {
        error = cudaMemcpyAsync(host_results.data(), state.results.get(),
                                host_results.size() * sizeof(V5Record),
                                cudaMemcpyDeviceToHost, options.stream);
    }
    if (injected_failure == CudaV5InjectedFailure::backend_synchronize) error = cudaErrorUnknown;
    if (error == cudaSuccess) error = cudaStreamSynchronize(options.stream);
    if (error == cudaSuccess && benchmark != nullptr) {
        float kernel_ms = 0.0F;
        error = cudaEventElapsedTime(&kernel_ms, kernel_start, kernel_stop);
        if (error == cudaSuccess) {
            benchmark->kernel_ns = static_cast<std::uint64_t>(
                static_cast<double>(kernel_ms) * 1000000.0);
            benchmark->device_timing_available = benchmark->kernel_ns > 0U;
            if (!benchmark->device_timing_available) {
                benchmark->timing_detail = "V5 CUDA-event kernel interval rounded to zero";
            }
        } else {
            benchmark->timing_detail = cuda_detail("cudaEventElapsedTime", error);
        }
    }
    if (kernel_start != nullptr) (void)cudaEventDestroy(kernel_start);
    if (kernel_stop != nullptr) (void)cudaEventDestroy(kernel_stop);
    if (error != cudaSuccess) {
        const cudaError_t drain = cudaStreamSynchronize(options.stream);
        if (drain != cudaSuccess || injected_failure == CudaV5InjectedFailure::backend_synchronize) {
            state.disabled.store(true);
        }
        outcome.invocation_status = InvocationStatus::backend_failure;
        outcome.domain_status = DomainStatus::unknown;
        outcome.backend_detail = cuda_detail("V5 batch execution", error);
        return outcome;
    }
    try {
        outcome.results.reserve(host_results.size());
    } catch (const std::bad_alloc&) {
        outcome.invocation_status = InvocationStatus::resource_failure;
        outcome.domain_status = DomainStatus::unknown;
        outcome.backend_detail = "V5 host outcome allocation failed";
        return outcome;
    }
    for (const auto& record : host_results) {
        if ((record.flags & kPresent) == 0U) {
            outcome.invocation_status = InvocationStatus::backend_failure;
            outcome.backend_detail = "V5 reduction lost logical presence";
            outcome.results.clear();
            return outcome;
        }
        if ((record.flags & kNonfinite) != 0U) {
            outcome.invocation_status = InvocationStatus::backend_failure;
            outcome.domain_status = DomainStatus::valid_outside_approved_domain;
            outcome.backend_detail = "nonfinite V5 candidate arithmetic";
            outcome.results.clear();
            return outcome;
        }
        outcome.results.push_back({record.index, record.distance});
    }
    outcome.invocation_status = InvocationStatus::completed;
    outcome.domain_status = dataset.state_->domain_status;
    return outcome;
}

BatchProductionOutcome cuda_v5_search_prepared_batch_float32(
    const CudaV5PreparedDataset& dataset, QueryBatchView queries,
    CudaV5Workspace& workspace, const CudaV5Options& options) {
    return detail::CudaV5SearchAccess::run(dataset, queries, workspace, options,
                                           detail::CudaV5InjectedFailure::none);
}

BatchProductionOutcome cuda_v5_search_batch_float32(
    DatasetView dataset, QueryBatchView queries, const CudaV5Options& options) {
    std::size_t dataset_elements = 0;
    const InputOutcome dataset_input = validate_dataset(dataset, dataset_elements);
    if (dataset_input != InputOutcome::valid) {
        BatchProductionOutcome result;
        result.input_outcome = dataset_input;
        result.invocation_status = dataset_input == InputOutcome::not_applicable
                                       ? InvocationStatus::api_misuse
                                       : InvocationStatus::contract_invalid;
        return result;
    }
    std::size_t query_elements = 0;
    const InputOutcome query_input =
        validate_queries(queries, dataset.dimension, query_elements);
    if (query_input != InputOutcome::valid) {
        BatchProductionOutcome result;
        result.input_outcome = query_input;
        result.invocation_status = query_input == InputOutcome::not_applicable
                                       ? InvocationStatus::api_misuse
                                       : InvocationStatus::contract_invalid;
        return result;
    }
    auto preparation = cuda_v5_prepare_dataset_float32(dataset, options);
    if (!preparation.prepared_dataset) {
        BatchProductionOutcome result;
        result.invocation_status = preparation.invocation_status;
        result.input_outcome = preparation.input_outcome;
        result.domain_status = preparation.domain_status;
        result.backend_detail = preparation.backend_detail;
        return result;
    }
    auto workspace = cuda_v5_create_workspace(*preparation.prepared_dataset,
                                               queries.query_count, options);
    if (!workspace.workspace) {
        BatchProductionOutcome result;
        result.invocation_status = workspace.invocation_status;
        result.backend_detail = workspace.backend_detail;
        return result;
    }
    return cuda_v5_search_prepared_batch_float32(*preparation.prepared_dataset,
                                                  queries, *workspace.workspace,
                                                  options);
}

namespace detail {
CudaV5BenchmarkOutcome cuda_v5_search_prepared_batch_float32_for_benchmark(
    const CudaV5PreparedDataset& dataset, QueryBatchView queries,
    CudaV5Workspace& workspace, const CudaV5Options& options) {
    CudaV5BenchmarkOutcome result;
    result.production = CudaV5SearchAccess::run(
        dataset, queries, workspace, options, CudaV5InjectedFailure::none,
        &result);
    return result;
}

BatchProductionOutcome cuda_v5_search_prepared_batch_float32_for_testing(
    const CudaV5PreparedDataset& dataset, QueryBatchView queries,
    CudaV5Workspace& workspace, const CudaV5Options& options,
    CudaV5InjectedFailure injected_failure) {
    return CudaV5SearchAccess::run(dataset, queries, workspace, options,
                                   injected_failure);
}
}  // namespace detail
}  // namespace s6
