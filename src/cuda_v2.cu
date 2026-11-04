#include "s6/cuda_v2.hpp"
#include "s6/benchmark_internal.hpp"
#include "s7_benchmark_cuda_internal.cuh"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace s6 {
namespace {

constexpr std::uint64_t kMaximumPolicyDimension = (std::uint64_t{1} << 24) - 1U;
constexpr std::uint32_t kRecordPresent = 1U << 0U;
constexpr std::uint32_t kRecordFiniteMinimum = 1U << 1U;
constexpr std::uint32_t kRecordNonfinite = 1U << 2U;
constexpr std::uint32_t kRecordDomainFailure = 1U << 3U;
constexpr std::uint32_t kRecordStatusMask =
    kRecordPresent | kRecordNonfinite | kRecordDomainFailure;

struct alignas(16) CudaV2ReductionRecord {
    float distance;
    std::uint32_t flags;
    std::uint64_t index;
};

static_assert(sizeof(CudaV2ReductionRecord) == 16U,
              "S6 V2 reduction record must be exactly 16 bytes");
static_assert(alignof(CudaV2ReductionRecord) == 16U,
              "S6 V2 reduction record must have 16-byte alignment");

struct InputCheck {
    InvocationStatus invocation = InvocationStatus::not_executed;
    InputOutcome input = InputOutcome::not_applicable;
    DomainStatus domain = DomainStatus::not_applicable;
    std::string detail;
    std::size_t dataset_bytes = 0;
    std::size_t query_bytes = 0;
    std::size_t distance_bytes = 0;
};

bool supported_block_size(std::uint32_t value) {
    return value == 128U || value == 256U || value == 512U;
}

std::uint64_t ceiling_divide(std::uint64_t count, std::uint32_t divisor) {
    return (count - 1U) / divisor + 1U;
}

InputCheck inspect_input(DatasetView dataset, QueryView query) {
    if (dataset.candidate_count == 0U) {
        return {InvocationStatus::contract_invalid, InputOutcome::invalid_N_zero,
                DomainStatus::not_applicable, "N is zero"};
    }
    if (dataset.dimension == 0U) {
        return {InvocationStatus::contract_invalid, InputOutcome::invalid_D_zero,
                DomainStatus::not_applicable, "D is zero"};
    }
    if (query.dimension != dataset.dimension) {
        return {InvocationStatus::api_misuse, InputOutcome::not_applicable,
                DomainStatus::not_applicable,
                "query and dataset dimensions differ"};
    }
    if (dataset.data == nullptr || query.data == nullptr) {
        return {InvocationStatus::api_misuse, InputOutcome::not_applicable,
                DomainStatus::not_applicable, "null data view"};
    }
    if (dataset.dimension >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return {InvocationStatus::api_misuse, InputOutcome::not_applicable,
                DomainStatus::not_applicable, "dimension is not addressable"};
    }
    const auto dimension = static_cast<std::size_t>(dataset.dimension);
    if (dataset.candidate_count >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() /
                                   dimension)) {
        return {InvocationStatus::api_misuse, InputOutcome::not_applicable,
                DomainStatus::not_applicable, "dataset element-count overflow"};
    }
    const auto expected = static_cast<std::size_t>(dataset.candidate_count) * dimension;
    if (dataset.element_count != expected || query.element_count != dimension) {
        return {InvocationStatus::api_misuse, InputOutcome::not_applicable,
                DomainStatus::not_applicable, "inconsistent view element count"};
    }
    if (expected > std::numeric_limits<std::size_t>::max() / sizeof(float) ||
        dimension > std::numeric_limits<std::size_t>::max() / sizeof(float) ||
        dataset.candidate_count >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() /
                                       sizeof(float))) {
        return {InvocationStatus::api_misuse, InputOutcome::not_applicable,
                DomainStatus::not_applicable, "CUDA byte-count overflow"};
    }
    for (std::size_t i = 0; i < dataset.element_count; ++i) {
        if (!std::isfinite(dataset.data[i])) {
            return {InvocationStatus::contract_invalid,
                    InputOutcome::invalid_nonfinite,
                    DomainStatus::not_applicable,
                    "dataset contains NaN or infinity"};
        }
    }
    for (std::size_t j = 0; j < query.element_count; ++j) {
        if (!std::isfinite(query.data[j])) {
            return {InvocationStatus::contract_invalid,
                    InputOutcome::invalid_nonfinite,
                    DomainStatus::not_applicable,
                    "query contains NaN or infinity"};
        }
    }
    InputCheck result;
    result.invocation = InvocationStatus::completed;
    result.input = InputOutcome::valid;
    result.domain = dataset.dimension <= kMaximumPolicyDimension
                        ? DomainStatus::core_eligible
                        : DomainStatus::valid_outside_approved_domain;
    result.dataset_bytes = expected * sizeof(float);
    result.query_bytes = dimension * sizeof(float);
    result.distance_bytes = static_cast<std::size_t>(dataset.candidate_count) *
                            sizeof(float);
    return result;
}

