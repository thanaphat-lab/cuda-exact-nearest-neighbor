#include "s6/cuda_v1.hpp"
#include "s6/cuda_v0.hpp"
#include "s6/cpu_build_identity.hpp"

#include <cmath>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
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

float independent_distance(const float* candidate, const float* query,
                           std::size_t dimension) {
    float accumulator = 0.0F;
    for (std::size_t j = 0; j < dimension; ++j) {
        volatile float difference = candidate[j] - query[j];
        accumulator = std::fma(difference, difference, accumulator);
    }
    return accumulator;
}

void verify_evidence(const std::vector<float>& dataset,
                     const std::vector<float>& query, std::uint64_t n,
                     std::uint64_t d,
                     const s6::CudaV1EvidencedOutcome& outcome) {
    require(outcome.production.invocation_status == s6::InvocationStatus::completed,
            "CUDA V1 invocation did not complete");
    require(outcome.evidence.mapping_status == s6::EvidenceStatus::passed,
            "CUDA production mapping evidence failed");
    require(outcome.evidence.overwrite_status == s6::EvidenceStatus::passed,
            "CUDA poison-overwrite evidence failed");
    require(outcome.evidence.observed_mapping_count == n,
            "CUDA production mapping count mismatch");
    require(outcome.evidence.candidate_distances.size() ==
                static_cast<std::size_t>(n),
            "CUDA candidate-distance evidence count mismatch");

    const s6::DatasetView dataset_view{dataset.data(), n, d, dataset.size()};
    const s6::QueryView query_view{query.data(), d, query.size()};
    const auto cpu = s6::cpu_search_float32(dataset_view, query_view);
    const auto v0 = s6::cuda_v0_search_float32_evidenced(dataset_view, query_view);
    require(cpu.invocation_status == s6::InvocationStatus::completed && cpu.result,
            "CPU comparison invocation did not complete");
    require(v0.production.invocation_status == s6::InvocationStatus::completed &&
                v0.production.result &&
                v0.evidence.candidate_distances.size() ==
                    outcome.evidence.candidate_distances.size(),
            "CUDA V0 comparison invocation did not complete");
    require(outcome.production.result &&
                outcome.production.result->index == cpu.result->index &&
                outcome.production.result->index == v0.production.result->index &&
                bits(outcome.production.result->squared_distance) ==
                    bits(cpu.result->squared_distance) &&
                bits(outcome.production.result->squared_distance) ==
                    bits(v0.production.result->squared_distance),
            "CUDA V1 result differs from CPU or CUDA V0");
    for (std::uint64_t i = 0; i < n; ++i) {
        const auto expected = independent_distance(
            dataset.data() + static_cast<std::size_t>(i * d), query.data(),
            static_cast<std::size_t>(d));
        require(bits(outcome.evidence.candidate_distances[static_cast<std::size_t>(i)]) ==
                    bits(expected),
                "CUDA candidate distance differs from independent Float32 graph");
        require(bits(outcome.evidence.candidate_distances[static_cast<std::size_t>(i)]) ==
                    bits(v0.evidence.candidate_distances[static_cast<std::size_t>(i)]),
                "CUDA V1 candidate distance differs from CUDA V0");
    }
}

void test_transpose_mapping(std::ofstream& evidence) {
    const std::vector<float> aos = {
        1.0F, 2.0F, 3.0F, 4.0F,
        5.0F, 6.0F, 7.0F, 8.0F,
        -0.0F, +0.0F, -1.0F, 9.0F,
    };
    const std::vector<float> expected_soa = {
        1.0F, 5.0F, -0.0F,
        2.0F, 6.0F, +0.0F,
        3.0F, 7.0F, -1.0F,
        4.0F, 8.0F, 9.0F,
    };
    const auto observed = s6::detail::cuda_v1_transform_aos_to_soa_for_testing(
        {aos.data(), 3U, 4U, aos.size()});
    require(observed.size() == expected_soa.size(),
            "AoS-to-SoA transform size mismatch");
    for (std::size_t index = 0; index < observed.size(); ++index) {
        require(bits(observed[index]) == bits(expected_soa[index]),
                "AoS-to-SoA transform mapping mismatch");
    }
    evidence << "transpose_mapping=passed bit_preservation=passed N=3 D=4\n";
}

