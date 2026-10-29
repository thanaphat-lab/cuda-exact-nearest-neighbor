#include "s6/cpu_baseline.hpp"
#include "s6/cpu_evidence.hpp"

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

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read test artifact: " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write test artifact");
    }
    output << text;
}

std::string field(const std::string& manifest, const std::string& name) {
    const auto prefix = name + "=";
    const auto start = manifest.find(prefix);
    if (start == std::string::npos) {
        throw std::runtime_error("manifest test field missing");
    }
    const auto value_start = start + prefix.size();
    const auto end = manifest.find('\n', value_start);
    auto value = manifest.substr(value_start, end - value_start);
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }
    return value;
}

std::string replace_field(std::string manifest, const std::string& name,
                          const std::string& replacement) {
    const auto prefix = name + "=";
    const auto start = manifest.find(prefix);
    if (start == std::string::npos) {
        throw std::runtime_error("manifest test field missing");
    }
    const auto value_start = start + prefix.size();
    const auto end = manifest.find('\n', value_start);
    manifest.replace(value_start, end - value_start, replacement);
    return manifest;
}

void test_manifest_rejection(const std::filesystem::path& manifest_path) {
    const auto original = read_text(manifest_path);
    const auto directory = manifest_path.parent_path() / "negative-evidence";
    std::filesystem::create_directories(directory);

    const std::vector<std::pair<std::string, std::string>> corruptions = {
        {"revision", std::string(40, '0')},
        {"compiler_version", "0.0.invalid"},
        {"required_compile_flags", "/fp:fast"},
        {"executable_sha256", std::string(64, '0')},
        {"baseline_source_sha256", std::string(64, 'f')}};
    std::size_t case_index = 0;
    for (const auto& corruption : corruptions) {
        const auto path = directory / ("manifest-" + std::to_string(case_index++) + ".txt");
        write_text(path, replace_field(original, corruption.first, corruption.second));
        const auto rejected = s6::verify_cpu_build_evidence(path.string());
        require(rejected.identity_status() == s6::EvidenceStatus::failed &&
                    rejected.graph_status() == s6::EvidenceStatus::blocked,
                "mismatched build evidence was not rejected");
    }

    const auto missing =
        s6::verify_cpu_build_evidence((directory / "missing.manifest").string());
    require(missing.identity_status() == s6::EvidenceStatus::failed &&
                missing.graph_status() == s6::EvidenceStatus::blocked,
            "missing build evidence was not blocked");
}

void test_graph_negative(const std::filesystem::path& manifest_path) {
    const auto manifest = read_text(manifest_path);
    const auto object_disassembly = read_text(field(manifest, "object_disassembly_path"));
    const auto executable_disassembly =
        read_text(field(manifest, "executable_disassembly_path"));
    const auto linker_map = read_text(field(manifest, "map_path"));
    const auto valid = s6::assess_graph_disassembly_for_testing(
        object_disassembly, executable_disassembly, linker_map);
    require(valid.status == s6::EvidenceStatus::passed,
            "valid graph inspection fixture did not pass");

    auto tampered = object_disassembly;
    const auto instruction = tampered.find("subss       xmm0");
    require(instruction != std::string::npos, "graph tamper anchor missing");
    tampered.replace(instruction, 5, "mulss");
    const auto invalid = s6::assess_graph_disassembly_for_testing(
        tampered, executable_disassembly, linker_map);
    require(invalid.status == s6::EvidenceStatus::failed,
            "invalid graph evidence was not failed");
}

void test_coverage_verifier() {
    const auto complete = s6::assess_candidate_visitation(4, {0, 1, 2, 3}, true);
    require(complete.status == s6::EvidenceStatus::passed,
            "complete coverage did not pass");
    require(s6::assess_candidate_visitation(4, {0, 1, 3}, true).status ==
                s6::EvidenceStatus::failed,
            "omitted candidate was not failed");
    require(s6::assess_candidate_visitation(4, {0, 1, 1, 2, 3}, true).status ==
                s6::EvidenceStatus::failed,
            "duplicate candidate was not failed");
    require(s6::assess_candidate_visitation(4, {0, 1, 2, 3, 4}, true).status ==
                s6::EvidenceStatus::failed,
            "out-of-range candidate was not failed");
    require(s6::assess_candidate_visitation(4, {0, 1}, false).status ==
                s6::EvidenceStatus::blocked,
            "incomplete invocation was not blocked");
}