std::string cuda_detail(const char* operation, cudaError_t error) {
    std::ostringstream stream;
    stream << operation << ": " << cudaGetErrorName(error) << " ("
           << cudaGetErrorString(error) << ')';
    return stream.str();
}

template <typename T>
class DeviceAllocation {
  public:
    DeviceAllocation() = default;
    DeviceAllocation(const DeviceAllocation&) = delete;
    DeviceAllocation& operator=(const DeviceAllocation&) = delete;
    ~DeviceAllocation() { reset(); }

    cudaError_t allocate(std::size_t count) noexcept {
        reset();
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            return cudaErrorInvalidValue;
        }
        return cudaMalloc(reinterpret_cast<void**>(&pointer_), count * sizeof(T));
    }
    void reset() noexcept {
        if (pointer_ != nullptr) {
            (void)cudaFree(pointer_);
            pointer_ = nullptr;
        }
    }
    T* get() const noexcept { return pointer_; }

  private:
    T* pointer_ = nullptr;
};

template <std::size_t TileN, std::size_t TileD>
void transform_aos_to_soa_blocked(DatasetView dataset, std::vector<float>& soa) {
    static_assert(TileN > 0U && TileD > 0U, "transpose tiles must be nonzero");
    const auto n = static_cast<std::size_t>(dataset.candidate_count);
    const auto d = static_cast<std::size_t>(dataset.dimension);
    for (std::size_t i0 = 0; i0 < n;) {
        const std::size_t i_end = i0 + std::min(TileN, n - i0);
        for (std::size_t j0 = 0; j0 < d;) {
            const std::size_t j_end = j0 + std::min(TileD, d - j0);
            for (std::size_t i = i0; i < i_end; ++i) {
                const std::size_t aos_base = i * d;
                for (std::size_t j = j0; j < j_end; ++j) {
                    soa[j * n + i] = dataset.data[aos_base + j];
                }
            }
            j0 = j_end;
        }
        i0 = i_end;
    }
}

void transform_aos_to_soa_original(DatasetView dataset, std::vector<float>& soa) {
    const auto candidate_count = static_cast<std::size_t>(dataset.candidate_count);
    const auto dimension = static_cast<std::size_t>(dataset.dimension);
    for (std::size_t j = 0; j < dimension; ++j) {
        const std::size_t soa_base = j * candidate_count;
        for (std::size_t i = 0; i < candidate_count; ++i) {
            soa[soa_base + i] = dataset.data[i * dimension + j];
        }
    }
}

bool transform_aos_to_soa(
    DatasetView dataset, std::vector<float>& soa, std::string& detail,
    detail::S8TransposeImplementation implementation,
    detail::S7CudaStageTiming* benchmark_timing) {
    if (dataset.element_count > soa.max_size()) {
        detail = "SoA host element count exceeds vector capacity";
        return false;
    }
    try {
        soa.resize(dataset.element_count);
    } catch (const std::bad_alloc&) {
        detail = "SoA host allocation failed";
        return false;
    } catch (const std::length_error&) {
        detail = "SoA host element count exceeds vector capacity";
        return false;
    }
    const auto transpose_start = benchmark_timing != nullptr
        ? detail::benchmark_internal::Clock::now()
        : detail::benchmark_internal::Clock::time_point{};
    switch (implementation) {
        case detail::S8TransposeImplementation::original:
            transform_aos_to_soa_original(dataset, soa);
            break;
        case detail::S8TransposeImplementation::tile_32x32:
            transform_aos_to_soa_blocked<32U, 32U>(dataset, soa);
            break;
        case detail::S8TransposeImplementation::tile_64x32:
            transform_aos_to_soa_blocked<64U, 32U>(dataset, soa);
            break;
        case detail::S8TransposeImplementation::tile_64x64:
            transform_aos_to_soa_blocked<64U, 64U>(dataset, soa);
            break;
    }
    if (benchmark_timing != nullptr) {
        benchmark_timing->transpose_ns = detail::benchmark_internal::elapsed_ns(
            transpose_start, detail::benchmark_internal::Clock::now());
    }
    return true;
}

__device__ __forceinline__ CudaV2ReductionRecord combine_records(
    const CudaV2ReductionRecord left, const CudaV2ReductionRecord right) {
    CudaV2ReductionRecord result{};
    result.flags = (left.flags | right.flags) & kRecordStatusMask;
    const bool left_finite = (left.flags & kRecordFiniteMinimum) != 0U;
    const bool right_finite = (right.flags & kRecordFiniteMinimum) != 0U;
    if (!left_finite && !right_finite) {
        return result;
    }
    CudaV2ReductionRecord selected = left_finite ? left : right;
    if (left_finite && right_finite) {
        if (right.distance < left.distance ||
            (right.distance == left.distance && right.index < left.index) ||
            (right.distance == 0.0F && left.distance == 0.0F &&
             right.index == left.index &&
             (__float_as_uint(right.distance) & 0x80000000U) == 0U &&
             (__float_as_uint(left.distance) & 0x80000000U) != 0U)) {
            selected = right;
        }
    }
    result.distance = selected.distance;
    result.index = selected.index;
    result.flags |= kRecordFiniteMinimum;
    return result;
}