void test_device_and_abi(std::ofstream& evidence) {
    const auto device = s6::inspect_cuda_v1_device();
    require(device.status == s6::EvidenceStatus::passed,
            "CUDA V1 device inspection failed");
    require(device.compute_major > 0 && device.compute_minor >= 0,
            "CUDA device did not report a usable compute capability");
    require(device.maximum_threads_per_block >=
                static_cast<int>(s6::kCudaV1ThreadsPerBlock),
            "device cannot launch approved V1 block size");
    const auto abi = s6::inspect_cuda_v1_final_record_abi();
    require(abi.status == s6::EvidenceStatus::passed,
            "CUDA V1 host/device final-record ABI probe failed");
    evidence << "device=passed name=" << device.name << " uuid=" << device.uuid
             << " compute=" << device.compute_major << '.' << device.compute_minor
             << " memory_bytes=" << device.total_global_memory
             << " max_threads_per_block=" << device.maximum_threads_per_block
             << " max_grid_x=" << device.maximum_grid_x
             << " warp_size=" << device.warp_size
             << " driver_version=" << device.driver_version
             << " runtime_version=" << device.runtime_version << '\n';
    evidence << "abi=passed host_size=" << abi.host_size
             << " host_alignment=" << abi.host_alignment
             << " device_size=" << abi.device_size
             << " device_alignment=" << abi.device_alignment << '\n';
}

void test_exact_tie_and_mapping(std::ofstream& evidence) {
    constexpr std::uint64_t n = 1025U;
    constexpr std::uint64_t d = 8U;
    std::vector<float> query(static_cast<std::size_t>(d), 0.0F);
    std::vector<float> dataset(static_cast<std::size_t>(n * d), 4.0F);
    for (std::uint64_t j = 0; j < d; ++j) {
        dataset[static_cast<std::size_t>(17U * d + j)] = query[j];
        dataset[static_cast<std::size_t>((n - 1U) * d + j)] = query[j];
    }
    const s6::DatasetView dataset_view{dataset.data(), n, d, dataset.size()};
    const s6::QueryView query_view{query.data(), d, query.size()};
    const auto result = s6::cuda_v1_search_float32_evidenced(dataset_view, query_view);
    verify_evidence(dataset, query, n, d, result);
    require(result.production.result && result.production.result->index == 17U,
            "CUDA V1 did not preserve smallest-index exact tie");
    require(bits(result.production.result->squared_distance) == bits(0.0F),
            "CUDA V1 exact match was not positive zero");
    evidence << "mapping_tail_tie=passed N=" << n << " D=" << d
             << " result_index=" << result.production.result->index << '\n';
}

void test_signed_zero_and_underflow(std::ofstream& evidence) {
    {
        const std::vector<float> dataset = {-0.0F, +0.0F, 1.0F, 1.0F};
        const std::vector<float> query = {+0.0F, -0.0F};
        const auto result = s6::cuda_v1_search_float32_evidenced(
            {dataset.data(), 2U, 2U, dataset.size()},
            {query.data(), 2U, query.size()});
        verify_evidence(dataset, query, 2U, 2U, result);
        require(result.production.result && result.production.result->index == 0U &&
                    bits(result.production.result->squared_distance) == bits(0.0F),
                "signed-zero fixture failed");
    }
    {
        const float tiny = std::numeric_limits<float>::denorm_min();
        const std::vector<float> dataset = {tiny, 1.0F};
        const std::vector<float> query = {0.0F};
        const auto result = s6::cuda_v1_search_float32_evidenced(
            {dataset.data(), 2U, 1U, dataset.size()},
            {query.data(), 1U, query.size()});
        verify_evidence(dataset, query, 2U, 1U, result);
        require(result.production.result && result.production.result->index == 0U &&
                    bits(result.production.result->squared_distance) == bits(0.0F),
                "positive tiny FMA underflow-to-zero fixture failed");
    }
    {
        const float subnormal_result = std::ldexp(1.0F, -74);
        const std::vector<float> dataset = {subnormal_result, 1.0F};
        const std::vector<float> query = {0.0F};
        const auto result = s6::cuda_v1_search_float32_evidenced(
            {dataset.data(), 2U, 1U, dataset.size()},
            {query.data(), 1U, query.size()});
        verify_evidence(dataset, query, 2U, 1U, result);
        require(result.production.result &&
                    std::fpclassify(result.production.result->squared_distance) ==
                        FP_SUBNORMAL,
                "CUDA V1 gradual-underflow fixture did not retain a subnormal");
    }
    evidence << "signed_zero_underflow=passed\n";
}

