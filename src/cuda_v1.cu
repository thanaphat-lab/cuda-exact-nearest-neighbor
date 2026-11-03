#include "s6/cuda_v1.hpp"
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

namespace s6 {
namespace {

constexpr std::uint64_t kMaximumPolicyDimension = (std::uint64_t{1} << 24) - 1U;
constexpr std::uint32_t kRecordPresent = 1U << 0U;
constexpr std::uint32_t kRecordFiniteMinimum = 1U << 1U;
constexpr std::uint32_t kRecordNonfinite = 1U << 2U;
constexpr std::uint32_t kRecordDomainFailure = 1U << 3U;

struct alignas(16) CudaV1FinalRecord {
    float distance;
    std::uint32_t flags;
    std::uint64_t index;
};

static_assert(sizeof(CudaV1FinalRecord) == 16U,
              "S7 V1 final record must be exactly 16 bytes");
static_assert(alignof(CudaV1FinalRecord) == 16U,
              "S7 V1 final record must have 16-byte alignment");

struct InputCheck {
    InvocationStatus invocation = InvocationStatus::not_executed;
    InputOutcome input = InputOutcome::not_applicable;
    DomainStatus domain = DomainStatus::not_applicable;
    std::string detail;
    std::size_t dataset_bytes = 0;
    std::size_t query_bytes = 0;
    std::size_t distance_bytes = 0;
};

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
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() / dimension)) {
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
            return {InvocationStatus::contract_invalid, InputOutcome::invalid_nonfinite,
                    DomainStatus::not_applicable,
                    "dataset contains NaN or infinity"};
        }
    }
    for (std::size_t j = 0; j < query.element_count; ++j) {
        if (!std::isfinite(query.data[j])) {
            return {InvocationStatus::contract_invalid, InputOutcome::invalid_nonfinite,
                    DomainStatus::not_applicable,
                    "query contains NaN or infinity"};
        }
    }
    const auto domain = dataset.dimension <= kMaximumPolicyDimension
                            ? DomainStatus::core_eligible
                            : DomainStatus::valid_outside_approved_domain;
    InputCheck result;
    result.invocation = InvocationStatus::completed;
    result.input = InputOutcome::valid;
    result.domain = domain;
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
bool transform_aos_to_soa_blocked(DatasetView dataset, std::vector<float>& soa,
                                  std::string& detail) {
    static_assert(TileN > 0U && TileD > 0U, "transpose tiles must be nonzero");
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

    const auto candidate_count = static_cast<std::size_t>(dataset.candidate_count);
    const auto dimension = static_cast<std::size_t>(dataset.dimension);
    for (std::size_t i0 = 0; i0 < candidate_count;) {
        const std::size_t i_end = i0 + std::min(TileN, candidate_count - i0);
        for (std::size_t j0 = 0; j0 < dimension;) {
            const std::size_t j_end = j0 + std::min(TileD, dimension - j0);
            for (std::size_t i = i0; i < i_end; ++i) {
                const std::size_t aos_base = i * dimension;
                for (std::size_t j = j0; j < j_end; ++j) {
                    soa[j * candidate_count + i] = dataset.data[aos_base + j];
                }
            }
            j0 = j_end;
        }
        i0 = i_end;
    }
    return true;
}

bool transform_aos_to_soa(DatasetView dataset, std::vector<float>& soa,
                          std::string& detail) {
    return transform_aos_to_soa_blocked<32U, 32U>(dataset, soa, detail);
}

__global__ void cuda_v1_distance_kernel(const float* dataset, const float* query,
                                        std::uint64_t candidate_count,
                                        std::uint64_t dimension, float* distances,
                                        std::uint64_t* mapping) {
    const std::uint64_t candidate =
        static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (candidate >= candidate_count) {
        return;
    }
    float accumulator = 0.0F;
#pragma unroll 1
    for (std::uint64_t j = 0; j < dimension; ++j) {
        const float difference =
            __fsub_rn(dataset[j * candidate_count + candidate], query[j]);
        accumulator = __fmaf_rn(difference, difference, accumulator);
    }
    distances[candidate] = accumulator;
    if (mapping != nullptr) {
        mapping[candidate] = candidate;
    }
}

__global__ void cuda_v1_serial_reduce_kernel(const float* distances,
                                             std::uint64_t candidate_count,
                                             CudaV1FinalRecord* output) {
    if (blockIdx.x != 0U || threadIdx.x != 0U) {
        return;
    }
    CudaV1FinalRecord result{};
    bool have_finite = false;
    for (std::uint64_t i = 0; i < candidate_count; ++i) {
        result.flags |= kRecordPresent;
        const float distance = distances[i];
        if (isfinite(distance)) {
            if (!have_finite || distance < result.distance ||
                (distance == result.distance && i < result.index)) {
                result.distance = distance;
                result.index = i;
                have_finite = true;
            }
        } else {
            result.flags |= kRecordNonfinite | kRecordDomainFailure;
        }
    }
    if (have_finite) {
        result.flags |= kRecordFiniteMinimum;
    }
    *output = result;
}