template <unsigned int BlockSize>
__global__ void cuda_v2_distance_block_reduce_kernel(
    const float* dataset, const float* query, std::uint64_t candidate_count,
    std::uint64_t dimension, CudaV2ReductionRecord* block_records,
    float* diagnostic_distances, std::uint64_t* diagnostic_mapping) {
    __shared__ CudaV2ReductionRecord shared[BlockSize];
    const unsigned int lane = threadIdx.x;
    const std::uint64_t candidate =
        static_cast<std::uint64_t>(blockIdx.x) * BlockSize + lane;
    CudaV2ReductionRecord record{};
    if (candidate < candidate_count) {
        float accumulator = 0.0F;
#pragma unroll 1
        for (std::uint64_t j = 0; j < dimension; ++j) {
            const float difference =
                __fsub_rn(dataset[j * candidate_count + candidate], query[j]);
            accumulator = __fmaf_rn(difference, difference, accumulator);
        }
        record.flags = kRecordPresent;
        record.index = candidate;
        if (isfinite(accumulator)) {
            record.distance = accumulator;
            record.flags |= kRecordFiniteMinimum;
        } else {
            record.flags |= kRecordNonfinite | kRecordDomainFailure;
        }
        if (diagnostic_distances != nullptr) {
            diagnostic_distances[candidate] = accumulator;
        }
        if (diagnostic_mapping != nullptr) {
            diagnostic_mapping[candidate] = candidate;
        }
    }
    shared[lane] = record;
    __syncthreads();
    for (unsigned int stride = BlockSize / 2U; stride > 0U; stride /= 2U) {
        if (lane < stride) {
            shared[lane] = combine_records(shared[lane], shared[lane + stride]);
        }
        __syncthreads();
    }
    if (lane == 0U) {
        block_records[blockIdx.x] = shared[0];
    }
}

template <unsigned int BlockSize>
__global__ void cuda_v2_record_block_reduce_kernel(
    const CudaV2ReductionRecord* input, std::uint64_t input_count,
    CudaV2ReductionRecord* output) {
    __shared__ CudaV2ReductionRecord shared[BlockSize];
    const unsigned int lane = threadIdx.x;
    const std::uint64_t position =
        static_cast<std::uint64_t>(blockIdx.x) * BlockSize + lane;
    shared[lane] = position < input_count ? input[position]
                                          : CudaV2ReductionRecord{};
    __syncthreads();
    for (unsigned int stride = BlockSize / 2U; stride > 0U; stride /= 2U) {
        if (lane < stride) {
            shared[lane] = combine_records(shared[lane], shared[lane + stride]);
        }
        __syncthreads();
    }
    if (lane == 0U) {
        output[blockIdx.x] = shared[0];
    }
}

__global__ void cuda_v2_abi_probe_kernel(std::uint64_t* output) {
    if (blockIdx.x == 0U && threadIdx.x == 0U) {
        output[0] = static_cast<std::uint64_t>(sizeof(CudaV2ReductionRecord));
        output[1] = static_cast<std::uint64_t>(alignof(CudaV2ReductionRecord));
    }
}

cudaError_t launch_distance_stage(
    std::uint32_t block_size, unsigned int grid, cudaStream_t stream,
    const float* dataset, const float* query, std::uint64_t candidate_count,
    std::uint64_t dimension, CudaV2ReductionRecord* output,
    float* diagnostic_distances, std::uint64_t* diagnostic_mapping) {
    switch (block_size) {
        case 128U:
            cuda_v2_distance_block_reduce_kernel<128U><<<grid, 128U, 0U, stream>>>(
                dataset, query, candidate_count, dimension, output,
                diagnostic_distances, diagnostic_mapping);
            break;
        case 256U:
            cuda_v2_distance_block_reduce_kernel<256U><<<grid, 256U, 0U, stream>>>(
                dataset, query, candidate_count, dimension, output,
                diagnostic_distances, diagnostic_mapping);
            break;
        case 512U:
            cuda_v2_distance_block_reduce_kernel<512U><<<grid, 512U, 0U, stream>>>(
                dataset, query, candidate_count, dimension, output,
                diagnostic_distances, diagnostic_mapping);
            break;
        default:
            return cudaErrorInvalidConfiguration;
    }
    return cudaPeekAtLastError();
}