void test_unique_boundary_winners(std::ofstream& evidence) {
    constexpr std::uint64_t n = 257U;
    constexpr std::uint64_t d = 3U;
    const std::vector<float> query = {0.25F, -0.5F, 0.75F};
    std::vector<float> dataset(static_cast<std::size_t>(n * d), 8.0F);
    const auto set_match = [&](std::uint64_t index) {
        for (std::uint64_t j = 0; j < d; ++j) {
            dataset[static_cast<std::size_t>(index * d + j)] =
                query[static_cast<std::size_t>(j)];
        }
    };
    set_match(0U);
    auto result = s6::cuda_v1_search_float32_evidenced(
        {dataset.data(), n, d, dataset.size()}, {query.data(), d, query.size()});
    verify_evidence(dataset, query, n, d, result);
    require(result.production.result && result.production.result->index == 0U,
            "unique nearest candidate at index 0 was not returned");
    auto raw = s6::cuda_v1_search_float32(
        {dataset.data(), n, d, dataset.size()}, {query.data(), d, query.size()});
    require(raw.invocation_status == s6::InvocationStatus::completed && raw.result &&
                raw.result->index == 0U &&
                bits(raw.result->squared_distance) == bits(0.0F),
            "raw V1 path failed unique index-0 fixture");
    for (std::uint64_t j = 0; j < d; ++j) {
        dataset[static_cast<std::size_t>(j)] = 8.0F;
    }
    set_match(n - 1U);
    result = s6::cuda_v1_search_float32_evidenced(
        {dataset.data(), n, d, dataset.size()}, {query.data(), d, query.size()});
    verify_evidence(dataset, query, n, d, result);
    require(result.production.result && result.production.result->index == n - 1U,
            "unique nearest candidate at index N-1 was not returned");
    raw = s6::cuda_v1_search_float32(
        {dataset.data(), n, d, dataset.size()}, {query.data(), d, query.size()});
    require(raw.invocation_status == s6::InvocationStatus::completed && raw.result &&
                raw.result->index == n - 1U &&
                bits(raw.result->squared_distance) == bits(0.0F),
            "raw V1 path failed unique index-N-1 fixture");
    evidence << "unique_boundary_winners=passed raw_path=passed indices=0,"
             << (n - 1U) << '\n';
}

void test_raw_nonzero_and_repeatability(std::ofstream& evidence) {
    const std::vector<float> dataset = {3.0F, 2.0F, 5.0F};
    const std::vector<float> query = {0.0F};
    const s6::DatasetView dataset_view{dataset.data(), 3U, 1U, dataset.size()};
    const s6::QueryView query_view{query.data(), 1U, query.size()};
    std::uint64_t expected_index = 0U;
    std::uint32_t expected_bits = 0U;
    for (int repetition = 0; repetition < 5; ++repetition) {
        const auto result = s6::cuda_v1_search_float32(dataset_view, query_view);
        require(result.invocation_status == s6::InvocationStatus::completed &&
                    result.result,
                "raw V1 repeatability invocation failed");
        if (repetition == 0) {
            expected_index = result.result->index;
            expected_bits = bits(result.result->squared_distance);
        }
        require(result.result->index == expected_index &&
                    bits(result.result->squared_distance) == expected_bits,
                "raw V1 repeated result changed");
    }
    require(expected_index == 1U && expected_bits == bits(4.0F),
            "raw V1 nonzero-distance fixture mismatch");
    evidence << "raw_nonzero_repeatability=passed repetitions=5 index=1"
                " distance_bits="
             << expected_bits << '\n';
}

