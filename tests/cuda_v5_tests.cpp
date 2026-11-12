#include "s6/cpu_baseline.hpp"
#include "s6/cuda_v5.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint32_t bits(float value) {
    std::uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

void verify_configuration(std::uint32_t block_size, std::uint32_t query_tile) {
    constexpr std::uint64_t n = 257U;
    constexpr std::uint64_t d = 17U;
    constexpr std::uint64_t q = 9U;
    std::vector<float> dataset(n * d);
    std::vector<float> queries(q * d);
    for (std::uint64_t i = 0; i < n; ++i) {
        for (std::uint64_t j = 0; j < d; ++j) {
            dataset[i * d + j] = static_cast<float>((i * 11U + j * 7U) % 101U) / 32.0F;
        }
    }
    for (std::uint64_t k = 0; k < q; ++k) {
        for (std::uint64_t j = 0; j < d; ++j) {
            queries[k * d + j] = static_cast<float>((k * 13U + j * 5U) % 97U) / 32.0F;
        }
        const std::uint64_t expected = 3U + k * 19U;
        std::copy_n(queries.data() + k * d, d, dataset.data() + expected * d);
    }
    // Equal computed distances must select the smallest index.
    std::copy_n(queries.data(), d, dataset.data() + 200U * d);
    const s6::DatasetView dataset_view{dataset.data(), n, d, dataset.size()};
    const s6::QueryBatchView query_view{queries.data(), q, d, queries.size()};
    const s6::CudaV5Options options{0, nullptr, block_size, query_tile};
    const auto cpu = s6::cpu_search_batch_float32(dataset_view, query_view);
    require(cpu.invocation_status == s6::InvocationStatus::completed,
            "CPU batch prerequisite failed");
    auto prepared_result = s6::cuda_v5_prepare_dataset_float32(dataset_view, options);
    require(prepared_result.prepared_dataset.has_value(), "V5 preparation failed");
    auto prepared = std::move(*prepared_result.prepared_dataset);
    auto workspace_result = s6::cuda_v5_create_workspace(prepared, q, options);
    require(workspace_result.workspace.has_value(), "V5 workspace failed");
    auto workspace = std::move(*workspace_result.workspace);
    const auto result = s6::cuda_v5_search_prepared_batch_float32(
        prepared, query_view, workspace, options);
    require(result.invocation_status == s6::InvocationStatus::completed &&
                result.results.size() == cpu.results.size(),
            "V5 prepared execution failed");
    for (std::size_t i = 0; i < result.results.size(); ++i) {
        const s6::QueryView single_query{
            queries.data() + i * static_cast<std::size_t>(d), d,
            static_cast<std::size_t>(d)};
        const auto reference = s6::compute_float64_reference(dataset_view,
                                                              single_query);
        require(reference.argmin_index.has_value() &&
                    *reference.argmin_index == result.results[i].index,
                "V5 result differs from Float64 exact-match reference index");
        const auto bound = s6::evaluate_ud001(
            d, result.results[i].squared_distance,
            reference.distances[result.results[i].index], true);
        require(bound.status == s6::EvidenceStatus::passed,
                "V5 exact-match result failed the approved numerical bound");
        require(result.results[i].index == cpu.results[i].index &&
                    bits(result.results[i].squared_distance) ==
                        bits(cpu.results[i].squared_distance),
                "V5 prepared result differs from CPU Float32 graph");
    }
    const auto raw = s6::cuda_v5_search_batch_float32(dataset_view, query_view, options);
    require(raw.invocation_status == s6::InvocationStatus::completed &&
                raw.results.size() == result.results.size(),
            "V5 raw execution failed");
    for (std::size_t i = 0; i < raw.results.size(); ++i) {
        require(raw.results[i].index == result.results[i].index &&
                    bits(raw.results[i].squared_distance) ==
                        bits(result.results[i].squared_distance),
                "V5 raw and prepared results differ");
    }
}

void verify_failures() {
    float dataset_value = 0.0F;
    float query_value = 0.0F;
    const s6::CudaV5Options options{};
    require(s6::cuda_v5_search_batch_float32(
                {&dataset_value, 0U, 1U, 0U}, {&query_value, 1U, 1U, 1U}, options)
                .invocation_status == s6::InvocationStatus::contract_invalid,
            "V5 must reject N=0");
    dataset_value = std::numeric_limits<float>::infinity();
    require(s6::cuda_v5_search_batch_float32(
                {&dataset_value, 1U, 1U, 1U}, {&query_value, 1U, 1U, 1U}, options)
                .invocation_status == s6::InvocationStatus::contract_invalid,
            "V5 must reject nonfinite dataset input");
    dataset_value = -0.0F;
    const auto signed_zero = s6::cuda_v5_search_batch_float32(
        {&dataset_value, 1U, 1U, 1U}, {&query_value, 1U, 1U, 1U}, options);
    require(signed_zero.invocation_status == s6::InvocationStatus::completed &&
                signed_zero.results[0].index == 0U &&
                bits(signed_zero.results[0].squared_distance) == bits(0.0F),
            "V5 must accept signed zero");
    const s6::CudaV5Options bad_block{0, nullptr, 64U, 1U};
    require(s6::cuda_v5_search_batch_float32(
                {&dataset_value, 1U, 1U, 1U}, {&query_value, 1U, 1U, 1U}, bad_block)
                .invocation_status == s6::InvocationStatus::api_misuse,
            "V5 must reject unsupported block sizes");
}

}  // namespace

int main() {
    try {
        constexpr std::array<std::uint32_t, 3> blocks{128U, 256U, 512U};
        constexpr std::array<std::uint32_t, 3> tiles{1U, 4U, 8U};
        for (const auto block : blocks) for (const auto tile : tiles) {
            verify_configuration(block, tile);
        }
        verify_failures();
        std::cout << "CUDA V5 functional tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CUDA V5 functional tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