cudaError_t launch_record_stage(
    std::uint32_t block_size, unsigned int grid, cudaStream_t stream,
    const CudaV2ReductionRecord* input, std::uint64_t input_count,
    CudaV2ReductionRecord* output) {
    switch (block_size) {
        case 128U:
            cuda_v2_record_block_reduce_kernel<128U><<<grid, 128U, 0U, stream>>>(
                input, input_count, output);
            break;
        case 256U:
            cuda_v2_record_block_reduce_kernel<256U><<<grid, 256U, 0U, stream>>>(
                input, input_count, output);
            break;
        case 512U:
            cuda_v2_record_block_reduce_kernel<512U><<<grid, 512U, 0U, stream>>>(
                input, input_count, output);
            break;
        default:
            return cudaErrorInvalidConfiguration;
    }
    return cudaPeekAtLastError();
}

void apply_final_record(CudaV2EvidencedOutcome& result,
                        const CudaV2ReductionRecord& record) {
    if ((record.flags & kRecordPresent) == 0U) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "CUDA V2 reduction lost logical presence";
        return;
    }
    if ((record.flags & (kRecordNonfinite | kRecordDomainFailure)) != 0U) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::valid_outside_approved_domain;
        result.production.backend_detail =
            "nonfinite CUDA candidate arithmetic propagated through V2 hierarchy";
        return;
    }
    if ((record.flags & kRecordFiniteMinimum) == 0U) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail =
            "CUDA V2 reduction produced no finite minimum";
        return;
    }
    result.production.result = SearchResult{record.index, record.distance};
    result.production.invocation_status = InvocationStatus::completed;
    result.production.backend_detail.clear();
}