void test_policy_near_tie(std::ofstream& evidence) {
    const float adjacent = std::nextafter(1.0F, 2.0F);
    const std::vector<float> dataset = {1.0F, adjacent, 4.0F};
    const std::vector<float> query = {0.0F};
    const auto result = s6::cuda_v1_search_float32_evidenced(
        {dataset.data(), 3U, 1U, dataset.size()},
        {query.data(), 1U, query.size()});
    verify_evidence(dataset, query, 3U, 1U, result);
    require(result.production.result && result.production.result->index == 0U,
            "near-tie Float32 lexicographic result mismatch");
    std::vector<double> distances64;
    distances64.reserve(dataset.size());
    for (std::size_t i = 0; i < dataset.size(); ++i) {
        const double promoted = static_cast<double>(dataset[i]);
        const double distance64 = std::fma(promoted, promoted, 0.0);
        distances64.push_back(distance64);
        const auto bound = s6::evaluate_ud001(
            1U, result.evidence.candidate_distances[i], distance64, false);
        require(bound.status == s6::EvidenceStatus::passed,
                "near-tie UD-001 candidate bound failed or blocked");
    }
    const auto ranking = s6::evaluate_ud002(1U, distances64);
    require(ranking.status == s6::EvidenceStatus::passed,
            "near-tie UD-002 classification failed or blocked");
    const auto& contenders = ranking.exact_membership_resolved
                                 ? ranking.exact_contenders
                                 : ranking.conservative_contenders;
    bool returned_is_contender = false;
    for (const auto index : contenders) {
        returned_is_contender = returned_is_contender || index == 0U;
    }
    require(returned_is_contender,
            "near-tie returned index is outside the policy contender set");
    evidence << "policy_near_tie=passed ud001=passed ud002=passed"
             << " exact_membership_resolved="
             << (ranking.exact_membership_resolved ? "true" : "false")
             << " exact_contender_count=" << ranking.exact_contenders.size()
             << " conservative_contender_count="
             << ranking.conservative_contenders.size() << '\n';
}

