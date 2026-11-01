#include "s6/cuda_v0.hpp"

#include <cuda_runtime.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <sstream>
#include <string>
#include <utility>

namespace s6 {
namespace {

constexpr std::uint64_t kMaximumPolicyDimension = (std::uint64_t{1} << 24) - 1U;
constexpr std::uint32_t kRecordPresent = 1U << 0U;
constexpr std::uint32_t kRecordFiniteMinimum = 1U << 1U;
constexpr std::uint32_t kRecordNonfinite = 1U << 2U;
constexpr std::uint32_t kRecordDomainFailure = 1U << 3U;

struct alignas(16) CudaV0FinalRecord {
    float distance;
    std::uint32_t flags;
    std::uint64_t index;
};

static_assert(sizeof(CudaV0FinalRecord) == 16U,
              "S7 V0 final record must be exactly 16 bytes");
static_assert(alignof(CudaV0FinalRecord) == 16U,
              "S7 V0 final record must have 16-byte alignment");

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

__global__ void cuda_v0_distance_kernel(const float* dataset, const float* query,
                                        std::uint64_t candidate_count,
                                        std::uint64_t dimension, float* distances,
                                        std::uint64_t* mapping) {
    const std::uint64_t candidate =
        static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (candidate >= candidate_count) {
        return;
    }
    const std::uint64_t base = candidate * dimension;
    float accumulator = 0.0F;
#pragma unroll 1
    for (std::uint64_t j = 0; j < dimension; ++j) {
        const float difference = __fsub_rn(dataset[base + j], query[j]);
        accumulator = __fmaf_rn(difference, difference, accumulator);
    }
    distances[candidate] = accumulator;
    if (mapping != nullptr) {
        mapping[candidate] = candidate;
    }
}

__global__ void cuda_v0_serial_reduce_kernel(const float* distances,
                                             std::uint64_t candidate_count,
                                             CudaV0FinalRecord* output) {
    if (blockIdx.x != 0U || threadIdx.x != 0U) {
        return;
    }
    CudaV0FinalRecord result{};
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

__global__ void cuda_v0_abi_probe_kernel(std::uint64_t* output) {
    if (blockIdx.x == 0U && threadIdx.x == 0U) {
        output[0] = static_cast<std::uint64_t>(sizeof(CudaV0FinalRecord));
        output[1] = static_cast<std::uint64_t>(alignof(CudaV0FinalRecord));
    }
}

CudaV0EvidencedOutcome run_cuda_v0(
    DatasetView dataset, QueryView query, const CudaV0Options& options,
    bool collect_evidence, detail::CudaV0InjectedFailure injected_failure) {
    CudaV0EvidencedOutcome result;
    const auto input = inspect_input(dataset, query);
    result.production.invocation_status = input.invocation;
    result.production.input_outcome = input.input;
    result.production.domain_status = input.domain;
    result.production.backend_detail = input.detail;
    result.evidence.expected_candidate_count = dataset.candidate_count;
    if (input.invocation != InvocationStatus::completed) {
        return result;
    }

    if (injected_failure == detail::CudaV0InjectedFailure::resource_allocation) {
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
    if (properties.maxThreadsPerBlock < static_cast<int>(kCudaV0ThreadsPerBlock)) {
        result.production.invocation_status = InvocationStatus::api_misuse;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "device does not support 256-thread V0 blocks";
        return result;
    }
    const std::uint64_t grid =
        (dataset.candidate_count - 1U) / kCudaV0ThreadsPerBlock + 1U;
    if (grid == 0U || grid > static_cast<std::uint64_t>(properties.maxGridSize[0]) ||
        grid > static_cast<std::uint64_t>(std::numeric_limits<unsigned int>::max())) {
        result.production.invocation_status = InvocationStatus::api_misuse;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "candidate count exceeds CUDA grid capacity";
        return result;
    }

    DeviceAllocation<float> device_dataset;
    DeviceAllocation<float> device_query;
    DeviceAllocation<float> device_distances;
    DeviceAllocation<CudaV0FinalRecord> device_record;
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

    if (injected_failure == detail::CudaV0InjectedFailure::backend_copy) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "injected CUDA copy failure";
        return result;
    }
    error = cudaMemcpyAsync(device_dataset.get(), dataset.data, input.dataset_bytes,
                            cudaMemcpyHostToDevice, options.stream);
    if (error == cudaSuccess) {
        error = cudaMemcpyAsync(device_query.get(), query.data, input.query_bytes,
                                cudaMemcpyHostToDevice, options.stream);
    }
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

    if (injected_failure == detail::CudaV0InjectedFailure::backend_launch) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "injected CUDA launch failure";
        return result;
    }
    (void)cudaGetLastError();
    cuda_v0_distance_kernel<<<static_cast<unsigned int>(grid),
                              kCudaV0ThreadsPerBlock, 0U, options.stream>>>(
        device_dataset.get(), device_query.get(), dataset.candidate_count,
        dataset.dimension, device_distances.get(),
        collect_evidence ? device_mapping.get() : nullptr);
    error = cudaPeekAtLastError();
    if (error == cudaSuccess) {
        cuda_v0_serial_reduce_kernel<<<1U, 1U, 0U, options.stream>>>(
            device_distances.get(), dataset.candidate_count, device_record.get());
        error = cudaPeekAtLastError();
    }
    if (error != cudaSuccess) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = cuda_detail("CUDA V0 kernel launch", error);
        return result;
    }

