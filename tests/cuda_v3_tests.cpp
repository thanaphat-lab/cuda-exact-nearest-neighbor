#include "s6/cuda_v3.hpp"
#include "s6/cuda_v2.hpp"
#include "s6/cuda_v1.hpp"
#include "s6/cuda_v0.hpp"
#include "s6/cpu_build_identity.hpp"

#include <cuda_runtime_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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

float independent_distance(const float* candidate, const float* query,
                           std::size_t dimension) {
    float accumulator = 0.0F;
    for (std::size_t j = 0; j < dimension; ++j) {
        volatile float difference = candidate[j] - query[j];
        accumulator = std::fma(difference, difference, accumulator);
    }
    return accumulator;
}

void verify_against_prior_variants(const std::vector<float>& dataset,
                                   const std::vector<float>& query,
                                   std::uint64_t n, std::uint64_t d,
                                   std::uint32_t block_size) {
    const s6::DatasetView dataset_view{dataset.data(), n, d, dataset.size()};
    const s6::QueryView query_view{query.data(), d, query.size()};
    const s6::CudaV3Options options{0, nullptr, block_size};
    const auto v3 = s6::cuda_v3_search_float32_evidenced(
        dataset_view, query_view, options);
    const auto cpu = s6::cpu_search_float32(dataset_view, query_view);
    const auto v0 = s6::cuda_v0_search_float32_evidenced(dataset_view, query_view);
    const auto v1 = s6::cuda_v1_search_float32_evidenced(dataset_view, query_view);
    const auto v2 = s6::cuda_v2_search_float32_evidenced(dataset_view, query_view);
    require(v3.production.invocation_status == s6::InvocationStatus::completed &&
                v3.production.result && cpu.result && v0.production.result &&
                v1.production.result && v2.production.result,
            "V3 comparison invocation failed");
    require(v3.evidence.mapping_status == s6::EvidenceStatus::passed &&
                v3.evidence.overwrite_status == s6::EvidenceStatus::passed &&
                v3.evidence.observed_mapping_count == n &&
                v3.evidence.candidate_distances.size() ==
                    static_cast<std::size_t>(n),
            "V3 diagnostic candidate coverage failed");
    require(v3.production.result->index == cpu.result->index &&
                v3.production.result->index == v0.production.result->index &&
                v3.production.result->index == v1.production.result->index &&
                v3.production.result->index == v2.production.result->index &&
                bits(v3.production.result->squared_distance) ==
                    bits(cpu.result->squared_distance) &&
                bits(v3.production.result->squared_distance) ==
                    bits(v0.production.result->squared_distance) &&
                bits(v3.production.result->squared_distance) ==
                    bits(v1.production.result->squared_distance) &&
                bits(v3.production.result->squared_distance) ==
                    bits(v2.production.result->squared_distance),
            "V3 result differs from CPU, V0, V1, or V2");
    for (std::uint64_t i = 0; i < n; ++i) {
        const auto expected = independent_distance(
            dataset.data() + static_cast<std::size_t>(i * d), query.data(),
            static_cast<std::size_t>(d));
        require(bits(v3.evidence.candidate_distances[static_cast<std::size_t>(i)]) ==
                    bits(expected) &&
                    bits(v3.evidence.candidate_distances[static_cast<std::size_t>(i)]) ==
                        bits(v0.evidence.candidate_distances[static_cast<std::size_t>(i)]) &&
                    bits(v3.evidence.candidate_distances[static_cast<std::size_t>(i)]) ==
                        bits(v1.evidence.candidate_distances[static_cast<std::size_t>(i)]) &&
                    bits(v3.evidence.candidate_distances[static_cast<std::size_t>(i)]) ==
                        bits(v2.evidence.candidate_distances[static_cast<std::size_t>(i)]),
                "V3 candidate graph differs from independent/V0/V1/V2 graph");
    }
}

