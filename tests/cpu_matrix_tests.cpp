#include "s6/cpu_baseline.hpp"
#include "s6/cpu_evidence.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

constexpr std::uint64_t kSeed = 0x6a09e667f3bcc909ULL;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const char* verdict_token(s6::NnVerdict verdict) {
    switch (verdict) {
        case s6::NnVerdict::accepted_unambiguous:
            return "accepted-unambiguous";
        case s6::NnVerdict::accepted_ambiguous:
            return "accepted-ambiguous";
        case s6::NnVerdict::failed:
            return "failed";
        case s6::NnVerdict::blocked_unverified:
            return "blocked-unverified";
        case s6::NnVerdict::not_applicable:
            return "not-applicable";
    }
    return "blocked-unverified";
}

void run_case(std::uint64_t n, std::uint64_t d,
              const s6::CpuBuildEvidence& build_evidence,
              const s6::MandatoryGateEvidence& gates,
              const std::filesystem::path& report_root) {
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

    std::vector<std::uint64_t> mathematical_numerators(
        static_cast<std::size_t>(n));
    for (std::uint64_t i = 0; i < n; ++i) {
        const auto offset = static_cast<std::size_t>(i * d);
        std::uint64_t numerator = 0;
        for (std::uint64_t j = 0; j < d; ++j) {
            const auto dataset_scaled = static_cast<std::int64_t>(std::llround(
                static_cast<double>(dataset[offset + static_cast<std::size_t>(j)]) *
                64.0));
            const auto query_scaled = static_cast<std::int64_t>(std::llround(
                static_cast<double>(query[static_cast<std::size_t>(j)]) * 64.0));
            const auto difference = dataset_scaled - query_scaled;
            numerator += static_cast<std::uint64_t>(difference * difference);
        }
        mathematical_numerators[static_cast<std::size_t>(i)] = numerator;
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
    validation_options.mandatory_gate_evidence = &gates;
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
    require(report.numerical_paths.production_float32_graph ==
                    s6::EvidenceStatus::passed &&
                report.numerical_paths.validator_float32_graph ==
                    s6::EvidenceStatus::passed &&
                report.numerical_paths.reference_float64_graph ==
                    s6::EvidenceStatus::passed &&
                report.numerical_paths.validator_float64_graph ==
                    s6::EvidenceStatus::passed,
            "matrix did not verify all four numerical graphs");
    require(report.ud001_status == s6::EvidenceStatus::passed,
            "matrix UD-001 failed or blocked");
    require(report.ud002_status == s6::EvidenceStatus::passed,
            "matrix UD-002 failed or blocked");
    require(report.production_candidate_coverage_status == s6::EvidenceStatus::passed &&
                report.production_candidate_coverage_provenance ==
                    s6::EvidenceProvenance::production_instrumentation,
            "matrix production coverage evidence did not pass");
    require(report.nn_verdict != s6::NnVerdict::failed &&
                report.nn_verdict != s6::NnVerdict::blocked_unverified &&
                report.nn_verdict != s6::NnVerdict::not_applicable &&
                report.exact_ambiguity_known && !report.exact_ambiguity &&
                report.ranking.exact_membership_resolved &&
                report.ranking.exact_contenders.size() == 1U &&
                report.float64_index_agrees,
            "matrix did not derive an evidence-qualified singleton outcome");
    require(report.independently_computed_float32_distances.size() ==
                static_cast<std::size_t>(n),
            "matrix independent Float32 coverage mismatch");
    require(report.independently_computed_float32_argmin_index.has_value(),
            "matrix independent Float32 argmin missing");

    const auto case_id = "cpu-N" + std::to_string(n) + "-D" + std::to_string(d);
    const auto report_summary = s6::write_cpu_matrix_case_report(
        report_root.string(), case_id, kSeed, dataset_view, query_view,
        build_evidence, evidenced, reference, report, gates,
        mathematical_numerators, -12);
    require(report_summary.status == s6::EvidenceStatus::passed,
            "matrix structured evidence report is incomplete");

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
              << " report_bytes=" << report_summary.candidate_bytes
              << " report_sha256=" << report_summary.candidate_sha256
              << " verdict=" << verdict_token(report.nn_verdict) << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 4,
                "matrix manifest, fixture evidence, and report root are required");
        const auto build_evidence = s6::verify_cpu_build_evidence(argv[1]);
        require(build_evidence.identity_status() == s6::EvidenceStatus::passed,
                "matrix build evidence identity failed");
        require(build_evidence.graph_status() == s6::EvidenceStatus::passed,
                "matrix graph inspection failed");
        std::string fixture_detail;
        const auto gates = s6::load_cpu_fixture_evidence(
            argv[2], build_evidence.source_identity(), fixture_detail);
        require(gates.fixture_status == s6::EvidenceStatus::passed &&
                    gates.oracle_status == s6::EvidenceStatus::passed &&
                    gates.tie_status == s6::EvidenceStatus::passed &&
                    gates.reduction_status == s6::EvidenceStatus::passed,
                "matrix mandatory fixture evidence failed or is unbound");
        constexpr std::array<std::uint64_t, 4> candidate_counts = {
            1024U, 16384U, 262144U, 1048576U};
        constexpr std::array<std::uint64_t, 3> dimensions = {8U, 32U, 128U};
        for (const auto n : candidate_counts) {
            for (const auto d : dimensions) {
                run_case(n, d, build_evidence, gates, argv[3]);
            }
        }
        std::cout << "S6 CPU required 12-case matrix: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "S6 CPU required 12-case matrix: FAIL: " << error.what() << '\n';
        return 1;
    }
}
