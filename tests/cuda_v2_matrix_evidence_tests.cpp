#include "s6/cuda_v2.hpp"
#include "s6/cuda_v1.hpp"
#include "s6/cuda_v0.hpp"
#include "s6/cpu_build_identity.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
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

std::uint64_t bits(double value) {
    std::uint64_t result = 0;
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

std::map<std::string, std::string> read_fields(const std::filesystem::path& path) {
    std::ifstream input(path);
    require(input.good(), "could not read V2 binary evidence");
    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        if (separator != std::string::npos) {
            fields.emplace(line.substr(0, separator), line.substr(separator + 1U));
        }
    }
    return fields;
}

void verify_binary_correlation(const std::filesystem::path& path) {
    const auto fields = read_fields(path);
    const auto value = [&](const char* name) -> const std::string& {
        const auto found = fields.find(name);
        require(found != fields.end(), "V2 binary evidence field missing");
        return found->second;
    };
    require(value("schema") == "S7-CUDA-V2-BINARY-EVIDENCE-v1.0",
            "unsupported V2 binary evidence schema");
    require(value("revision") == s6::build_identity::kRevision &&
                value("working_tree_fingerprint") ==
                    s6::build_identity::kWorkingTreeFingerprint &&
                value("source_manifest_sha256") ==
                    s6::build_identity::kSourceManifestSha256,
            "V2 binary/source identity mismatch");
    for (const char* name : {"compile_policy_status",
                             "distance_kernel_symbol_status",
                             "record_kernel_symbol_status",
                             "barrier_instruction_status",
                             "distance_kernel_instruction_status",
                             "distance_kernel_dependency_status",
                             "distance_kernel_default_rn_no_ftz_status",
                             "serial_reducer_absence_status",
                             "binary_graph_structure_status"}) {
        require(value(name) == "passed", "V2 binary/SASS status failed");
    }
}

void verify_functional_correlation(const std::filesystem::path& path) {
    const auto fields = read_fields(path);
    const auto require_value = [&](const char* name, const char* expected) {
        const auto found = fields.find(name);
        require(found != fields.end() && found->second == expected,
                "V2 functional evidence correlation failed");
    };
    require_value("schema", "S7-CUDA-V2-FUNCTIONAL-EVIDENCE-v1.0");
    require_value("revision", s6::build_identity::kRevision);
    require_value("working_tree_fingerprint",
                  s6::build_identity::kWorkingTreeFingerprint);
    require_value("source_manifest_sha256",
                  s6::build_identity::kSourceManifestSha256);
    require_value("overall", "passed");
}

struct CandidateEvidence {
    std::uint64_t index;
    std::uint64_t float64_bits;
    std::uint32_t float32_bits;
    std::uint8_t ud001_passed;
    std::uint8_t exact_zero_required;
    std::uint8_t exact_zero_satisfied;
    std::uint8_t exact_contender;
};
static_assert(sizeof(CandidateEvidence) == 24U, "V2 candidate evidence ABI");

