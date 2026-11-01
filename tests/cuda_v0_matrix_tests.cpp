#include "s6/cuda_v0.hpp"
#include "s6/cpu_build_identity.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

constexpr std::uint64_t kSeed = 0x6a09e667f3bcc909ULL;

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

float independent_float32_distance(const float* candidate, const float* query,
                                   std::size_t dimension) {
    float accumulator = 0.0F;
    for (std::size_t j = 0; j < dimension; ++j) {
        volatile float difference = candidate[j] - query[j];
        accumulator = std::fma(difference, difference, accumulator);
    }
    return accumulator;
}

double independent_float64_distance(const float* candidate, const float* query,
                                    std::size_t dimension) {
    double accumulator = 0.0;
    for (std::size_t j = 0; j < dimension; ++j) {
        volatile double difference = static_cast<double>(candidate[j]) -
                                     static_cast<double>(query[j]);
        accumulator = std::fma(difference, difference, accumulator);
    }
    return accumulator;
}

void run_case(std::uint64_t n, std::uint64_t d, std::ofstream& evidence) {
    std::vector<float> query(static_cast<std::size_t>(d));
    for (std::uint64_t j = 0; j < d; ++j) {
        const auto centered = static_cast<int>((j + (kSeed & 15U)) % 17U) - 8;
        query[static_cast<std::size_t>(j)] = static_cast<float>(centered) / 16.0F;
    }
    std::vector<float> dataset(static_cast<std::size_t>(n * d));
    for (std::uint64_t i = 0; i < n; ++i) {
        const auto offset = static_cast<std::size_t>(i * d);
        for (std::uint64_t j = 0; j < d; ++j) {
            const std::uint64_t mixed = i * 17U + j * 13U + (kSeed & 255U);
            const auto centered = static_cast<int>(mixed % 257U) - 128;
            dataset[offset + static_cast<std::size_t>(j)] =
                static_cast<float>(centered) / 64.0F;
        }
        dataset[offset] = query[0] + static_cast<float>((i % 31U) + 1U) / 64.0F;
    }
    const std::uint64_t expected_index = n / 2U;
    const auto expected_offset = static_cast<std::size_t>(expected_index * d);
    for (std::uint64_t j = 0; j < d; ++j) {
        dataset[expected_offset + static_cast<std::size_t>(j)] =
            query[static_cast<std::size_t>(j)];
    }

    const s6::DatasetView dataset_view{dataset.data(), n, d, dataset.size()};
    const s6::QueryView query_view{query.data(), d, query.size()};
    const auto gpu = s6::cuda_v0_search_float32_evidenced(dataset_view, query_view);
    require(gpu.production.invocation_status == s6::InvocationStatus::completed &&
                gpu.production.result,
            "matrix CUDA invocation failed");
    require(gpu.evidence.mapping_status == s6::EvidenceStatus::passed &&
                gpu.evidence.overwrite_status == s6::EvidenceStatus::passed &&
                gpu.evidence.observed_mapping_count == n,
            "matrix production mapping/overwrite evidence failed");
    require(gpu.evidence.candidate_distances.size() == static_cast<std::size_t>(n),
            "matrix CUDA candidate evidence count mismatch");

    std::vector<double> distances64(static_cast<std::size_t>(n));
    std::uint64_t independent_index = 0U;
    float independent_minimum = 0.0F;
    bool have_minimum = false;
    for (std::uint64_t i = 0; i < n; ++i) {
        const auto offset = static_cast<std::size_t>(i * d);
        const float distance32 = independent_float32_distance(
            dataset.data() + offset, query.data(), static_cast<std::size_t>(d));
        const double distance64 = independent_float64_distance(
            dataset.data() + offset, query.data(), static_cast<std::size_t>(d));
        distances64[static_cast<std::size_t>(i)] = distance64;
        require(bits(gpu.evidence.candidate_distances[static_cast<std::size_t>(i)]) ==
                    bits(distance32),
                "matrix GPU distance differs from independent Float32 graph");
        const bool exact_match = distance64 == 0.0;
        const auto bound = s6::evaluate_ud001(d, distance32, distance64, exact_match);
        require(bound.status == s6::EvidenceStatus::passed,
                "matrix UD-001 candidate bound failed or blocked");
        if (!have_minimum || distance32 < independent_minimum ||
            (distance32 == independent_minimum && i < independent_index)) {
            independent_minimum = distance32;
            independent_index = i;
            have_minimum = true;
        }
    }
    const auto ranking = s6::evaluate_ud002(d, distances64);
    require(ranking.status == s6::EvidenceStatus::passed &&
                ranking.exact_membership_resolved &&
                ranking.exact_contenders.size() == 1U &&
                ranking.exact_contenders[0] == expected_index,
            "matrix UD-002 classification was not an exact singleton");
    require(independent_index == expected_index &&
                gpu.production.result->index == expected_index &&
                bits(gpu.production.result->squared_distance) == bits(independent_minimum),
            "matrix Float32 lexicographic result mismatch");

    evidence << "case=N" << n << "-D" << d << " seed=" << kSeed
             << " mapping=passed overwrite=passed independent_f32=passed"
             << " ud001=passed ud002=passed"
             << " numerical_pair_classification=exact-singleton"
             << " graph_environment_status=blocked"
             << " nn_verdict=blocked/unverified"
             << " result_index=" << gpu.production.result->index
             << " result_distance_bits="
             << bits(gpu.production.result->squared_distance) << '\n';
    std::cout << "CUDA V0 matrix N=" << n << " D=" << d << ": PASS\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "matrix evidence output path is required");
        const std::filesystem::path evidence_path = argv[1];
        std::filesystem::create_directories(evidence_path.parent_path());
        std::ofstream evidence(evidence_path, std::ios::trunc);
        require(evidence.good(), "could not open matrix evidence output");
        evidence << "schema=S7-CUDA-V0-MATRIX-EVIDENCE-v1.0\n";
        evidence << "evidence_class="
                 << (std::strcmp(s6::build_identity::kWorkingTreeState, "clean") == 0
                         ? "clean-candidate"
                         : "local-uncommitted-not-acceptance")
                 << '\n';
        constexpr std::array<std::uint64_t, 4> candidate_counts = {
            1024U, 16384U, 262144U, 1048576U};
        constexpr std::array<std::uint64_t, 3> dimensions = {8U, 32U, 128U};
        for (const auto n : candidate_counts) {
            for (const auto d : dimensions) {
                run_case(n, d, evidence);
            }
        }
        evidence << "matrix_functional_numerical_comparisons=passed cases=12\n";
        evidence << "matrix_nn_acceptance=blocked/unverified"
                    " reason=independent-gpu-graph-environment-evidence-incomplete\n";
        std::cout << "S7 CUDA V0 required 12-case matrix: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "S7 CUDA V0 required matrix: FAIL: " << error.what() << '\n';
        return 1;
    }
}
