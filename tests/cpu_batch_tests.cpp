#include "s6/cpu_baseline.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::uint32_t bits(float value) {
    std::uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

void test_batch_sizes_and_equivalence() {
    constexpr std::uint64_t n = 17U;
    constexpr std::uint64_t d = 5U;
    std::vector<float> dataset(static_cast<std::size_t>(n * d));
    for (std::uint64_t i = 0; i < n; ++i) {
        for (std::uint64_t j = 0; j < d; ++j) {
            dataset[static_cast<std::size_t>(i * d + j)] =
                static_cast<float>(static_cast<int>((i * 13U + j * 7U) % 43U) -
                                   21) /
                8.0F;
        }
    }
    std::vector<float> queries(static_cast<std::size_t>(32U * d));
    for (std::uint64_t q = 0; q < 32U; ++q) {
        for (std::uint64_t j = 0; j < d; ++j) {
            queries[static_cast<std::size_t>(q * d + j)] =
                static_cast<float>(
                    static_cast<int>((q * 5U + j * 11U) % 37U) - 18) /
                8.0F;
        }
    }
    queries[0] = -0.0F;
    const s6::DatasetView dataset_view{dataset.data(), n, d, dataset.size()};
    for (const std::uint64_t batch_size : {1U, 8U, 32U}) {
        const auto batch = s6::cpu_search_batch_float32(
            dataset_view,
            {queries.data(), batch_size, d,
             static_cast<std::size_t>(batch_size * d)});
        require(batch.invocation_status == s6::InvocationStatus::completed &&
                    batch.results.size() == static_cast<std::size_t>(batch_size),
                "CPU batch execution failed");
        for (std::uint64_t q = 0; q < batch_size; ++q) {
            const auto single = s6::cpu_search_float32(
                dataset_view,
                {queries.data() + static_cast<std::size_t>(q * d), d,
                 static_cast<std::size_t>(d)});
            const auto index = static_cast<std::size_t>(q);
            require(single.result &&
                        single.result->index == batch.results[index].index &&
                        bits(single.result->squared_distance) ==
                            bits(batch.results[index].squared_distance),
                    "CPU batch differs from single-query production");
        }
    }
}

void test_ties_and_errors() {
    constexpr std::uint64_t d = 2U;
    const std::vector<float> dataset = {
        1.0F, 1.0F, 2.0F, 2.0F, 1.0F, 1.0F, -1.0F, -1.0F};
    const std::vector<float> queries = {1.0F, 1.0F, -0.0F, +0.0F};
    const s6::DatasetView dataset_view{dataset.data(), 4U, d, dataset.size()};
    const auto batch = s6::cpu_search_batch_float32(
        dataset_view, {queries.data(), 2U, d, queries.size()});
    require(batch.invocation_status == s6::InvocationStatus::completed &&
                batch.results.size() == 2U && batch.results[0].index == 0U &&
                batch.results[0].squared_distance == 0.0F,
            "CPU batch tie or exact-zero behavior failed");
    require(s6::cpu_search_batch_float32(
                dataset_view, {queries.data(), 0U, d, 0U})
                .invocation_status == s6::InvocationStatus::api_misuse,
            "CPU zero-query batch was not API misuse");
    require(s6::cpu_search_batch_float32(
                dataset_view, {queries.data(), 2U, d, queries.size() - 1U})
                .invocation_status == s6::InvocationStatus::api_misuse,
            "CPU inconsistent batch shape was not API misuse");
    std::vector<float> invalid = queries;
    invalid[2] = std::numeric_limits<float>::infinity();
    require(s6::cpu_search_batch_float32(
                dataset_view, {invalid.data(), 2U, d, invalid.size()})
                .input_outcome == s6::InputOutcome::invalid_nonfinite,
            "CPU nonfinite batch was not rejected");

    const float maximum = std::numeric_limits<float>::max();
    const float negative_maximum = -maximum;
    const auto overflow = s6::cpu_search_batch_float32(
        {&maximum, 1U, 1U, 1U}, {&negative_maximum, 1U, 1U, 1U});
    require(overflow.invocation_status == s6::InvocationStatus::backend_failure &&
                overflow.results.empty(),
            "CPU batch nonfinite arithmetic did not fail all-or-nothing");
}

}  // namespace

int main() {
    try {
        test_batch_sizes_and_equivalence();
        test_ties_and_errors();
        std::cout << "S14 CPU batch tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "S14 CPU batch tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