CudaV2EvidencedOutcome run_cuda_v2(
    DatasetView dataset, QueryView query, const CudaV2Options& options,
    bool collect_evidence, detail::CudaV2InjectedFailure injected_failure,
    detail::S7CudaStageTiming* benchmark_timing = nullptr,
    detail::S8TransposeImplementation transpose_implementation =
        detail::S8TransposeImplementation::tile_32x32) {
    CudaV2EvidencedOutcome result;
    const auto validation_start = benchmark_timing != nullptr
        ? detail::benchmark_internal::Clock::now()
        : detail::benchmark_internal::Clock::time_point{};
    const auto input = inspect_input(dataset, query);
    if (benchmark_timing != nullptr) {
        benchmark_timing->validation_ns = detail::benchmark_internal::elapsed_ns(
            validation_start, detail::benchmark_internal::Clock::now());
    }
    result.production.invocation_status = input.invocation;
    result.production.input_outcome = input.input;
    result.production.domain_status = input.domain;
    result.production.backend_detail = input.detail;
    result.evidence.expected_candidate_count = dataset.candidate_count;
    if (input.invocation != InvocationStatus::completed) {
        return result;
    }
    if (!supported_block_size(options.threads_per_block)) {
        result.production.invocation_status = InvocationStatus::api_misuse;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail =
            "CUDA V2 block size must be one of 128, 256, or 512";
        return result;
    }
    if (injected_failure == detail::CudaV2InjectedFailure::resource_allocation) {
        result.production.invocation_status = InvocationStatus::resource_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "injected CUDA allocation failure";
        return result;
    }

    int device_count = 0;
    cudaError_t error = cudaGetDeviceCount(&device_count);
    if (error != cudaSuccess) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = cuda_detail("cudaGetDeviceCount", error);
        return result;
    }
    if (options.device_ordinal < 0 || options.device_ordinal >= device_count) {
        result.production.invocation_status = InvocationStatus::api_misuse;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "CUDA device ordinal is unavailable";
        return result;
    }
    error = cudaSetDevice(options.device_ordinal);
    if (error != cudaSuccess) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = cuda_detail("cudaSetDevice", error);
        return result;
    }
    if (options.stream != nullptr) {
        int stream_device = -1;
        error = cudaStreamGetDevice(options.stream, &stream_device);
        if (error != cudaSuccess || stream_device != options.device_ordinal) {
            result.production.invocation_status = InvocationStatus::api_misuse;
            result.production.domain_status = DomainStatus::unknown;
            result.production.backend_detail =
                error == cudaSuccess
                    ? "CUDA stream belongs to an incompatible device"
                    : cuda_detail("invalid CUDA stream", error);
            return result;
        }
    }
    cudaDeviceProp properties{};
    error = cudaGetDeviceProperties(&properties, options.device_ordinal);
    if (error != cudaSuccess) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = cuda_detail("cudaGetDeviceProperties", error);
        return result;
    }
    if (properties.maxThreadsPerBlock <
        static_cast<int>(options.threads_per_block)) {
        result.production.invocation_status = InvocationStatus::api_misuse;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail =
            "device does not support requested CUDA V2 block size";
        return result;
    }
    const std::uint64_t first_count =
        ceiling_divide(dataset.candidate_count, options.threads_per_block);
    if (first_count > static_cast<std::uint64_t>(properties.maxGridSize[0]) ||
        first_count > static_cast<std::uint64_t>(
                          std::numeric_limits<unsigned int>::max()) ||
        first_count > static_cast<std::uint64_t>(
                          std::numeric_limits<std::size_t>::max())) {
        result.production.invocation_status = InvocationStatus::api_misuse;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "candidate count exceeds CUDA grid capacity";
        return result;
    }

    const auto conversion_start = benchmark_timing != nullptr
        ? detail::benchmark_internal::Clock::now()
        : detail::benchmark_internal::Clock::time_point{};
    std::vector<float> host_soa_dataset;
    std::string transform_detail;
    if (!transform_aos_to_soa(dataset, host_soa_dataset, transform_detail,
                              transpose_implementation, benchmark_timing)) {
        result.production.invocation_status = InvocationStatus::resource_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = std::move(transform_detail);
        return result;
    }
    if (benchmark_timing != nullptr) {
        benchmark_timing->conversion_ns = detail::benchmark_internal::elapsed_ns(
            conversion_start, detail::benchmark_internal::Clock::now());
    }

    const auto allocation_start = benchmark_timing != nullptr
        ? detail::benchmark_internal::Clock::now()
        : detail::benchmark_internal::Clock::time_point{};
    DeviceAllocation<float> device_dataset;
    DeviceAllocation<float> device_query;
    DeviceAllocation<CudaV2ReductionRecord> device_pairs_a;
    DeviceAllocation<CudaV2ReductionRecord> device_pairs_b;
    DeviceAllocation<float> device_distances;
    DeviceAllocation<std::uint64_t> device_mapping;
    const auto allocate = [&](auto& allocation, std::size_t count,
                              const char* name) -> bool {
        const cudaError_t allocation_error = allocation.allocate(count);
        if (allocation_error == cudaSuccess) {
            return true;
        }
        result.production.invocation_status =
            allocation_error == cudaErrorMemoryAllocation
                ? InvocationStatus::resource_failure
                : InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = cuda_detail(name, allocation_error);
        return false;
    };
    if (!allocate(device_dataset, dataset.element_count, "cudaMalloc(dataset)") ||
        !allocate(device_query, query.element_count, "cudaMalloc(query)") ||
        !allocate(device_pairs_a, static_cast<std::size_t>(first_count),
                  "cudaMalloc(reduction pairs A)") ||
        !allocate(device_pairs_b, static_cast<std::size_t>(first_count),
                  "cudaMalloc(reduction pairs B)") ||
        (collect_evidence &&
         (!allocate(device_distances,
                    static_cast<std::size_t>(dataset.candidate_count),
                    "cudaMalloc(diagnostic distances)") ||
          !allocate(device_mapping,
                    static_cast<std::size_t>(dataset.candidate_count),
                    "cudaMalloc(diagnostic mapping)")))) {
        return result;
    }
    if (benchmark_timing != nullptr) {
        benchmark_timing->allocation_ns = detail::benchmark_internal::elapsed_ns(
            allocation_start, detail::benchmark_internal::Clock::now());
    }
    detail::benchmark_internal::StageEvents stage_events(benchmark_timing);

    if (injected_failure == detail::CudaV2InjectedFailure::backend_copy) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "injected CUDA copy failure";
        return result;
    }
    stage_events.record(0U, options.stream);
    error = cudaMemcpyAsync(device_dataset.get(), host_soa_dataset.data(),
                            input.dataset_bytes, cudaMemcpyHostToDevice,
                            options.stream);
    if (error == cudaSuccess) {
        error = cudaMemcpyAsync(device_query.get(), query.data, input.query_bytes,
                                cudaMemcpyHostToDevice, options.stream);
    }
    stage_events.record(1U, options.stream);
    if (error == cudaSuccess && collect_evidence) {
        error = cudaMemsetAsync(device_distances.get(), 0xff, input.distance_bytes,
                                options.stream);
    }
    if (error == cudaSuccess && collect_evidence) {
        error = cudaMemsetAsync(
            device_mapping.get(), 0xff,
            static_cast<std::size_t>(dataset.candidate_count) *
                sizeof(std::uint64_t),
            options.stream);
    }
    if (error != cudaSuccess) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = cuda_detail("CUDA input transfer", error);
        return result;
    }
    stage_events.record(2U, options.stream);

    if (injected_failure == detail::CudaV2InjectedFailure::backend_launch) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "injected CUDA launch failure";
        return result;
    }
    (void)cudaGetLastError();
    error = launch_distance_stage(
        options.threads_per_block, static_cast<unsigned int>(first_count),
        options.stream, device_dataset.get(), device_query.get(),
        dataset.candidate_count, dataset.dimension, device_pairs_a.get(),
        collect_evidence ? device_distances.get() : nullptr,
        collect_evidence ? device_mapping.get() : nullptr);
    CudaV2ReductionRecord* current = device_pairs_a.get();
    CudaV2ReductionRecord* next = device_pairs_b.get();
    std::uint64_t current_count = first_count;
    while (error == cudaSuccess && current_count > 1U) {
        const std::uint64_t next_count =
            ceiling_divide(current_count, options.threads_per_block);
        error = launch_record_stage(
            options.threads_per_block, static_cast<unsigned int>(next_count),
            options.stream, current, current_count, next);
        std::swap(current, next);
        current_count = next_count;
    }
    if (error != cudaSuccess) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail =
            cuda_detail("CUDA V2 kernel launch", error);
        return result;
    }
    stage_events.record(3U, options.stream);

    CudaV2ReductionRecord host_record{};
    error = cudaMemcpyAsync(&host_record, current, sizeof(host_record),
                            cudaMemcpyDeviceToHost, options.stream);
    std::vector<std::uint64_t> mapping;
    if (error == cudaSuccess && collect_evidence) {
        try {
            result.evidence.candidate_distances.resize(
                static_cast<std::size_t>(dataset.candidate_count));
            mapping.resize(static_cast<std::size_t>(dataset.candidate_count));
        } catch (const std::bad_alloc&) {
            result.production.invocation_status = InvocationStatus::resource_failure;
            result.production.domain_status = DomainStatus::unknown;
            result.production.backend_detail = "host evidence allocation failure";
            return result;
        }
        error = cudaMemcpyAsync(result.evidence.candidate_distances.data(),
                                device_distances.get(), input.distance_bytes,
                                cudaMemcpyDeviceToHost, options.stream);
        if (error == cudaSuccess) {
            error = cudaMemcpyAsync(
                mapping.data(), device_mapping.get(),
                mapping.size() * sizeof(std::uint64_t), cudaMemcpyDeviceToHost,
                options.stream);
        }
    }
    if (error != cudaSuccess) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = cuda_detail("CUDA output transfer", error);
        return result;
    }
    stage_events.record(4U, options.stream);
    if (injected_failure == detail::CudaV2InjectedFailure::backend_synchronize) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "injected CUDA synchronization failure";
        return result;
    }
    const auto synchronize_start = benchmark_timing != nullptr
        ? detail::benchmark_internal::Clock::now()
        : detail::benchmark_internal::Clock::time_point{};
    error = cudaStreamSynchronize(options.stream);
    if (benchmark_timing != nullptr) {
        benchmark_timing->synchronize_ns = detail::benchmark_internal::elapsed_ns(
            synchronize_start, detail::benchmark_internal::Clock::now());
    }
    if (error != cudaSuccess) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail =
            cuda_detail("cudaStreamSynchronize", error);
        return result;
    }
    stage_events.finish();

    if (collect_evidence) {
        bool mapping_complete = true;
        bool overwrite_complete = true;
        for (std::uint64_t i = 0; i < dataset.candidate_count; ++i) {
            if (mapping[static_cast<std::size_t>(i)] != i) {
                mapping_complete = false;
            } else {
                ++result.evidence.observed_mapping_count;
            }
            if (std::isnan(
                    result.evidence.candidate_distances[static_cast<std::size_t>(i)])) {
                overwrite_complete = false;
            }
        }
        result.evidence.mapping_status = mapping_complete ? EvidenceStatus::passed
                                                          : EvidenceStatus::failed;
        result.evidence.overwrite_status = overwrite_complete ? EvidenceStatus::passed
                                                               : EvidenceStatus::failed;
        result.evidence.candidate_distance_status = EvidenceStatus::blocked;
        result.evidence.detail =
            "diagnostic per-candidate buffers are independent of production reduction";
    }
    apply_final_record(result, host_record);
    return result;
}