__global__ void cuda_v1_abi_probe_kernel(std::uint64_t* output) {
    if (blockIdx.x == 0U && threadIdx.x == 0U) {
        output[0] = static_cast<std::uint64_t>(sizeof(CudaV1FinalRecord));
        output[1] = static_cast<std::uint64_t>(alignof(CudaV1FinalRecord));
    }
}

CudaV1EvidencedOutcome run_cuda_v1(
    DatasetView dataset, QueryView query, const CudaV1Options& options,
    bool collect_evidence, detail::CudaV1InjectedFailure injected_failure,
    detail::S7CudaStageTiming* benchmark_timing = nullptr) {
    CudaV1EvidencedOutcome result;
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

    if (injected_failure == detail::CudaV1InjectedFailure::resource_allocation) {
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
    if (properties.maxThreadsPerBlock < static_cast<int>(kCudaV1ThreadsPerBlock)) {
        result.production.invocation_status = InvocationStatus::api_misuse;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "device does not support 256-thread V1 blocks";
        return result;
    }
    const std::uint64_t grid =
        (dataset.candidate_count - 1U) / kCudaV1ThreadsPerBlock + 1U;
    if (grid == 0U || grid > static_cast<std::uint64_t>(properties.maxGridSize[0]) ||
        grid > static_cast<std::uint64_t>(std::numeric_limits<unsigned int>::max())) {
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
    if (!transform_aos_to_soa(dataset, host_soa_dataset, transform_detail)) {
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
    DeviceAllocation<float> device_distances;
    DeviceAllocation<CudaV1FinalRecord> device_record;
    DeviceAllocation<std::uint64_t> device_mapping;
    if (collect_evidence &&
        dataset.candidate_count >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() /
                                       sizeof(std::uint64_t))) {
        result.production.invocation_status = InvocationStatus::api_misuse;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "mapping evidence byte-count overflow";
        return result;
    }
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
        !allocate(device_distances, static_cast<std::size_t>(dataset.candidate_count),
                  "cudaMalloc(distances)") ||
        !allocate(device_record, 1U, "cudaMalloc(final record)") ||
        (collect_evidence &&
         !allocate(device_mapping, static_cast<std::size_t>(dataset.candidate_count),
                   "cudaMalloc(mapping evidence)"))) {
        return result;
    }
    if (benchmark_timing != nullptr) {
        benchmark_timing->allocation_ns = detail::benchmark_internal::elapsed_ns(
            allocation_start, detail::benchmark_internal::Clock::now());
    }
    detail::benchmark_internal::StageEvents stage_events(benchmark_timing);

    if (injected_failure == detail::CudaV1InjectedFailure::backend_copy) {
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
    if (error == cudaSuccess) {
        error = cudaMemsetAsync(device_distances.get(), 0xff, input.distance_bytes,
                                options.stream);
    }
    if (error == cudaSuccess && collect_evidence) {
        error = cudaMemsetAsync(device_mapping.get(), 0xff,
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

    if (injected_failure == detail::CudaV1InjectedFailure::backend_launch) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "injected CUDA launch failure";
        return result;
    }
    (void)cudaGetLastError();
    cuda_v1_distance_kernel<<<static_cast<unsigned int>(grid),
                              kCudaV1ThreadsPerBlock, 0U, options.stream>>>(
        device_dataset.get(), device_query.get(), dataset.candidate_count,
        dataset.dimension, device_distances.get(),
        collect_evidence ? device_mapping.get() : nullptr);
    error = cudaPeekAtLastError();
    if (error == cudaSuccess) {
        cuda_v1_serial_reduce_kernel<<<1U, 1U, 0U, options.stream>>>(
            device_distances.get(), dataset.candidate_count, device_record.get());
        error = cudaPeekAtLastError();
    }
    if (error != cudaSuccess) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = cuda_detail("CUDA V1 kernel launch", error);
        return result;
    }
    stage_events.record(3U, options.stream);

    CudaV1FinalRecord host_record{};
    error = cudaMemcpyAsync(&host_record, device_record.get(), sizeof(host_record),
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
    if (injected_failure == detail::CudaV1InjectedFailure::backend_synchronize) {
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
        result.production.backend_detail = cuda_detail("cudaStreamSynchronize", error);
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
            "mapping and poison-overwrite are production instrumentation; "
            "independent distance comparison remains required";
    }

    if ((host_record.flags & kRecordPresent) == 0U) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "CUDA reducer lost logical presence";
        return result;
    }
    if ((host_record.flags & (kRecordNonfinite | kRecordDomainFailure)) != 0U) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::valid_outside_approved_domain;
        result.production.backend_detail =
            "nonfinite CUDA candidate arithmetic propagated by reducer";
        return result;
    }
    if ((host_record.flags & kRecordFiniteMinimum) == 0U) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "CUDA reducer produced no finite minimum";
        return result;
    }
    result.production.result = SearchResult{host_record.index, host_record.distance};
    result.production.invocation_status = InvocationStatus::completed;
    result.production.backend_detail.clear();
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

CudaV1DeviceInfo inspect_cuda_v1_device(int device_ordinal) {
    CudaV1DeviceInfo result;
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
    error = cudaDriverGetVersion(&result.driver_version);
    if (error == cudaSuccess) {
        error = cudaRuntimeGetVersion(&result.runtime_version);
    }
    if (error != cudaSuccess) {
        result.detail = cuda_detail("CUDA version query", error);
        return result;
    }
    result.status = EvidenceStatus::passed;
    return result;
}

CudaV1AbiInfo inspect_cuda_v1_final_record_abi(const CudaV1Options& options) {
    CudaV1AbiInfo result;
    result.host_size = sizeof(CudaV1FinalRecord);
    result.host_alignment = alignof(CudaV1FinalRecord);
    cudaError_t error = cudaSetDevice(options.device_ordinal);
    DeviceAllocation<std::uint64_t> device_values;
    if (error == cudaSuccess) {
        error = device_values.allocate(2U);
    }
    if (error == cudaSuccess) {
        (void)cudaGetLastError();
        cuda_v1_abi_probe_kernel<<<1U, 1U, 0U, options.stream>>>(device_values.get());
        error = cudaPeekAtLastError();
    }
    std::uint64_t host_values[2] = {0U, 0U};
    if (error == cudaSuccess) {
        error = cudaMemcpyAsync(host_values, device_values.get(), sizeof(host_values),
                                cudaMemcpyDeviceToHost, options.stream);
    }
    if (error == cudaSuccess) {
        error = cudaStreamSynchronize(options.stream);
    }
    if (error != cudaSuccess) {
        result.detail = cuda_detail("CUDA V1 ABI probe", error);
        return result;
    }
    result.device_size = host_values[0];
    result.device_alignment = host_values[1];
    if (result.host_size == 16U && result.host_alignment == 16U &&
        result.device_size == 16U && result.device_alignment == 16U &&
        result.host_size == result.device_size &&
        result.host_alignment == result.device_alignment) {
        result.status = EvidenceStatus::passed;
    } else {
        result.status = EvidenceStatus::failed;
        result.detail = "host/device final-record ABI mismatch";
    }
    return result;
}

ProductionOutcome cuda_v1_search_float32(DatasetView dataset, QueryView query,
                                         const CudaV1Options& options) {
    return run_cuda_v1(dataset, query, options, false,
                       detail::CudaV1InjectedFailure::none)
        .production;
}

CudaV1EvidencedOutcome cuda_v1_search_float32_evidenced(
    DatasetView dataset, QueryView query, const CudaV1Options& options) {
    return run_cuda_v1(dataset, query, options, true,
                       detail::CudaV1InjectedFailure::none);
}

std::size_t cuda_v1_final_record_size() noexcept { return sizeof(CudaV1FinalRecord); }
std::size_t cuda_v1_final_record_alignment() noexcept {
    return alignof(CudaV1FinalRecord);
}

namespace detail {

S7StagedOutcome cuda_v1_search_float32_staged_for_benchmark(
    DatasetView dataset, QueryView query, const CudaV1Options& options) {
    S7StagedOutcome result;
    auto production = run_cuda_v1(dataset, query, options, false,
                                  CudaV1InjectedFailure::none, &result.stages);
    result.production = std::move(production.production);
    return result;
}

CudaV1EvidencedOutcome cuda_v1_search_float32_for_testing(
    DatasetView dataset, QueryView query, const CudaV1Options& options,
    CudaV1InjectedFailure injected_failure) {
    return run_cuda_v1(dataset, query, options, true, injected_failure);
}

ProductionOutcome cuda_v1_reduce_fixture_for_testing(
    const std::vector<float>& distances, const CudaV1Options& options) {
    return cuda_v1_reduce_diagnostics_for_testing(distances, options).production;
}

CudaV1ReductionDiagnostics cuda_v1_reduce_diagnostics_for_testing(
    const std::vector<float>& distances, const CudaV1Options& options) {
    CudaV1ReductionDiagnostics diagnostics;
    auto& outcome = diagnostics.production;
    if (distances.empty()) {
        outcome.invocation_status = InvocationStatus::api_misuse;
        outcome.backend_detail = "empty reduction fixture";
        return diagnostics;
    }
    DeviceAllocation<float> device_distances;
    DeviceAllocation<CudaV1FinalRecord> device_record;
    cudaError_t error = cudaSetDevice(options.device_ordinal);
    if (error == cudaSuccess) {
        error = device_distances.allocate(distances.size());
    }
    if (error == cudaSuccess) {
        error = device_record.allocate(1U);
    }
    if (error == cudaSuccess) {
        error = cudaMemcpyAsync(device_distances.get(), distances.data(),
                                distances.size() * sizeof(float),
                                cudaMemcpyHostToDevice, options.stream);
    }
    if (error == cudaSuccess) {
        cuda_v1_serial_reduce_kernel<<<1U, 1U, 0U, options.stream>>>(
            device_distances.get(), static_cast<std::uint64_t>(distances.size()),
            device_record.get());
        error = cudaPeekAtLastError();
    }
    CudaV1FinalRecord record{};
    if (error == cudaSuccess) {
        error = cudaMemcpyAsync(&record, device_record.get(), sizeof(record),
                                cudaMemcpyDeviceToHost, options.stream);
    }
    if (error == cudaSuccess) {
        error = cudaStreamSynchronize(options.stream);
    }
    if (error != cudaSuccess) {
        outcome.invocation_status = error == cudaErrorMemoryAllocation
                                        ? InvocationStatus::resource_failure
                                        : InvocationStatus::backend_failure;
        outcome.domain_status = DomainStatus::unknown;
        outcome.backend_detail = cuda_detail("reduction fixture", error);
        return diagnostics;
    }
    outcome.input_outcome = InputOutcome::valid;
    diagnostics.logical_candidate_present = (record.flags & kRecordPresent) != 0U;
    diagnostics.finite_minimum_present =
        (record.flags & kRecordFiniteMinimum) != 0U;
    diagnostics.nonfinite_arithmetic_seen =
        (record.flags & kRecordNonfinite) != 0U;
    diagnostics.domain_failure_seen =
        (record.flags & kRecordDomainFailure) != 0U;
    diagnostics.finite_minimum_distance = record.distance;
    diagnostics.finite_minimum_index = record.index;
    if ((record.flags & (kRecordNonfinite | kRecordDomainFailure)) != 0U) {
        outcome.invocation_status = InvocationStatus::backend_failure;
        outcome.domain_status = DomainStatus::valid_outside_approved_domain;
        outcome.backend_detail = "fixture propagated nonfinite status";
        return diagnostics;
    }
    outcome.invocation_status = InvocationStatus::completed;
    outcome.domain_status = DomainStatus::core_eligible;
    outcome.result = SearchResult{record.index, record.distance};
    return diagnostics;
}

std::vector<float> cuda_v1_transform_aos_to_soa_for_testing(DatasetView dataset) {
    return cuda_v1_transform_aos_to_soa_for_testing(
        dataset, CudaV1TransposeTileCandidate::tile_32x32);
}

std::vector<float> cuda_v1_transform_aos_to_soa_for_testing(
    DatasetView dataset, CudaV1TransposeTileCandidate tile) {
    if (dataset.data == nullptr || dataset.candidate_count == 0U ||
        dataset.dimension == 0U ||
        dataset.dimension >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument("invalid dataset view for AoS-to-SoA transform");
    }
    const auto dimension = static_cast<std::size_t>(dataset.dimension);
    if (dataset.candidate_count >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() /
                                   dimension)) {
        throw std::invalid_argument("dataset element-count overflow");
    }
    const std::size_t expected =
        static_cast<std::size_t>(dataset.candidate_count) * dimension;
    if (dataset.element_count != expected ||
        expected > std::numeric_limits<std::size_t>::max() / sizeof(float)) {
        throw std::invalid_argument("inconsistent dataset element count");
    }
    std::vector<float> soa;
    std::string detail;
    bool transformed = false;
    switch (tile) {
        case CudaV1TransposeTileCandidate::tile_32x32:
            transformed = transform_aos_to_soa_blocked<32U, 32U>(dataset, soa, detail);
            break;
        case CudaV1TransposeTileCandidate::tile_64x32:
            transformed = transform_aos_to_soa_blocked<64U, 32U>(dataset, soa, detail);
            break;
        case CudaV1TransposeTileCandidate::tile_64x64:
            transformed = transform_aos_to_soa_blocked<64U, 64U>(dataset, soa, detail);
            break;
    }
    if (!transformed) {
        throw std::runtime_error(detail);
    }
    return soa;
}

}  // namespace detail
}  // namespace s6