void test_end_to_end_evidence(const s6::CpuBuildEvidence& build_evidence) {
    std::vector<float> dataset = {2.0F, 2.0F, 0.0F, 0.0F, -1.0F, 1.0F};
    std::vector<float> query = {0.0F, 0.0F};
    const s6::DatasetView dataset_view{dataset.data(), 3, 2, dataset.size()};
    const s6::QueryView query_view{query.data(), 2, query.size()};
    const auto evidenced =
        s6::cpu_search_float32_evidenced(dataset_view, query_view, build_evidence);
    const auto reference = s6::compute_float64_reference(dataset_view, query_view);
    s6::ValidationOptions options;
    options.production_evidence = &evidenced.evidence;
    const auto report = s6::validate_cpu_search(dataset_view, query_view,
                                                evidenced.production, reference, options);
    require(report.evidence_identity_status == s6::EvidenceStatus::passed &&
                report.graph_status == s6::EvidenceStatus::passed &&
                report.production_candidate_coverage_status ==
                    s6::EvidenceStatus::passed &&
                report.nn_verdict == s6::NnVerdict::accepted_unambiguous,
            "genuine verified production evidence was not accepted");

    std::vector<float> other_dataset = dataset;
    other_dataset[0] = 3.0F;
    const s6::DatasetView other_view{other_dataset.data(), 3, 2, other_dataset.size()};
    const auto mismatched = s6::validate_cpu_search(other_view, query_view,
                                                    evidenced.production, reference, options);
    require(mismatched.evidence_identity_status == s6::EvidenceStatus::failed &&
                mismatched.nn_verdict == s6::NnVerdict::failed,
            "wrong workload identity was not rejected with production mismatch precedence");

    auto corrupt = evidenced.production;
    corrupt.result->index = 2;
    const auto failed = s6::validate_cpu_search(dataset_view, query_view, corrupt, reference,
                                                options);
    require(failed.nn_verdict == s6::NnVerdict::failed,
            "failed-over-blocked precedence was not preserved");

    s6::ValidationOptions missing;
    const auto blocked = s6::validate_cpu_search(dataset_view, query_view,
                                                 evidenced.production, reference, missing);
    require(blocked.nn_verdict == s6::NnVerdict::blocked_unverified,
            "missing evidence did not block");

    std::vector<float> tied_dataset = {-1.0F, 1.0F};
    std::vector<float> tied_query = {0.0F};
    const s6::DatasetView tied_view{tied_dataset.data(), 2, 1, tied_dataset.size()};
    const s6::QueryView tied_query_view{tied_query.data(), 1, tied_query.size()};
    const auto tied =
        s6::cpu_search_float32_evidenced(tied_view, tied_query_view, build_evidence);
    require(tied.production.result && tied.production.result->index == 0,
            "smallest-index production tie failed");

    const float maximum = std::numeric_limits<float>::max();
    const float negative_maximum = -maximum;
    const s6::DatasetView overflow_dataset{&maximum, 1, 1, 1};
    const s6::QueryView overflow_query{&negative_maximum, 1, 1};
    const auto aborted = s6::cpu_search_float32_evidenced(
        overflow_dataset, overflow_query, build_evidence);
    require(aborted.production.invocation_status == s6::InvocationStatus::backend_failure &&
                aborted.evidence.coverage_status() == s6::EvidenceStatus::blocked &&
                !aborted.evidence.coverage().completed,
            "aborted production evidence was not distinguished from exhaustive success");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "evidence manifest argument missing");
        const std::filesystem::path manifest_path = argv[1];
        const auto build_evidence = s6::verify_cpu_build_evidence(manifest_path.string());
        if (build_evidence.identity_status() != s6::EvidenceStatus::passed) {
            throw std::runtime_error("valid build identity did not pass: " +
                                     build_evidence.detail());
        }
        if (build_evidence.graph_status() != s6::EvidenceStatus::passed) {
            throw std::runtime_error("valid production graph did not pass: " +
                                     build_evidence.detail());
        }
        test_manifest_rejection(manifest_path);
        test_graph_negative(manifest_path);
        test_coverage_verifier();
        test_end_to_end_evidence(build_evidence);
        std::cout << "S6 CPU evidence tests: PASS identity=" << build_evidence.identity()
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "S6 CPU evidence tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