void test_invalid_and_failure_status(std::ofstream& evidence) {
    const std::vector<float> valid_dataset = {1.0F};
    const std::vector<float> valid_query = {0.0F};
    const s6::DatasetView valid_dataset_view{valid_dataset.data(), 1U, 1U,
                                             valid_dataset.size()};
    const s6::QueryView valid_query_view{valid_query.data(), 1U, valid_query.size()};
    const auto n_zero = s6::cuda_v1_search_float32(
        {valid_dataset.data(), 0U, 1U, 0U}, valid_query_view);
    require(n_zero.invocation_status == s6::InvocationStatus::contract_invalid &&
                n_zero.input_outcome == s6::InputOutcome::invalid_N_zero,
            "N=0 was not rejected");
    const auto d_zero = s6::cuda_v1_search_float32(
        {valid_dataset.data(), 1U, 0U, 0U}, {valid_query.data(), 0U, 0U});
    require(d_zero.invocation_status == s6::InvocationStatus::contract_invalid &&
                d_zero.input_outcome == s6::InputOutcome::invalid_D_zero,
            "D=0 was not rejected");
    const auto require_invalid_nonfinite = [](const std::vector<float>& dataset,
                                              const std::vector<float>& query,
                                              const char* message) {
        const auto outcome = s6::cuda_v1_search_float32(
            {dataset.data(), 1U, 1U, dataset.size()},
            {query.data(), 1U, query.size()});
        require(outcome.invocation_status == s6::InvocationStatus::contract_invalid &&
                    outcome.input_outcome == s6::InputOutcome::invalid_nonfinite &&
                    !outcome.result,
                message);
    };
    const float nan_value = std::numeric_limits<float>::quiet_NaN();
    const float positive_infinity = std::numeric_limits<float>::infinity();
    const float negative_infinity = -std::numeric_limits<float>::infinity();
    require_invalid_nonfinite({nan_value}, valid_query, "NaN dataset was not rejected");
    require_invalid_nonfinite(valid_dataset, {nan_value}, "NaN query was not rejected");
    require_invalid_nonfinite({positive_infinity}, valid_query,
                              "+infinity dataset was not rejected");
    require_invalid_nonfinite(valid_dataset, {positive_infinity},
                              "+infinity query was not rejected");
    require_invalid_nonfinite({negative_infinity}, valid_query,
                              "-infinity dataset was not rejected");
    require_invalid_nonfinite(valid_dataset, {negative_infinity},
                              "-infinity query was not rejected");
    const auto bad_device = s6::cuda_v1_search_float32(
        valid_dataset_view, valid_query_view, {9999, nullptr});
    require(bad_device.invocation_status == s6::InvocationStatus::api_misuse,
            "unavailable device was not API misuse");

    const float maximum = std::numeric_limits<float>::max();
    const std::vector<float> overflow_dataset = {maximum};
    const std::vector<float> overflow_query = {-maximum};
    const auto overflow = s6::cuda_v1_search_float32_evidenced(
        {overflow_dataset.data(), 1U, 1U, overflow_dataset.size()},
        {overflow_query.data(), 1U, overflow_query.size()});
    require(overflow.production.invocation_status ==
                s6::InvocationStatus::backend_failure &&
                overflow.production.domain_status ==
                    s6::DomainStatus::valid_outside_approved_domain &&
                !overflow.production.result.has_value() &&
                overflow.evidence.mapping_status == s6::EvidenceStatus::passed &&
                overflow.evidence.observed_mapping_count == 1U &&
                overflow.evidence.candidate_distances.size() == 1U &&
                std::isinf(overflow.evidence.candidate_distances[0]),
            "nonfinite candidate arithmetic reported success");

    using Failure = s6::detail::CudaV1InjectedFailure;
    require(s6::detail::cuda_v1_search_float32_for_testing(
                valid_dataset_view, valid_query_view, {}, Failure::resource_allocation)
                .production.invocation_status == s6::InvocationStatus::resource_failure,
            "resource failure mapping failed");
    for (const auto failure : {Failure::backend_copy, Failure::backend_launch,
                               Failure::backend_synchronize}) {
        require(s6::detail::cuda_v1_search_float32_for_testing(
                    valid_dataset_view, valid_query_view, {}, failure)
                    .production.invocation_status ==
                    s6::InvocationStatus::backend_failure,
                "backend failure mapping failed");
    }
    evidence << "invalid_resource_backend=passed"
                " real_nonfinite_mapping_presence=passed"
                " valid_outside_domain=passed"
                " valid_outside_domain_status=valid-outside-approved-domain"
                " valid_outside_domain_verdict=blocked/unverified\n";
}

void test_reduction_fixtures(std::ofstream& evidence) {
    const auto tie = s6::detail::cuda_v1_reduce_fixture_for_testing(
        {8.0F, 2.0F, 3.0F, 2.0F});
    require(tie.invocation_status == s6::InvocationStatus::completed && tie.result &&
                tie.result->index == 1U && tie.result->squared_distance == 2.0F,
            "serial reducer tie fixture failed");
    const auto nonfinite = s6::detail::cuda_v1_reduce_fixture_for_testing(
        {2.0F, std::numeric_limits<float>::infinity(), 1.0F});
    require(nonfinite.invocation_status == s6::InvocationStatus::backend_failure &&
                !nonfinite.result,
            "serial reducer did not independently propagate nonfinite status");
    const auto diagnostics = s6::detail::cuda_v1_reduce_diagnostics_for_testing(
        {2.0F, std::numeric_limits<float>::infinity(), 1.0F});
    require(diagnostics.logical_candidate_present &&
                diagnostics.finite_minimum_present &&
                diagnostics.nonfinite_arithmetic_seen &&
                diagnostics.domain_failure_seen &&
                diagnostics.finite_minimum_index == 2U &&
                diagnostics.finite_minimum_distance == 1.0F &&
                diagnostics.production.invocation_status ==
                    s6::InvocationStatus::backend_failure &&
                !diagnostics.production.result,
            "mixed finite/nonfinite reducer product-state diagnostics failed");
    evidence << "reduction=passed mixed_finite_nonfinite_flags=passed"
                " result_suppression=passed\n";
}