std::string format_uuid(const cudaUUID_t& uuid) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string value;
    value.reserve(32U);
    for (unsigned char byte : uuid.bytes) {
        value.push_back(digits[(byte >> 4U) & 0x0fU]);
        value.push_back(digits[byte & 0x0fU]);
    }
    return value;
}

}  // namespace

CudaV2DeviceInfo inspect_cuda_v2_device(int device_ordinal) {
    CudaV2DeviceInfo result;
    result.device_ordinal = device_ordinal;
    int device_count = 0;
    cudaError_t error = cudaGetDeviceCount(&device_count);
    if (error != cudaSuccess) {
        result.detail = cuda_detail("cudaGetDeviceCount", error);
        return result;
    }
    if (device_ordinal < 0 || device_ordinal >= device_count) {
        result.detail = "CUDA device ordinal is unavailable";
        return result;
    }
    cudaDeviceProp properties{};
    error = cudaGetDeviceProperties(&properties, device_ordinal);
    if (error != cudaSuccess) {
        result.detail = cuda_detail("cudaGetDeviceProperties", error);
        return result;
    }
    result.name = properties.name;
    result.uuid = format_uuid(properties.uuid);
    result.compute_major = properties.major;
    result.compute_minor = properties.minor;
    result.total_global_memory = properties.totalGlobalMem;
    result.maximum_threads_per_block = properties.maxThreadsPerBlock;
    result.maximum_grid_x = properties.maxGridSize[0];
    result.warp_size = properties.warpSize;
    (void)cudaDriverGetVersion(&result.driver_version);
    (void)cudaRuntimeGetVersion(&result.runtime_version);
    result.status = EvidenceStatus::passed;
    return result;
}