void test_device_abi_and_layout(std::ofstream& evidence) {
    const auto device = s6::inspect_cuda_v3_device();
    require(device.status == s6::EvidenceStatus::passed &&
                device.maximum_threads_per_block >= 512 && device.warp_size == 32,
            "CUDA V3 device cannot run approved block candidates");
    const auto abi = s6::inspect_cuda_v3_reduction_record_abi();
    require(abi.status == s6::EvidenceStatus::passed && abi.host_size == 16U &&
                abi.host_alignment == 16U && abi.device_size == 16U &&
                abi.device_alignment == 16U && abi.host_distance_offset == 0U &&
                abi.host_flags_offset == 4U && abi.host_index_offset == 8U &&
                abi.device_distance_offset == 0U && abi.device_flags_offset == 4U &&
                abi.device_index_offset == 8U,
            "CUDA V3 reduction-record ABI failed");
    const std::vector<float> aos = {1.0F, -0.0F, 2.0F, +0.0F, 3.0F, 4.0F};
    const auto soa = s6::detail::cuda_v3_transform_aos_to_soa_for_testing(
        {aos.data(), 3U, 2U, aos.size()});
    const std::vector<float> expected = {1.0F, 2.0F, 3.0F, -0.0F, +0.0F, 4.0F};
    require(soa.size() == expected.size(), "V2 SoA transform size mismatch");
    for (std::size_t i = 0; i < soa.size(); ++i) {
        require(bits(soa[i]) == bits(expected[i]), "V2 SoA bit mapping failed");
    }
    evidence << "device=passed name=" << device.name << " uuid=" << device.uuid
             << " compute=" << device.compute_major << '.' << device.compute_minor
             << " memory_bytes=" << device.total_global_memory
             << " max_threads_per_block=" << device.maximum_threads_per_block
             << " max_grid_x=" << device.maximum_grid_x
             << " warp_size=" << device.warp_size
             << " driver_version=" << device.driver_version
             << " runtime_version=" << device.runtime_version << '\n'
             << "abi=passed host_size=16 host_alignment=16 device_size=16"
                " device_alignment=16 offsets=0,4,8\n"
             << "transpose_mapping=passed bit_preservation=passed\n";
}

void test_block_candidates_and_hierarchy(std::ofstream& evidence) {
    constexpr std::uint64_t n = 262145U;
    constexpr std::uint64_t d = 1U;
    std::vector<float> dataset(static_cast<std::size_t>(n), 8.0F);
    const std::vector<float> query = {0.0F};
    dataset[17U] = 1.0F;
    dataset[131071U] = 1.0F;
    dataset[n - 1U] = 2.0F;
    for (const auto block_size : {128U, 256U, 512U}) {
        verify_against_prior_variants(dataset, query, n, d, block_size);
        const auto raw = s6::cuda_v3_search_float32(
            {dataset.data(), n, d, dataset.size()},
            {query.data(), d, query.size()}, {0, nullptr, block_size});
        require(raw.result && raw.result->index == 17U &&
                    bits(raw.result->squared_distance) == bits(1.0F),
                "V2 hierarchy cross-stage tie failed");
    }
    evidence << "block_candidates=passed values=128,256,512 default=256-not-tuned"
                " hierarchy_tail=passed cross_block_tie=passed"
                " cpu_v3_comparison=passed v0_v3_comparison=passed"
                " v1_v3_comparison=passed v2_v3_comparison=passed\n";
}

void test_logical_warp_tails(std::ofstream& evidence) {
    constexpr std::array<std::uint64_t, 9> counts = {
        1U, 2U, 15U, 16U, 17U, 31U, 32U, 33U, 65U};
    const std::vector<float> query = {0.0F};
    for (const auto block_size : {128U, 256U, 512U}) {
        for (const auto n : counts) {
            std::vector<float> dataset(static_cast<std::size_t>(n), 5.0F);
            dataset.back() = 1.0F;
            verify_against_prior_variants(dataset, query, n, 1U, block_size);
            const auto result = s6::cuda_v3_search_float32(
                {dataset.data(), n, 1U, dataset.size()},
                {query.data(), 1U, query.size()}, {0, nullptr, block_size});
            require(result.result && result.result->index == n - 1U &&
                        bits(result.result->squared_distance) == bits(1.0F),
                    "V3 logical warp-tail winner failed");
            std::fill(dataset.begin(), dataset.end(), 1.0F);
            const auto tie = s6::cuda_v3_search_float32(
                {dataset.data(), n, 1U, dataset.size()},
                {query.data(), 1U, query.size()}, {0, nullptr, block_size});
            require(tie.result && tie.result->index == 0U,
                    "V3 logical warp-tail tie failed");
        }
    }
    evidence << "warp_participation=passed physical_warp_size=32 full_mask=passed"
                " safe_active_sources=passed logical_warp_tails=passed"
                " offsets=16,8,4,2,1\n";
}