    CudaV0FinalRecord host_record{};
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
    if (injected_failure == detail::CudaV0InjectedFailure::backend_synchronize) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = "injected CUDA synchronization failure";
        return result;
    }
    error = cudaStreamSynchronize(options.stream);
    if (error != cudaSuccess) {
        result.production.invocation_status = InvocationStatus::backend_failure;
        result.production.domain_status = DomainStatus::unknown;
        result.production.backend_detail = cuda_detail("cudaStreamSynchronize", error);
        return result;
    }

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

CudaV0DeviceInfo inspect_cuda_v0_device(int device_ordinal) {
    CudaV0DeviceInfo result;
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

CudaV0AbiInfo inspect_cuda_v0_final_record_abi(const CudaV0Options& options) {
    CudaV0AbiInfo result;
    result.host_size = sizeof(CudaV0FinalRecord);
    result.host_alignment = alignof(CudaV0FinalRecord);
    cudaError_t error = cudaSetDevice(options.device_ordinal);
    DeviceAllocation<std::uint64_t> device_values;
    if (error == cudaSuccess) {
        error = device_values.allocate(2U);
    }
    if (error == cudaSuccess) {
        (void)cudaGetLastError();
        cuda_v0_abi_probe_kernel<<<1U, 1U, 0U, options.stream>>>(device_values.get());
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
        result.detail = cuda_detail("CUDA V0 ABI probe", error);
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

ProductionOutcome cuda_v0_search_float32(DatasetView dataset, QueryView query,
                                         const CudaV0Options& options) {
    return run_cuda_v0(dataset, query, options, false,
                       detail::CudaV0InjectedFailure::none)
        .production;
}

CudaV0EvidencedOutcome cuda_v0_search_float32_evidenced(
    DatasetView dataset, QueryView query, const CudaV0Options& options) {
    return run_cuda_v0(dataset, query, options, true,
                       detail::CudaV0InjectedFailure::none);
}

std::size_t cuda_v0_final_record_size() noexcept { return sizeof(CudaV0FinalRecord); }
std::size_t cuda_v0_final_record_alignment() noexcept {
    return alignof(CudaV0FinalRecord);
}

namespace detail {

CudaV0EvidencedOutcome cuda_v0_search_float32_for_testing(
    DatasetView dataset, QueryView query, const CudaV0Options& options,
    CudaV0InjectedFailure injected_failure) {
    return run_cuda_v0(dataset, query, options, true, injected_failure);
}

ProductionOutcome cuda_v0_reduce_fixture_for_testing(
    const std::vector<float>& distances, const CudaV0Options& options) {
    return cuda_v0_reduce_diagnostics_for_testing(distances, options).production;
}

CudaV0ReductionDiagnostics cuda_v0_reduce_diagnostics_for_testing(
    const std::vector<float>& distances, const CudaV0Options& options) {
    CudaV0ReductionDiagnostics diagnostics;
    auto& outcome = diagnostics.production;
    if (distances.empty()) {
        outcome.invocation_status = InvocationStatus::api_misuse;
        outcome.backend_detail = "empty reduction fixture";
        return diagnostics;
    }
    DeviceAllocation<float> device_distances;
    DeviceAllocation<CudaV0FinalRecord> device_record;
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
        cuda_v0_serial_reduce_kernel<<<1U, 1U, 0U, options.stream>>>(
            device_distances.get(), static_cast<std::uint64_t>(distances.size()),
            device_record.get());
        error = cudaPeekAtLastError();
    }
    CudaV0FinalRecord record{};
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

}  // namespace detail
}  // namespace s6