CudaV2AbiInfo inspect_cuda_v2_reduction_record_abi(
    const CudaV2Options& options) {
    CudaV2AbiInfo result;
    result.host_size = sizeof(CudaV2ReductionRecord);
    result.host_alignment = alignof(CudaV2ReductionRecord);
    cudaError_t error = cudaSetDevice(options.device_ordinal);
    DeviceAllocation<std::uint64_t> values;
    if (error == cudaSuccess) {
        error = values.allocate(2U);
    }
    if (error == cudaSuccess) {
        cuda_v2_abi_probe_kernel<<<1U, 1U, 0U, options.stream>>>(values.get());
        error = cudaPeekAtLastError();
    }
    std::uint64_t host_values[2]{};
    if (error == cudaSuccess) {
        error = cudaMemcpyAsync(host_values, values.get(), sizeof(host_values),
                                cudaMemcpyDeviceToHost, options.stream);
    }
    if (error == cudaSuccess) {
        error = cudaStreamSynchronize(options.stream);
    }
    if (error != cudaSuccess) {
        result.detail = cuda_detail("CUDA V2 ABI probe", error);
        return result;
    }
    result.device_size = host_values[0];
    result.device_alignment = host_values[1];
    result.status = result.host_size == 16U && result.host_alignment == 16U &&
                            result.device_size == 16U &&
                            result.device_alignment == 16U
                        ? EvidenceStatus::passed
                        : EvidenceStatus::failed;
    return result;
}

ProductionOutcome cuda_v2_search_float32(DatasetView dataset, QueryView query,
                                          const CudaV2Options& options) {
    return run_cuda_v2(dataset, query, options, false,
                       detail::CudaV2InjectedFailure::none)
        .production;
}

CudaV2EvidencedOutcome cuda_v2_search_float32_evidenced(
    DatasetView dataset, QueryView query, const CudaV2Options& options) {
    return run_cuda_v2(dataset, query, options, true,
                       detail::CudaV2InjectedFailure::none);
}

std::size_t cuda_v2_reduction_record_size() noexcept {
    return sizeof(CudaV2ReductionRecord);
}
std::size_t cuda_v2_reduction_record_alignment() noexcept {
    return alignof(CudaV2ReductionRecord);
}