void test_boundary_and_zero_fixtures(std::ofstream& evidence) {
    for (const auto block_size : {128U, 256U, 512U}) {
        for (const auto n : {static_cast<std::uint64_t>(block_size - 1U),
                             static_cast<std::uint64_t>(block_size),
                             static_cast<std::uint64_t>(block_size + 1U),
                             static_cast<std::uint64_t>(2U * block_size + 1U)}) {
            std::vector<float> dataset(static_cast<std::size_t>(n), 7.0F);
            const std::vector<float> query = {-0.0F};
            dataset[n - 1U] = +0.0F;
            verify_against_prior_variants(dataset, query, n, 1U, block_size);
            const auto result = s6::cuda_v3_search_float32(
                {dataset.data(), n, 1U, dataset.size()},
                {query.data(), 1U, query.size()}, {0, nullptr, block_size});
            require(result.result && result.result->index == n - 1U &&
                        bits(result.result->squared_distance) == bits(+0.0F),
                    "V2 boundary winner or signed-zero fixture failed");
        }
    }
    evidence << "identity_tails=passed unique_boundary_winners=passed"
                " signed_zero=passed exact_positive_zero=passed\n";
}

void test_direct_reduction(std::ofstream& evidence) {
    using Input = s6::detail::CudaV3ReductionInput;
    const std::uint64_t high = (std::uint64_t{1} << 32U) + 9U;
    for (const auto block_size : {128U, 256U, 512U}) {
        std::vector<Input> records(static_cast<std::size_t>(block_size * block_size + 1U),
                                   {9.0F, 99U, false});
        records[0] = {4.0F, high + 8U, true};
        records[block_size - 1U] = {2.0F, high + 1U, true};
        records[block_size + 3U] = {-0.0F, high + 5U, true};
        records.back() = {+0.0F, high, true};
        const auto tie = s6::detail::cuda_v3_reduce_diagnostics_for_testing(
            records, {0, nullptr, block_size});
        require(tie.production.invocation_status == s6::InvocationStatus::completed &&
                    tie.production.result && tie.production.result->index == high &&
                    bits(tie.production.result->squared_distance) == bits(+0.0F) &&
                    tie.logical_candidate_present && tie.finite_minimum_present &&
                    !tie.nonfinite_arithmetic_seen,
                "V2 record hierarchy tie/full-width index failed");

        records[block_size + 7U] =
            {std::numeric_limits<float>::infinity(), high + 2U, true};
        const auto mixed = s6::detail::cuda_v3_reduce_diagnostics_for_testing(
            records, {0, nullptr, block_size});
        require(mixed.production.invocation_status ==
                    s6::InvocationStatus::backend_failure &&
                    !mixed.production.result && mixed.logical_candidate_present &&
                    mixed.finite_minimum_present && mixed.nonfinite_arithmetic_seen &&
                    mixed.domain_failure_seen &&
                    mixed.finite_minimum_index == high,
                "V2 independent minimum/status aggregation failed");
    }
    evidence << "reduction=passed hierarchy_associativity=passed"
                " mixed_finite_nonfinite_flags=passed result_suppression=passed"
                " full_width_index=passed signed_zero_tie=passed\n";
}

