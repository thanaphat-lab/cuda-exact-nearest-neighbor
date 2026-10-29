#include "s6/cpu_baseline.hpp"
#include "s6/cpu_evidence.hpp"

#include <array>
#include <cstdint>
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

void run_case(std::uint64_t n, std::uint64_t d,
              const s6::CpuBuildEvidence& build_evidence) {
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
    const auto evidenced =
        s6::cpu_search_float32_evidenced(dataset_view, query_view, build_evidence);
    const auto& production = evidenced.production;
    require(production.invocation_status == s6::InvocationStatus::completed,
            "matrix production execution failed");
    require(production.result && production.result->index == expected_index &&
                production.result->squared_distance == 0.0F,
            "matrix production result mismatch");

    const auto reference = s6::compute_float64_reference(dataset_view, query_view);
    require(reference.invocation_status == s6::InvocationStatus::completed,
            "matrix Float64 execution failed");
    require(reference.distances.size() == static_cast<std::size_t>(n),
            "matrix Float64 candidate retention mismatch");

    s6::ValidationOptions validation_options;
    validation_options.production_evidence = &evidenced.evidence;
    const auto report = s6::validate_cpu_search(dataset_view, query_view, production,
                                                reference, validation_options);
    require(report.candidate_coverage_status == s6::EvidenceStatus::passed,
            "matrix candidate coverage failed");
    require(report.evidence_identity_status == s6::EvidenceStatus::passed,
            "matrix evidence identity mismatch");
    require(report.graph_status == s6::EvidenceStatus::passed &&
                report.graph_provenance ==
                    s6::EvidenceProvenance::verified_build_artifact,
            "matrix graph evidence did not pass");
    require(report.ud001_status == s6::EvidenceStatus::passed,
            "matrix UD-001 failed or blocked");
    require(report.ud002_status == s6::EvidenceStatus::passed,
            "matrix UD-002 failed or blocked");
    require(report.production_candidate_coverage_status == s6::EvidenceStatus::passed &&
                report.production_candidate_coverage_provenance ==
                    s6::EvidenceProvenance::production_instrumentation,
            "matrix production coverage evidence did not pass");
    require(report.nn_verdict == s6::NnVerdict::accepted_unambiguous,
            "matrix did not reach evidence-qualified unambiguous acceptance");
    require(report.independently_computed_float32_distances.size() ==
                static_cast<std::size_t>(n),
            "matrix independent Float32 coverage mismatch");
    require(report.independently_computed_float32_argmin_index.has_value(),
            "matrix independent Float32 argmin missing");

    std::cout << "matrix N=" << n << " D=" << d << " seed=" << kSeed
              << " invocation=completed result_index=" << production.result->index
              << " result_distance=" << production.result->squared_distance
              << " independent_f32_index="
              << *report.independently_computed_float32_argmin_index
              << " independent_f32_distance="
              << report.independently_computed_float32_distances[
                     static_cast<std::size_t>(
                         *report.independently_computed_float32_argmin_index)]
              << " f64_index=" << *reference.argmin_index
              << " ud001=passed ud002=passed graph=passed coverage=passed"
              << " workload_sha256=" << evidenced.evidence.workload_identity()
              << " evidence_identity=" << report.evidence_identity
              << " verdict=accepted-unambiguous\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "matrix evidence manifest argument missing");
        const auto build_evidence = s6::verify_cpu_build_evidence(argv[1]);
        require(build_evidence.identity_status() == s6::EvidenceStatus::passed,
                "matrix build evidence identity failed");
        require(build_evidence.graph_status() == s6::EvidenceStatus::passed,
                "matrix graph inspection failed");
        constexpr std::array<std::uint64_t, 4> candidate_counts = {
            1024U, 16384U, 262144U, 1048576U};
        constexpr std::array<std::uint64_t, 3> dimensions = {8U, 32U, 128U};
        for (const auto n : candidate_counts) {
            for (const auto d : dimensions) {
                run_case(n, d, build_evidence);
            }
        }
        std::cout << "S6 CPU required 12-case matrix: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "S6 CPU required 12-case matrix: FAIL: " << error.what() << '\n';
        return 1;
    }
}