namespace detail {

S7StagedOutcome cuda_v2_search_float32_staged_for_benchmark(
    DatasetView dataset, QueryView query, const CudaV2Options& options) {
    S7StagedOutcome result;
    auto production = run_cuda_v2(dataset, query, options, false,
                                  CudaV2InjectedFailure::none, &result.stages);
    result.production = std::move(production.production);
    return result;
}

ProductionOutcome cuda_v2_search_float32_for_s8_benchmark(
    DatasetView dataset, QueryView query, S8TransposeImplementation implementation,
    const CudaV2Options& options) {
    return run_cuda_v2(dataset, query, options, false,
                       CudaV2InjectedFailure::none, nullptr, implementation)
        .production;
}

CudaV2EvidencedOutcome cuda_v2_search_float32_evidenced_for_s8_testing(
    DatasetView dataset, QueryView query, S8TransposeImplementation implementation,
    const CudaV2Options& options) {
    return run_cuda_v2(dataset, query, options, true,
                       CudaV2InjectedFailure::none, nullptr, implementation);
}

S7StagedOutcome cuda_v2_search_float32_staged_for_s8_benchmark(
    DatasetView dataset, QueryView query, S8TransposeImplementation implementation,
    const CudaV2Options& options) {
    S7StagedOutcome result;
    auto production = run_cuda_v2(dataset, query, options, false,
                                  CudaV2InjectedFailure::none, &result.stages,
                                  implementation);
    result.production = std::move(production.production);
    return result;
}

CudaV2EvidencedOutcome cuda_v2_search_float32_for_testing(
    DatasetView dataset, QueryView query, const CudaV2Options& options,
    CudaV2InjectedFailure injected_failure) {
    return run_cuda_v2(dataset, query, options, true, injected_failure);
}

CudaV2ReductionDiagnostics cuda_v2_reduce_diagnostics_for_testing(
    const std::vector<CudaV2ReductionInput>& records,
    const CudaV2Options& options) {
    CudaV2ReductionDiagnostics diagnostics;
    auto& outcome = diagnostics.production;
    outcome.input_outcome = InputOutcome::not_applicable;
    outcome.domain_status = DomainStatus::unknown;
    if (records.empty()) {
        outcome.invocation_status = InvocationStatus::api_misuse;
        outcome.backend_detail = "reduction fixture is empty";
        return diagnostics;
    }
    if (!supported_block_size(options.threads_per_block)) {
        outcome.invocation_status = InvocationStatus::api_misuse;
        outcome.backend_detail = "unsupported CUDA V2 fixture block size";
        return diagnostics;
    }
    std::vector<CudaV2ReductionRecord> host_records(records.size());
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (!records[i].logical_candidate_present) {
            continue;
        }
        host_records[i].flags = kRecordPresent;
        host_records[i].index = records[i].index;
        if (std::isfinite(records[i].distance)) {
            host_records[i].distance = records[i].distance;
            host_records[i].flags |= kRecordFiniteMinimum;
        } else {
            host_records[i].flags |= kRecordNonfinite | kRecordDomainFailure;
        }
    }
    DeviceAllocation<CudaV2ReductionRecord> device_a;
    DeviceAllocation<CudaV2ReductionRecord> device_b;
    cudaError_t error = cudaSetDevice(options.device_ordinal);
    if (error == cudaSuccess) {
        error = device_a.allocate(records.size());
    }
    if (error == cudaSuccess) {
        error = device_b.allocate(records.size());
    }
    if (error == cudaSuccess) {
        error = cudaMemcpyAsync(device_a.get(), host_records.data(),
                                host_records.size() * sizeof(host_records[0]),
                                cudaMemcpyHostToDevice, options.stream);
    }
    CudaV2ReductionRecord* current = device_a.get();
    CudaV2ReductionRecord* next = device_b.get();
    std::uint64_t count = static_cast<std::uint64_t>(records.size());
    while (error == cudaSuccess && count > 1U) {
        const std::uint64_t next_count =
            ceiling_divide(count, options.threads_per_block);
        error = launch_record_stage(
            options.threads_per_block, static_cast<unsigned int>(next_count),
            options.stream, current, count, next);
        std::swap(current, next);
        count = next_count;
    }
    CudaV2ReductionRecord record{};
    if (error == cudaSuccess) {
        error = cudaMemcpyAsync(&record, current, sizeof(record),
                                cudaMemcpyDeviceToHost, options.stream);
    }
    if (error == cudaSuccess) {
        error = cudaStreamSynchronize(options.stream);
    }
    if (error != cudaSuccess) {
        outcome.invocation_status = InvocationStatus::backend_failure;
        outcome.backend_detail = cuda_detail("CUDA V2 reduction fixture", error);
        return diagnostics;
    }
    diagnostics.logical_candidate_present =
        (record.flags & kRecordPresent) != 0U;
    diagnostics.finite_minimum_present =
        (record.flags & kRecordFiniteMinimum) != 0U;
    diagnostics.nonfinite_arithmetic_seen =
        (record.flags & kRecordNonfinite) != 0U;
    diagnostics.domain_failure_seen =
        (record.flags & kRecordDomainFailure) != 0U;
    diagnostics.finite_minimum_distance = record.distance;
    diagnostics.finite_minimum_index = record.index;
    CudaV2EvidencedOutcome wrapper;
    wrapper.production = outcome;
    apply_final_record(wrapper, record);
    outcome = wrapper.production;
    return diagnostics;
}

std::vector<float> cuda_v2_transform_aos_to_soa_for_testing(DatasetView dataset) {
    return cuda_v2_transform_aos_to_soa_for_testing(
        dataset, CudaV2TransposeTileCandidate::tile_32x32);
}

std::vector<float> cuda_v2_transform_aos_to_soa_for_testing(
    DatasetView dataset, CudaV2TransposeTileCandidate tile) {
    if (dataset.data == nullptr || dataset.candidate_count == 0U ||
        dataset.dimension == 0U) {
        return {};
    }
    std::vector<float> result;
    std::string detail;
    S8TransposeImplementation implementation = S8TransposeImplementation::tile_32x32;
    switch (tile) {
        case CudaV2TransposeTileCandidate::tile_32x32:
            implementation = S8TransposeImplementation::tile_32x32;
            break;
        case CudaV2TransposeTileCandidate::tile_64x32:
            implementation = S8TransposeImplementation::tile_64x32;
            break;
        case CudaV2TransposeTileCandidate::tile_64x64:
            implementation = S8TransposeImplementation::tile_64x64;
            break;
    }
    if (!transform_aos_to_soa(dataset, result, detail, implementation, nullptr)) {
        return {};
    }
    return result;
}

}  // namespace detail
}  // namespace s6