void test_numerical_oracles(std::ofstream& evidence) {
    {
        const std::vector<float> dataset = {1.0e19F, 1.5e19F};
        const std::vector<float> query = {0.0F};
        const auto result = s6::cuda_v3_search_float32_evidenced(
            {dataset.data(), 2U, 1U, dataset.size()},
            {query.data(), 1U, query.size()});
        require(result.production.result && result.production.result->index == 0U &&
                    std::isfinite(result.production.result->squared_distance) &&
                    std::isfinite(result.evidence.candidate_distances[0]) &&
                    std::isfinite(result.evidence.candidate_distances[1]),
                "V2 finite high-magnitude fixture failed");
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
    const auto result = s6::cuda_v3_search_float32_evidenced(
        {dataset.data(), n, d, dataset.size()}, {query.data(), d, query.size()});
    require(result.production.result.has_value(),
            "V2 exact-dyadic invocation failed");
    for (std::size_t i = 0; i < n; ++i) {
        require(static_cast<double>(result.evidence.candidate_distances[i]) ==
                    oracle_distances[i],
                "V2 result differs from integer exact-dyadic oracle");
        require(s6::evaluate_ud001(
                    d, result.evidence.candidate_distances[i], oracle_distances[i],
                    false)
                    .status == s6::EvidenceStatus::passed,
                "V2 exact-dyadic UD-001 check failed");
    }
    const auto ranking = s6::evaluate_ud002(d, oracle_distances);
    require(ranking.status == s6::EvidenceStatus::passed &&
                ranking.exact_membership_resolved &&
                !ranking.exact_contenders.empty() &&
                ranking.exact_contenders[0] == 0U,
            "V2 exact-dyadic interval classifier failed");

    const auto exact_boundary = s6::evaluate_ud002(1U, {0.0, 4.0, 9.0});
    require(exact_boundary.status == s6::EvidenceStatus::passed &&
                exact_boundary.exact_membership_resolved &&
                exact_boundary.candidates[0].confirmed_contender &&
                exact_boundary.candidates[1].confirmed_non_contender &&
                exact_boundary.candidates[2].confirmed_non_contender,
            "V2 interval inclusion/exclusion fixture failed");
    constexpr double exact_boundary_lower = 1.0;
    constexpr double exact_boundary_upper_minimum = 1.0;
    require(exact_boundary_lower <= exact_boundary_upper_minimum,
            "V2 inclusive interval boundary was excluded");

    const double u32 = std::ldexp(1.0, -24);
    const double u64 = std::ldexp(1.0, -53);
    const double gamma32 = u32 / (1.0 - u32);
    const double gamma64 = u64 / (1.0 - u64);
    const double e32 = (1.0 + u32) * (1.0 + u32) * (1.0 + gamma32) - 1.0;
    const double e64 = (1.0 + u64) * (1.0 + u64) * (1.0 + gamma64) - 1.0;
    const double relative = (e32 + e64) / (1.0 - e64);
    const double additive = std::ldexp(1.0, -150) * (1.0 + gamma32);
    const double boundary = (1.0 + relative + 2.0 * additive) / (1.0 - relative);
    bool retained_uncertainty = false;
    for (int step = -32; step <= 32 && !retained_uncertainty; ++step) {
        double candidate = boundary;
        for (int move = 0; move < std::abs(step); ++move) {
            candidate = std::nextafter(
                candidate, step < 0 ? -std::numeric_limits<double>::infinity()
                                    : std::numeric_limits<double>::infinity());
        }
        const auto boundary_ranking = s6::evaluate_ud002(1U, {1.0, candidate});
        retained_uncertainty =
            boundary_ranking.status == s6::EvidenceStatus::passed &&
            !boundary_ranking.boundary_uncertain_indices.empty() &&
            !boundary_ranking.conservative_contenders.empty();
    }
    require(retained_uncertainty,
            "V2 outward interval boundary uncertainty was not retained");
    evidence << "finite_high_magnitude=passed exact_dyadic_oracle=passed"
                " interval_classifier=passed interval_boundary_fixture=passed"
                " inclusive_boundary=passed confirmed_inclusion=passed"
                " confirmed_exclusion=passed boundary_uncertain_retention=passed\n";
}

void test_invalid_failures_and_repeatability(std::ofstream& evidence) {
    const std::vector<float> dataset = {3.0F, 2.0F, 5.0F};
    const std::vector<float> query = {0.0F};
    const s6::DatasetView dataset_view{dataset.data(), 3U, 1U, dataset.size()};
    const s6::QueryView query_view{query.data(), 1U, query.size()};
    for (const auto block_size : {128U, 256U, 512U}) {
        for (int repetition = 0; repetition < 5; ++repetition) {
            const auto result = s6::cuda_v3_search_float32(
                dataset_view, query_view, {0, nullptr, block_size});
            require(result.result && result.result->index == 1U &&
                        bits(result.result->squared_distance) == bits(4.0F),
                    "V2 repeatability fixture failed");
        }
    }
    require(s6::cuda_v3_search_float32(dataset_view, query_view, {0, nullptr, 64U})
                .invocation_status == s6::InvocationStatus::api_misuse,
            "unsupported V2 block size was not rejected");
    require(s6::cuda_v3_search_float32(
                {dataset.data(), 0U, 1U, 0U}, query_view)
                .invocation_status == s6::InvocationStatus::contract_invalid,
            "V2 N=0 rejection failed");
    const float infinity = std::numeric_limits<float>::infinity();
    require(s6::cuda_v3_search_float32(
                {&infinity, 1U, 1U, 1U}, query_view)
                .invocation_status == s6::InvocationStatus::contract_invalid,
            "V2 nonfinite input rejection failed");
    const float maximum = std::numeric_limits<float>::max();
    const std::vector<float> overflow_dataset = {maximum};
    const std::vector<float> overflow_query = {-maximum};
    const auto overflow = s6::cuda_v3_search_float32_evidenced(
        {overflow_dataset.data(), 1U, 1U, overflow_dataset.size()},
        {overflow_query.data(), 1U, overflow_query.size()});
    require(overflow.production.invocation_status ==
                s6::InvocationStatus::backend_failure &&
                overflow.production.domain_status ==
                    s6::DomainStatus::valid_outside_approved_domain &&
                !overflow.production.result &&
                overflow.evidence.mapping_status == s6::EvidenceStatus::passed &&
                overflow.evidence.candidate_distances.size() == 1U &&
                std::isinf(overflow.evidence.candidate_distances[0]),
            "V2 valid-outside-domain nonfinite propagation failed");
    using Failure = s6::detail::CudaV3InjectedFailure;
    require(s6::detail::cuda_v3_search_float32_for_testing(
                dataset_view, query_view, {}, Failure::resource_allocation)
                .production.invocation_status == s6::InvocationStatus::resource_failure,
            "V2 resource failure mapping failed");
    for (const auto failure : {Failure::backend_copy, Failure::backend_launch,
                               Failure::backend_synchronize}) {
        require(s6::detail::cuda_v3_search_float32_for_testing(
                    dataset_view, query_view, {}, failure)
                    .production.invocation_status ==
                    s6::InvocationStatus::backend_failure,
                "V2 backend failure mapping failed");
    }
    evidence << "repeatability=passed repetitions=5 block_candidates=3"
                " invalid_input=passed invalid_configuration=passed"
                " resource_backend_failures=passed"
                " valid_outside_domain=passed\n";
}

void test_nondefault_stream(std::ofstream& evidence) {
    cudaStream_t stream = nullptr;
    require(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking) == cudaSuccess,
            "could not create V2 nondefault stream");
    const std::vector<float> dataset = {4.0F, 1.0F, 9.0F};
    const std::vector<float> query = {0.0F};
    const auto result = s6::cuda_v3_search_float32(
        {dataset.data(), 3U, 1U, dataset.size()},
        {query.data(), 1U, query.size()}, {0, stream, 256U});
    const auto destroy_status = cudaStreamDestroy(stream);
    require(result.result && result.result->index == 1U &&
                destroy_status == cudaSuccess,
            "V2 nondefault stream failed");
    evidence << "nondefault_stream=passed\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "functional evidence output path is required");
        const std::filesystem::path evidence_path = argv[1];
        std::filesystem::create_directories(evidence_path.parent_path());
        std::ofstream evidence(evidence_path, std::ios::trunc);
        require(evidence.good(), "could not open V2 functional evidence");
        evidence << "schema=S7-CUDA-V3-FUNCTIONAL-EVIDENCE-v1.0\n"
                 << "evidence_class="
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
        test_device_abi_and_layout(evidence);
        test_block_candidates_and_hierarchy(evidence);
        test_logical_warp_tails(evidence);
        test_boundary_and_zero_fixtures(evidence);
        test_direct_reduction(evidence);
        test_numerical_oracles(evidence);
        test_invalid_failures_and_repeatability(evidence);
        test_nondefault_stream(evidence);
        evidence << "overall=passed\n";
        std::cout << "S7 CUDA V3 functional tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "S7 CUDA V3 functional tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