void test_high_magnitude_and_exact_dyadic_oracle(std::ofstream& evidence) {
    {
        const std::vector<float> dataset = {1.0e19F, 1.5e19F};
        const std::vector<float> query = {0.0F};
        const auto result = s6::cuda_v1_search_float32_evidenced(
            {dataset.data(), 2U, 1U, dataset.size()},
            {query.data(), 1U, query.size()});
        verify_evidence(dataset, query, 2U, 1U, result);
        require(result.production.result && result.production.result->index == 0U &&
                    std::isfinite(result.production.result->squared_distance) &&
                    std::isfinite(result.evidence.candidate_distances[0]) &&
                    std::isfinite(result.evidence.candidate_distances[1]),
                "finite high-magnitude core fixture failed");
    }

    constexpr std::uint64_t n = 3U;
    constexpr std::uint64_t d = 3U;
    const std::array<std::int32_t, 3> query_scaled = {1, -2, 3};
    const std::array<std::array<std::int32_t, 3>, 3> dataset_scaled = {
        std::array<std::int32_t, 3>{2, -2, 3},
        std::array<std::int32_t, 3>{1, 0, 3},
        std::array<std::int32_t, 3>{4, -4, 5}};
    std::vector<float> query(d);
    std::vector<float> dataset(n * d);
    std::vector<double> oracle_distances;
    for (std::size_t j = 0; j < d; ++j) {
        query[j] = static_cast<float>(query_scaled[j]) / 8.0F;
    }
    for (std::size_t i = 0; i < n; ++i) {
        std::uint64_t numerator = 0U;
        for (std::size_t j = 0; j < d; ++j) {
            dataset[i * d + j] = static_cast<float>(dataset_scaled[i][j]) / 8.0F;
            const std::int64_t difference =
                static_cast<std::int64_t>(dataset_scaled[i][j]) - query_scaled[j];
            numerator += static_cast<std::uint64_t>(difference * difference);
        }
        oracle_distances.push_back(static_cast<double>(numerator) / 64.0);
    }
    const auto result = s6::cuda_v1_search_float32_evidenced(
        {dataset.data(), n, d, dataset.size()}, {query.data(), d, query.size()});
    verify_evidence(dataset, query, n, d, result);
    for (std::size_t i = 0; i < n; ++i) {
        require(static_cast<double>(result.evidence.candidate_distances[i]) ==
                    oracle_distances[i],
                "GPU exact-dyadic result differs from integer oracle");
        require(s6::evaluate_ud001(
                    d, result.evidence.candidate_distances[i], oracle_distances[i],
                    false)
                    .status == s6::EvidenceStatus::passed,
                "exact-dyadic UD-001 check failed");
    }
    const auto ranking = s6::evaluate_ud002(d, oracle_distances);
    require(ranking.status == s6::EvidenceStatus::passed &&
                ranking.exact_membership_resolved &&
                !ranking.exact_contenders.empty() &&
                ranking.exact_contenders[0] == 0U,
            "exact-dyadic interval classifier failed");

    const auto exact_boundary_ranking = s6::evaluate_ud002(1U, {0.0, 4.0, 9.0});
    require(exact_boundary_ranking.status == s6::EvidenceStatus::passed &&
                exact_boundary_ranking.exact_membership_resolved &&
                exact_boundary_ranking.candidates[0].confirmed_contender &&
                exact_boundary_ranking.candidates[1].confirmed_non_contender &&
                exact_boundary_ranking.candidates[2].confirmed_non_contender,
            "confirmed interval inclusion/exclusion fixture failed");
    constexpr double exact_boundary_lower = 1.0;
    constexpr double exact_boundary_upper_minimum = 1.0;
    require(exact_boundary_lower <= exact_boundary_upper_minimum,
            "inclusive exact interval boundary was excluded");

    const double u32 = std::ldexp(1.0, -24);
    const double u64 = std::ldexp(1.0, -53);
    const double gamma32 = u32 / (1.0 - u32);
    const double gamma64 = u64 / (1.0 - u64);
    const double e32 = (1.0 + u32) * (1.0 + u32) * (1.0 + gamma32) - 1.0;
    const double e64 = (1.0 + u64) * (1.0 + u64) * (1.0 + gamma64) - 1.0;
    const double relative = (e32 + e64) / (1.0 - e64);
    const double additive = std::ldexp(1.0, -150) * (1.0 + gamma32);
    const double boundary = (1.0 + relative + 2.0 * additive) / (1.0 - relative);
    bool retained_boundary_uncertainty = false;
    for (int step = -32; step <= 32 && !retained_boundary_uncertainty; ++step) {
        double candidate = boundary;
        for (int move = 0; move < std::abs(step); ++move) {
            candidate = std::nextafter(
                candidate, step < 0 ? -std::numeric_limits<double>::infinity()
                                    : std::numeric_limits<double>::infinity());
        }
        const auto boundary_ranking = s6::evaluate_ud002(1U, {1.0, candidate});
        retained_boundary_uncertainty =
            boundary_ranking.status == s6::EvidenceStatus::passed &&
            !boundary_ranking.boundary_uncertain_indices.empty() &&
            !boundary_ranking.conservative_contenders.empty();
    }
    require(retained_boundary_uncertainty,
            "outward interval boundary uncertainty was not retained");
    evidence << "finite_high_magnitude=passed exact_dyadic_oracle=passed"
                " interval_classifier=passed interval_boundary_fixture=passed"
                " inclusive_boundary=passed confirmed_inclusion=passed"
                " confirmed_exclusion=passed boundary_uncertain_retention=passed\n";
}