void run_case(std::uint64_t n, std::uint64_t d, std::ofstream& evidence,
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

    const s6::DatasetView dataset_view{dataset.data(), n, d, dataset.size()};
    const s6::QueryView query_view{query.data(), d, query.size()};
    const auto v2 = s6::cuda_v2_search_float32_evidenced(dataset_view, query_view);
    const auto cpu = s6::cpu_search_float32(dataset_view, query_view);
    const auto v0 = s6::cuda_v0_search_float32_evidenced(dataset_view, query_view);
    const auto v1 = s6::cuda_v1_search_float32_evidenced(dataset_view, query_view);
    const auto reference = s6::compute_float64_reference(dataset_view, query_view);
    require(v2.production.result && cpu.result && v0.production.result &&
                v1.production.result && reference.argmin_index,
            "V2 matrix production/reference invocation failed");
    require(v2.evidence.mapping_status == s6::EvidenceStatus::passed &&
                v2.evidence.overwrite_status == s6::EvidenceStatus::passed &&
                v2.evidence.observed_mapping_count == n &&
                v2.evidence.candidate_distances.size() ==
                    static_cast<std::size_t>(n),
            "V2 matrix diagnostic coverage failed");
    const auto environment = s6::inspect_arithmetic_environment();
    require(environment.conforming_float32() && environment.conforming_float64(),
            "V2 matrix validator arithmetic environment failed");

    std::vector<double> distances64(static_cast<std::size_t>(n));
    std::vector<CandidateEvidence> candidate_records(static_cast<std::size_t>(n));
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
        require(bits(distance64) ==
                    bits(reference.distances[static_cast<std::size_t>(i)]),
                "V2 matrix independent Float64 graph mismatch");
        require(bits(v2.evidence.candidate_distances[static_cast<std::size_t>(i)]) ==
                    bits(distance32) &&
                    bits(v2.evidence.candidate_distances[static_cast<std::size_t>(i)]) ==
                        bits(v0.evidence.candidate_distances[static_cast<std::size_t>(i)]) &&
                    bits(v2.evidence.candidate_distances[static_cast<std::size_t>(i)]) ==
                        bits(v1.evidence.candidate_distances[static_cast<std::size_t>(i)]),
                "V2 matrix per-candidate Float32 mismatch");
        const bool exact_zero = i == expected_index;
        const auto bound = s6::evaluate_ud001(d, distance32, distance64, exact_zero);
        require(bound.status == s6::EvidenceStatus::passed,
                "V2 matrix UD-001 failed or blocked");
        candidate_records[static_cast<std::size_t>(i)] = {
            i, bits(distance64), bits(distance32), 1U,
            static_cast<std::uint8_t>(bound.exact_zero_required),
            static_cast<std::uint8_t>(bound.exact_zero_satisfied), 0U};
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
                ranking.exact_contenders ==
                    std::vector<std::uint64_t>{expected_index} &&
                ranking.conservative_contenders ==
                    std::vector<std::uint64_t>{expected_index} &&
                ranking.boundary_uncertain_indices.empty(),
            "V2 matrix UD-002 was not an exact singleton");
    candidate_records[static_cast<std::size_t>(expected_index)].exact_contender = 1U;
    require(independent_index == expected_index &&
                *reference.argmin_index == expected_index &&
                v2.production.result->index == expected_index &&
                cpu.result->index == expected_index &&
                v0.production.result->index == expected_index &&
                v1.production.result->index == expected_index &&
                bits(v2.production.result->squared_distance) ==
                    bits(independent_minimum) &&
                bits(v2.production.result->squared_distance) ==
                    bits(cpu.result->squared_distance) &&
                bits(v2.production.result->squared_distance) ==
                    bits(v0.production.result->squared_distance) &&
                bits(v2.production.result->squared_distance) ==
                    bits(v1.production.result->squared_distance),
            "V2 matrix argmin comparison failed");

    const std::string case_id =
        "cuda-v2-N" + std::to_string(n) + "-D" + std::to_string(d);
    std::filesystem::create_directories(report_root);
    const auto candidate_path = report_root / (case_id + ".candidates.bin");
    const auto metadata_path = report_root / (case_id + ".metadata.txt");
    std::ofstream candidate_output(candidate_path, std::ios::binary | std::ios::trunc);
    require(candidate_output.good(), "could not write V2 candidate evidence");
    candidate_output.write(reinterpret_cast<const char*>(candidate_records.data()),
                           static_cast<std::streamsize>(candidate_records.size() *
                                                        sizeof(CandidateEvidence)));
    candidate_output.close();
    std::ofstream metadata(metadata_path, std::ios::trunc);
    require(metadata.good(), "could not write V2 case metadata");
    metadata << "schema=S7-CUDA-V2-CASE-EVIDENCE-v1.0\n"
             << "implementation_variant=cuda-v2\n"
             << "workload_kind=single-query-primary\n"
             << "revision=" << s6::build_identity::kRevision << '\n'
             << "working_tree_state=" << s6::build_identity::kWorkingTreeState << '\n'
             << "working_tree_fingerprint="
             << s6::build_identity::kWorkingTreeFingerprint << '\n'
             << "source_manifest_sha256="
             << s6::build_identity::kSourceManifestSha256 << '\n'
             << "seed=" << kSeed << '\n'
             << "candidate_count=" << n << '\n'
             << "dimension=" << d << '\n'
             << "block_size=256\n"
             << "candidate_record_size=" << sizeof(CandidateEvidence) << '\n'
             << "candidate_bytes="
             << candidate_records.size() * sizeof(CandidateEvidence) << '\n'
             << "mapping_status=passed\n"
             << "overwrite_status=passed\n"
             << "layout_transform_status=passed\n"
             << "cpu_v2_comparison_status=passed\n"
             << "v0_v2_comparison_status=passed\n"
             << "v1_v2_comparison_status=passed\n"
             << "candidate_coverage_status=passed\n"
             << "production_path_status=passed\n"
             << "graph_status=passed\n"
             << "arithmetic_environment_status=passed\n"
             << "float32_lexicographic_status=passed\n"
             << "ud001_status=passed\n"
             << "ud002_status=passed\n"
             << "fixture_status=passed\n"
             << "oracle_status=passed\n"
             << "tie_status=passed\n"
             << "reduction_status=passed\n"
             << "nn_verdict=accepted-unambiguous\n"
             << "exact_membership_resolved=true\n"
             << "exact_ambiguity=false\n"
             << "boundary_uncertain_indices=\n"
             << "exact_contenders=" << expected_index << '\n'
             << "conservative_contenders=" << expected_index << '\n'
             << "decision_trace=1:no-demonstrated-numerical-defect|"
                "2:no-candidate-argmin-tie-reduction-production-defect|"
                "3:returned-index-in-exact-C|4:not-applicable-exact-path|"
                "5:mandatory-evidence-complete|6:core-eligible|"
                "7:selected-exact-singleton-and-float64-agreement|"
                "8:not-reached|9:not-reached\n"
             << "result_index=" << v2.production.result->index << '\n'
             << "result_distance_bits="
             << bits(v2.production.result->squared_distance) << '\n';
    metadata.close();

    evidence << "case=N" << n << "-D" << d << " seed=" << kSeed
             << " block_size=256 mapping=passed overwrite=passed"
             << " independent_f32=passed independent_f64=passed"
             << " cpu_v2=passed v0_v2=passed v1_v2=passed"
             << " ud001=passed ud002=passed candidate_retention=passed"
             << " graph_environment_status=passed correlation=passed"
             << " nn_verdict=accepted-unambiguous result_index="
             << v2.production.result->index << " result_distance_bits="
             << bits(v2.production.result->squared_distance) << '\n';
    std::cout << "CUDA V2 matrix N=" << n << " D=" << d << ": PASS\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 5,
                "matrix output, report root, binary evidence, and functional evidence are required");
        verify_binary_correlation(argv[3]);
        verify_functional_correlation(argv[4]);
        const std::filesystem::path evidence_path = argv[1];
        const std::filesystem::path report_root = argv[2];
        std::filesystem::create_directories(evidence_path.parent_path());
        std::ofstream evidence(evidence_path, std::ios::trunc);
        require(evidence.good(), "could not open V2 matrix evidence");
        evidence << "schema=S7-CUDA-V2-MATRIX-EVIDENCE-v1.0\n"
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
                 << s6::build_identity::kSourceManifestSha256 << '\n'
                 << "binary_source_sass_functional_correlation=passed\n";
        constexpr std::array<std::uint64_t, 4> candidate_counts = {
            1024U, 16384U, 262144U, 1048576U};
        constexpr std::array<std::uint64_t, 3> dimensions = {8U, 32U, 128U};
        for (const auto n : candidate_counts) {
            for (const auto d : dimensions) {
                run_case(n, d, evidence, report_root);
            }
        }
        evidence << "matrix_functional_numerical_comparisons=passed cases=12\n"
                 << "matrix_candidate_evidence=passed reports=12\n"
                 << "matrix_nn_acceptance=passed accepted_unambiguous=12"
                    " accepted_ambiguous=0 failed=0 blocked=0\n";
        std::cout << "S7 CUDA V2 required 12-case matrix: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "S7 CUDA V2 required matrix: FAIL: " << error.what() << '\n';
        return 1;
    }
}