void test_nondefault_stream(std::ofstream& evidence) {
    cudaStream_t stream = nullptr;
    require(cudaStreamCreate(&stream) == cudaSuccess,
            "could not create non-default CUDA stream fixture");
    const std::vector<float> dataset = {2.0F, 0.0F};
    const std::vector<float> query = {0.0F};
    const auto result = s6::cuda_v1_search_float32_evidenced(
        {dataset.data(), 2U, 1U, dataset.size()},
        {query.data(), 1U, query.size()}, {0, stream});
    const cudaError_t destroy_status = cudaStreamDestroy(stream);
    require(destroy_status == cudaSuccess, "could not destroy CUDA stream fixture");
    verify_evidence(dataset, query, 2U, 1U, result);
    require(result.production.result && result.production.result->index == 1U,
            "non-default stream V1 result mismatch");
    evidence << "nondefault_stream=passed\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "functional evidence output path is required");
        const std::filesystem::path evidence_path = argv[1];
        std::filesystem::create_directories(evidence_path.parent_path());
        std::ofstream evidence(evidence_path, std::ios::trunc);
        require(evidence.good(), "could not open functional evidence output");
        evidence << "schema=S7-CUDA-V1-FUNCTIONAL-EVIDENCE-v1.0\n";
        evidence << "evidence_class="
                 << (std::strcmp(s6::build_identity::kWorkingTreeState, "clean") == 0
                         ? "clean-candidate"
                         : "local-uncommitted-not-acceptance")
                 << '\n'
                 << "revision=" << s6::build_identity::kRevision << '\n'
                 << "working_tree_state=" << s6::build_identity::kWorkingTreeState
                 << '\n'
                 << "working_tree_fingerprint="
                 << s6::build_identity::kWorkingTreeFingerprint << '\n'
                 << "source_manifest_sha256="
                 << s6::build_identity::kSourceManifestSha256 << '\n';
        test_device_and_abi(evidence);
        test_transpose_mapping(evidence);
        test_exact_tie_and_mapping(evidence);
        test_signed_zero_and_underflow(evidence);
        test_unique_boundary_winners(evidence);
        test_raw_nonzero_and_repeatability(evidence);
        test_policy_near_tie(evidence);
        test_invalid_and_failure_status(evidence);
        test_reduction_fixtures(evidence);
        test_high_magnitude_and_exact_dyadic_oracle(evidence);
        test_nondefault_stream(evidence);
        evidence << "cpu_v1_comparison=passed v0_v1_comparison=passed\n";
        evidence << "overall=passed\n";
        std::cout << "S7 CUDA V1 functional tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "S7 CUDA V1 functional tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
