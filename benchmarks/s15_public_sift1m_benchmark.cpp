#include "s10_sift1m_loader.hpp"

#include "s6/benchmark_internal.hpp"
#include "s6/cpu_build_identity.hpp"
#include "s6/cpu_evidence.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef S15_BENCHMARK_REVISION
#define S15_BENCHMARK_REVISION "unknown"
#endif
#ifndef S15_BENCHMARK_TREE_STATE
#define S15_BENCHMARK_TREE_STATE "unknown"
#endif

namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr std::array<std::uint64_t, 4> kCandidateCounts = {
    1024U, 16384U, 262144U, 1000000U};
constexpr std::array<std::uint64_t, 32> kQueryOrdinals = {
    0U,322U,645U,967U,1290U,1612U,1935U,2257U,
    2580U,2902U,3225U,3548U,3870U,4193U,4515U,4838U,
    5160U,5483U,5805U,6128U,6450U,6773U,7096U,7418U,
    7741U,8063U,8386U,8708U,9031U,9353U,9676U,9999U};
constexpr std::array<std::uint32_t, 4> kBlockSizes = {256U,128U,512U,256U};
constexpr std::string_view kBaseSha256 =
    "21f66e2975057b5728ba56de1c825bac4f4d89d596609ae985741c6242631816";
constexpr std::string_view kQuerySha256 =
    "f7fc9be140accdfd64116c2fa2365ecdb69b8f084970c6b0532db5ff79ac8fdc";
constexpr std::uint64_t kBaseBytes = 516000000U;
constexpr std::uint64_t kQueryBytes = 5160000U;
constexpr unsigned kWarmups = 10U;
constexpr unsigned kRepetitions = 100U;
constexpr unsigned kTotal = kWarmups + kRepetitions;

struct Options {
    std::string experiment_id;
    fs::path config;
    std::string config_sha256;
    std::string expected_revision;
    fs::path dataset_root;
    std::string base_sha256;
    std::string query_sha256;
    std::string captured_at;
    std::string timezone;
    std::string cuda_architecture;
    std::string build_flags;
    std::string benchmark_binary_sha256;
    std::string artifact_manifest_sha256;
    std::string numerical_build_identity_sha256;
    fs::path correctness_reference;
    fs::path cpu_manifest;
    fs::path fixture_evidence;
    fs::path cuda_v2_binary;
    fs::path cuda_v2_functional;
};

struct Sample {
    std::string scenario;
    std::string boundary;
    std::string variant;
    std::uint64_t n = 0U;
    std::uint64_t query_ordinal = 0U;
    std::uint32_t block_size = 0U;
    std::uint64_t run_order = 0U;
    std::uint32_t iteration = 0U;
    std::uint64_t elapsed_ns = 0U;
    std::string timer;
    std::string resource_scope;
};

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(std::string(message));
}
void require(bool condition, std::string_view message) {
    if (!condition) fail(message);
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::uint64_t duration_ns(Clock::time_point start, Clock::time_point stop) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count());
}

std::string json_escape(std::string_view value) {
    std::string result;
    for (const char character : value) {
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result.push_back(character); break;
        }
    }
    return result;
}

std::string read_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot open required evidence file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void require_token(const std::string& text, const std::string& token,
                   std::string_view label) {
    require(text.find(token) != std::string::npos,
            std::string(label) + " is missing required token: " + token);
}

void require_unique_token(const std::string& text, const std::string& token,
                          std::string_view label) {
    const auto first = text.find(token);
    require(first != std::string::npos &&
                text.find(token, first + token.size()) == std::string::npos,
            std::string(label) + " must contain exactly one token: " + token);
}

std::string take(int& index, int argc, char** argv, std::string_view option) {
    require(index + 1 < argc, std::string(option) + " requires a value");
    return argv[++index];
}

Options parse_options(int argc, char** argv, int first) {
    Options result;
    for (int index = first; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--experiment-id") result.experiment_id = take(index,argc,argv,option);
        else if (option == "--experiment-config") result.config = take(index,argc,argv,option);
        else if (option == "--config-sha256") result.config_sha256 = take(index,argc,argv,option);
        else if (option == "--expected-revision") result.expected_revision = take(index,argc,argv,option);
        else if (option == "--dataset-root") result.dataset_root = take(index,argc,argv,option);
        else if (option == "--base-sha256") result.base_sha256 = take(index,argc,argv,option);
        else if (option == "--query-sha256") result.query_sha256 = take(index,argc,argv,option);
        else if (option == "--captured-at") result.captured_at = take(index,argc,argv,option);
        else if (option == "--timezone") result.timezone = take(index,argc,argv,option);
        else if (option == "--cuda-architecture") result.cuda_architecture = take(index,argc,argv,option);
        else if (option == "--build-flags") result.build_flags = take(index,argc,argv,option);
        else if (option == "--benchmark-binary-sha256") result.benchmark_binary_sha256 = take(index,argc,argv,option);
        else if (option == "--artifact-manifest-sha256") result.artifact_manifest_sha256 = take(index,argc,argv,option);
        else if (option == "--numerical-build-identity-sha256") result.numerical_build_identity_sha256 = take(index,argc,argv,option);
        else if (option == "--correctness-reference") result.correctness_reference = take(index,argc,argv,option);
        else if (option == "--cpu-manifest") result.cpu_manifest = take(index,argc,argv,option);
        else if (option == "--fixture-evidence") result.fixture_evidence = take(index,argc,argv,option);
        else if (option == "--cuda-v2-binary") result.cuda_v2_binary = take(index,argc,argv,option);
        else if (option == "--cuda-v2-functional") result.cuda_v2_functional = take(index,argc,argv,option);
        else fail("unknown S15 benchmark option: " + option);
    }
    require(!result.experiment_id.empty() && fs::exists(result.config) &&
                result.config_sha256.size() == 64U,
            "missing experiment/config identity");
    require(s10::detail::sha256_file(result.config) == result.config_sha256,
            "configuration SHA-256 mismatch");
    require(result.expected_revision == S15_BENCHMARK_REVISION,
            "expected revision does not match compiled S15 revision");
    require(!result.dataset_root.empty() && result.base_sha256 == kBaseSha256 &&
                result.query_sha256 == kQuerySha256,
            "dataset hashes do not match the approved S15 identities");
    require(!result.captured_at.empty() && !result.timezone.empty() &&
                !result.cuda_architecture.empty() && !result.build_flags.empty(),
            "missing capture/build identity");
    require(result.benchmark_binary_sha256.size() == 64U &&
                result.artifact_manifest_sha256.size() == 64U &&
                result.numerical_build_identity_sha256.size() == 64U,
            "missing binary or numerical-build identity");
    return result;
}

std::vector<std::uint64_t> query_ordinals() {
    return {kQueryOrdinals.begin(), kQueryOrdinals.end()};
}

void validate_loaded_dataset(const s10::LoadedSift1m& dataset,
                             const Options& options) {
    require(dataset.base_sha256 == kBaseSha256 &&
                dataset.query_sha256 == kQuerySha256 &&
                dataset.base_sha256 == options.base_sha256 &&
                dataset.query_sha256 == options.query_sha256,
            "loaded SIFT1M hash does not match approved/configured identity");
    require(dataset.base_file_bytes == kBaseBytes &&
                dataset.query_file_bytes == kQueryBytes &&
                dataset.base_records == 1000000U && dataset.dimension == 128U &&
                dataset.selected_query_ordinals == query_ordinals(),
            "loaded SIFT1M size/schema/query selection mismatch");
}

void verify_prerequisites(const Options& options,
                          s6::CpuBuildEvidence& build,
                          s6::MandatoryGateEvidence& gates) {
    require(fs::exists(options.cpu_manifest) && fs::exists(options.fixture_evidence) &&
                fs::exists(options.cuda_v2_binary) &&
                fs::exists(options.cuda_v2_functional),
            "matching CPU/CUDA prerequisite evidence is missing");
    build = s6::verify_cpu_build_evidence(options.cpu_manifest.string());
    require(build.identity_status() == s6::EvidenceStatus::passed &&
                build.graph_status() == s6::EvidenceStatus::passed,
            "CPU build graph/source identity evidence did not pass");
    std::string detail;
    gates = s6::load_cpu_fixture_evidence(options.fixture_evidence.string(),
                                          build.source_identity(), detail);
    require(gates.fixture_status == s6::EvidenceStatus::passed &&
                gates.oracle_status == s6::EvidenceStatus::passed &&
                gates.tie_status == s6::EvidenceStatus::passed &&
                gates.reduction_status == s6::EvidenceStatus::passed,
            "mandatory CPU fixtures did not pass");
    const auto binary = read_text(options.cuda_v2_binary);
    const auto functional = read_text(options.cuda_v2_functional);
    for (const auto& token : {
             std::string("revision=") + s6::build_identity::kRevision,
             std::string("working_tree_state=") + s6::build_identity::kWorkingTreeState,
             std::string("source_manifest_sha256=") +
                 s6::build_identity::kSourceManifestSha256}) {
        require_token(binary, token, "CUDA V2 binary evidence");
        require_token(functional, token, "CUDA V2 functional evidence");
    }
    for (const char* token : {"compile_policy_status=passed",
                              "binary_graph_structure_status=passed"})
        require_token(binary, token, "CUDA V2 binary evidence");
    for (const char* token : {"overall=passed", "reduction=passed",
                              "exact_dyadic_oracle=passed"})
        require_token(functional, token, "CUDA V2 functional evidence");
}

bool accepted(s6::NnVerdict verdict) {
    return verdict == s6::NnVerdict::accepted_unambiguous ||
           verdict == s6::NnVerdict::accepted_ambiguous;
}

const char* verdict_name(s6::NnVerdict verdict) {
    return verdict == s6::NnVerdict::accepted_unambiguous
               ? "accepted-unambiguous"
               : verdict == s6::NnVerdict::accepted_ambiguous
                     ? "accepted-ambiguous" : "failed";
}

void require_matching_result(const s6::ProductionOutcome& outcome,
                             const s6::SearchResult& expected,
                             std::string_view label) {
    require(outcome.invocation_status == s6::InvocationStatus::completed &&
                outcome.result && outcome.result->index == expected.index &&
                float_bits(outcome.result->squared_distance) ==
                    float_bits(expected.squared_distance),
            std::string(label) + " differs from independent Float32 argmin");
}

void validate_raw_v2(const s6::CudaV2EvidencedOutcome& outcome,
                     const s6::ValidationReport& cpu_report,
                     const s6::SearchResult& expected, std::uint64_t n) {
    require_matching_result(outcome.production, expected, "raw V2 result");
    require(outcome.evidence.mapping_status == s6::EvidenceStatus::passed &&
                outcome.evidence.overwrite_status == s6::EvidenceStatus::passed &&
                outcome.evidence.expected_candidate_count == n &&
                outcome.evidence.observed_mapping_count == n &&
                outcome.evidence.candidate_distances.size() ==
                    static_cast<std::size_t>(n) &&
                cpu_report.independently_computed_float32_distances.size() ==
                    static_cast<std::size_t>(n),
            "raw V2 exhaustive candidate/reduction evidence is incomplete");
    for (std::size_t index = 0U; index < outcome.evidence.candidate_distances.size();
         ++index) {
        require(float_bits(outcome.evidence.candidate_distances[index]) ==
                    float_bits(cpu_report.independently_computed_float32_distances[index]),
                "raw V2 candidate distance differs from independent Float32 graph");
    }
}

void write_case(std::ofstream& output, std::uint64_t ordinal,
                std::string_view method, std::uint64_t n,
                std::uint64_t query_ordinal, std::uint32_t block_size,
                const s6::ProductionOutcome& production,
                const s6::ValidationReport& report, const Options& options,
                const s10::LoadedSift1m& dataset) {
    output << "{\"schema_version\":\"S15-CORRECTNESS-CASE-v1\","
           << "\"ordinal\":" << ordinal << ",\"revision\":\""
           << S15_BENCHMARK_REVISION << "\",\"working_tree_state\":\""
           << S15_BENCHMARK_TREE_STATE << "\",\"source_manifest_sha256\":\""
           << s6::build_identity::kSourceManifestSha256
           << "\",\"experiment_config_sha256\":\"" << options.config_sha256
           << "\",\"sift_base_sha256\":\"" << dataset.base_sha256
           << "\",\"sift_query_sha256\":\"" << dataset.query_sha256
           << "\",\"benchmark_binary_sha256\":\""
           << options.benchmark_binary_sha256
           << "\",\"artifact_manifest_sha256\":\""
           << options.artifact_manifest_sha256
           << "\",\"numerical_build_identity_sha256\":\""
           << options.numerical_build_identity_sha256
           << "\",\"cuda_architecture\":\""
           << json_escape(options.cuda_architecture)
           << "\",\"build_flags\":\"" << json_escape(options.build_flags)
           << "\",\"method\":\""
           << method << "\",\"N\":" << n << ",\"D\":128,\"query_ordinal\":"
           << query_ordinal << ",\"block_size\":" << block_size
           << ",\"implementation_index\":" << production.result->index
           << ",\"implementation_distance_bits\":"
           << float_bits(production.result->squared_distance)
           << ",\"f32_lexicographic_status\":\"passed\","
              "\"production_path_status\":\"passed\","
              "\"graph_status\":\"passed\","
              "\"arithmetic_environment_status\":\"passed\","
              "\"candidate_coverage_status\":\"passed\","
              "\"production_candidate_coverage_status\":\"passed\","
              "\"production_match_status\":\"passed\","
              "\"reference_match_status\":\"passed\","
              "\"all_candidate_distance_bound_status\":\"passed\","
              "\"ranking_status\":\"passed\",\"float64_index_agreement\":"
           << (report.float64_index_agrees ? "true" : "false")
           << ",\"exact_ambiguity_status\":\""
           << (report.exact_ambiguity_known
                   ? (report.exact_ambiguity ? "known-ambiguous" :
                                               "known-unambiguous")
                   : "unknown")
           << "\",\"nn_verdict\":\"" << verdict_name(report.nn_verdict)
           << "\",\"case_status\":\"passed\"}\n";
}

int run_correctness(const fs::path& output, const Options& options,
                    const s10::LoadedSift1m& dataset) {
    s6::CpuBuildEvidence build;
    s6::MandatoryGateEvidence gates;
    verify_prerequisites(options, build, gates);
    fs::create_directories(output);
    std::ofstream ledger(output / "correctness-cases.jsonl", std::ios::binary);
    require(static_cast<bool>(ledger), "cannot create S15 correctness ledger");
    std::uint64_t case_ordinal = 0U;
    for (std::size_t size_index = 0U; size_index < kCandidateCounts.size();
         ++size_index) {
        const auto n = kCandidateCounts[size_index];
        const auto data = dataset.prefix(n);
        const s6::CudaV2Options cuda_options{0, nullptr, kBlockSizes[size_index]};
        auto prepared_result = s6::cuda_v2_prepare_dataset_float32(data, cuda_options);
        require(prepared_result.invocation_status == s6::InvocationStatus::completed &&
                    prepared_result.prepared_dataset,
                "S15 correctness prepared-dataset construction failed");
        auto prepared = std::move(*prepared_result.prepared_dataset);
        auto workspace_result = s6::cuda_v2_create_workspace(prepared, cuda_options);
        require(workspace_result.invocation_status == s6::InvocationStatus::completed &&
                    workspace_result.workspace,
                "S15 correctness workspace construction failed");
        auto workspace = std::move(*workspace_result.workspace);
        for (const auto query_ordinal : kQueryOrdinals) {
            const auto query = dataset.selected_query_view(query_ordinal);
            const auto cpu = s6::cpu_search_float32_evidenced(data, query, build);
            require(cpu.production.invocation_status == s6::InvocationStatus::completed &&
                        cpu.production.result,
                    "S15 CPU production correctness invocation failed");
            const auto reference = s6::compute_float64_reference(data, query);
            require(reference.invocation_status == s6::InvocationStatus::completed,
                    "S15 Float64 reference invocation failed");
            s6::ValidationOptions validation;
            validation.production_evidence = &cpu.evidence;
            validation.mandatory_gate_evidence = &gates;
            const auto report = s6::validate_cpu_search(
                data, query, cpu.production, reference, validation);
            require(report.graph_status == s6::EvidenceStatus::passed &&
                        report.arithmetic_environment_status ==
                            s6::EvidenceStatus::passed &&
                        report.candidate_coverage_status == s6::EvidenceStatus::passed &&
                        report.production_candidate_coverage_status ==
                            s6::EvidenceStatus::passed &&
                        report.production_match_status == s6::EvidenceStatus::passed &&
                        report.reference_match_status == s6::EvidenceStatus::passed &&
                        report.ud001_status == s6::EvidenceStatus::passed &&
                        report.ud002_status == s6::EvidenceStatus::passed &&
                        accepted(report.nn_verdict) &&
                        report.independently_computed_float32_argmin_index,
                    "S15 CPU numerical correctness workload was not accepted");
            const auto expected_index =
                *report.independently_computed_float32_argmin_index;
            const s6::SearchResult expected{
                expected_index,
                report.independently_computed_float32_distances[
                    static_cast<std::size_t>(expected_index)]};
            require_matching_result(cpu.production, expected, "CPU result");
            write_case(ledger, case_ordinal++, "cpu-f32", n, query_ordinal, 0U,
                       cpu.production, report, options, dataset);
            const auto raw = s6::cuda_v2_search_float32_evidenced(
                data, query, cuda_options);
            validate_raw_v2(raw, report, expected, n);
            write_case(ledger, case_ordinal++, "v2-raw", n, query_ordinal,
                       kBlockSizes[size_index], raw.production, report, options,
                       dataset);
            const auto prepared_outcome = s6::cuda_v2_search_prepared_float32(
                prepared, query, workspace, cuda_options);
            require_matching_result(prepared_outcome, expected, "prepared V2 result");
            write_case(ledger, case_ordinal++, "v2-prepared", n, query_ordinal,
                       kBlockSizes[size_index], prepared_outcome, report, options,
                       dataset);
        }
    }
    require(case_ordinal == 384U, "S15 correctness matrix is incomplete");
    ledger.close();
    require(static_cast<bool>(ledger), "S15 correctness ledger write failed");
    const auto ledger_sha =
        s10::detail::sha256_file(output / "correctness-cases.jsonl");
    const bool clean = std::string_view(S15_BENCHMARK_TREE_STATE) == "clean";
    std::ofstream gate(output / "correctness-gate.json", std::ios::binary);
    require(static_cast<bool>(gate), "cannot create S15 correctness gate");
    gate << "{\n  \"schema_version\": \"S15-CORRECTNESS-GATE-v1\",\n"
         << "  \"status\": \""
         << (clean ? "passed-clean-candidate" :
                     "passed-local-uncommitted-not-acceptance") << "\",\n"
         << "  \"revision\": \"" << S15_BENCHMARK_REVISION << "\",\n"
         << "  \"working_tree_state\": \"" << S15_BENCHMARK_TREE_STATE << "\",\n"
         << "  \"source_manifest_sha256\": \""
         << s6::build_identity::kSourceManifestSha256 << "\",\n"
         << "  \"experiment_config_sha256\": \"" << options.config_sha256 << "\",\n"
         << "  \"sift_base_sha256\": \"" << dataset.base_sha256 << "\",\n"
         << "  \"sift_query_sha256\": \"" << dataset.query_sha256 << "\",\n"
         << "  \"benchmark_binary_sha256\": \""
         << options.benchmark_binary_sha256 << "\",\n"
         << "  \"artifact_manifest_sha256\": \""
         << options.artifact_manifest_sha256 << "\",\n"
         << "  \"numerical_build_identity_sha256\": \""
         << options.numerical_build_identity_sha256 << "\",\n"
         << "  \"cuda_architecture\": \""
         << json_escape(options.cuda_architecture) << "\",\n"
         << "  \"build_flags\": \"" << json_escape(options.build_flags)
         << "\",\n"
         << "  \"correctness_ledger_sha256\": \"" << ledger_sha << "\",\n"
         << "  \"query_selection\": \"S15-Q32-floor-k-times-9999-over-31-v1\",\n"
         << "  \"v2_block_mapping\": \"1024:256;16384:128;262144:512;1000000:256\",\n"
         << "  \"required_workloads\": 128,\n"
         << "  \"required_production_results\": 384,\n"
         << "  \"accepted_production_results\": 384,\n"
         << "  \"failed_production_results\": 0,\n"
         << "  \"blocked_production_results\": 0,\n"
         << "  \"performance_authorized\": " << (clean ? "true" : "false")
         << "\n}\n";
    return 0;
}

void validate_correctness_gate(const Options& options,
                               const s10::LoadedSift1m& dataset) {
    require(fs::exists(options.correctness_reference),
            "matching S15 correctness gate is required");
    const auto gate = read_text(options.correctness_reference);
    for (const auto& token : {
             std::string("\"schema_version\": \"S15-CORRECTNESS-GATE-v1\""),
             std::string("\"status\": \"passed-clean-candidate\""),
             std::string("\"revision\": \"") + S15_BENCHMARK_REVISION + "\"",
             std::string("\"working_tree_state\": \"clean\""),
             std::string("\"source_manifest_sha256\": \"") +
                 s6::build_identity::kSourceManifestSha256 + "\"",
             std::string("\"experiment_config_sha256\": \"") +
                 options.config_sha256 + "\"",
             std::string("\"sift_base_sha256\": \"") + dataset.base_sha256 + "\"",
             std::string("\"sift_query_sha256\": \"") + dataset.query_sha256 + "\"",
             std::string("\"benchmark_binary_sha256\": \"") +
                 options.benchmark_binary_sha256 + "\"",
             std::string("\"artifact_manifest_sha256\": \"") +
                 options.artifact_manifest_sha256 + "\"",
             std::string("\"numerical_build_identity_sha256\": \"") +
                 options.numerical_build_identity_sha256 + "\"",
             std::string("\"required_workloads\": 128"),
             std::string("\"required_production_results\": 384"),
             std::string("\"accepted_production_results\": 384"),
             std::string("\"failed_production_results\": 0"),
             std::string("\"blocked_production_results\": 0"),
             std::string("\"performance_authorized\": true")}) {
        require_unique_token(gate, token, "S15 correctness gate");
    }
    require_token(gate, "S15-Q32-floor-k-times-9999-over-31-v1",
                  "S15 correctness gate");
    require_token(gate, "1024:256;16384:128;262144:512;1000000:256",
                  "S15 correctness gate");
    require_unique_token(gate,
                         std::string("\"cuda_architecture\": \"") +
                             options.cuda_architecture + "\"",
                         "S15 correctness gate");
    require_unique_token(gate,
                         std::string("\"build_flags\": \"") +
                             options.build_flags + "\"",
                         "S15 correctness gate");
    const auto ledger_key = std::string("\"correctness_ledger_sha256\": \"");
    const auto ledger_start = gate.find(ledger_key);
    require(ledger_start != std::string::npos, "correctness ledger hash is missing");
    const auto hash_start = ledger_start + ledger_key.size();
    const auto hash_stop = gate.find('"', hash_start);
    require(hash_stop == hash_start + 64U, "correctness ledger hash is malformed");
    const auto expected_ledger_sha = gate.substr(hash_start, 64U);
    const auto ledger_path = options.correctness_reference.parent_path() /
                             "correctness-cases.jsonl";
    require(fs::exists(ledger_path) &&
                s10::detail::sha256_file(ledger_path) == expected_ledger_sha,
            "correctness ledger is missing or its SHA-256 mismatches the gate");
    std::istringstream input(read_text(ledger_path));
    std::string line;
    std::uint64_t ordinal = 0U;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        require(ordinal < 384U && !line.empty(),
                "malformed or extra S15 correctness ledger row");
        const auto workload = ordinal / 3U;
        const auto size_index = static_cast<std::size_t>(workload / 32U);
        const auto query_index = static_cast<std::size_t>(workload % 32U);
        constexpr std::array<std::string_view, 3> methods = {
            "cpu-f32", "v2-raw", "v2-prepared"};
        const auto method_index = static_cast<std::size_t>(ordinal % 3U);
        for (const auto& token : {
                 std::string("\"schema_version\":\"S15-CORRECTNESS-CASE-v1\""),
                 std::string("\"ordinal\":") + std::to_string(ordinal) + ",",
                 std::string("\"revision\":\"") + S15_BENCHMARK_REVISION + "\"",
                 std::string("\"working_tree_state\":\"clean\""),
                 std::string("\"experiment_config_sha256\":\"") +
                     options.config_sha256 + "\"",
                 std::string("\"sift_base_sha256\":\"") + dataset.base_sha256 + "\"",
                 std::string("\"sift_query_sha256\":\"") + dataset.query_sha256 + "\"",
                 std::string("\"benchmark_binary_sha256\":\"") +
                     options.benchmark_binary_sha256 + "\"",
                 std::string("\"artifact_manifest_sha256\":\"") +
                     options.artifact_manifest_sha256 + "\"",
                 std::string("\"numerical_build_identity_sha256\":\"") +
                     options.numerical_build_identity_sha256 + "\"",
                 std::string("\"cuda_architecture\":\"") +
                     options.cuda_architecture + "\"",
                 std::string("\"build_flags\":\"") +
                     options.build_flags + "\"",
                 std::string("\"method\":\"") +
                     std::string(methods[method_index]) + "\"",
                 std::string("\"N\":") +
                     std::to_string(kCandidateCounts[size_index]) + ",",
                 std::string("\"D\":128"),
                 std::string("\"query_ordinal\":") +
                     std::to_string(kQueryOrdinals[query_index]) + ",",
                 std::string("\"block_size\":") +
                     std::to_string(method_index == 0U ? 0U :
                                    kBlockSizes[size_index]) + ",",
                 std::string("\"case_status\":\"passed\"")}) {
            require_unique_token(line, token, "S15 correctness ledger row");
        }
        for (const char* status : {
                 "f32_lexicographic_status", "production_path_status",
                 "graph_status", "arithmetic_environment_status",
                 "candidate_coverage_status",
                 "production_candidate_coverage_status",
                 "production_match_status", "reference_match_status",
                 "all_candidate_distance_bound_status", "ranking_status"}) {
            require_unique_token(line,
                                 std::string("\"") + status +
                                     "\":\"passed\"",
                                 "S15 correctness ledger row");
        }
        require(line.find("\"nn_verdict\":\"accepted-unambiguous\"") !=
                    std::string::npos ||
                    line.find("\"nn_verdict\":\"accepted-ambiguous\"") !=
                    std::string::npos,
                "S15 correctness ledger row has a non-accepted verdict");
        ++ordinal;
    }
    require(input.eof() && ordinal == 384U,
            "S15 correctness ledger is incomplete");
}

void add_sample(std::vector<Sample>& samples, std::string_view scenario,
                std::string_view boundary, std::string_view variant,
                std::uint64_t n, std::uint64_t query_ordinal,
                std::uint32_t block_size, std::uint64_t& order,
                std::uint32_t iteration, std::uint64_t elapsed,
                std::string_view timer, std::string_view resource_scope) {
    samples.push_back({std::string(scenario), std::string(boundary),
                       std::string(variant), n, query_ordinal, block_size,
                       order++, iteration, elapsed, std::string(timer),
                       std::string(resource_scope)});
}

void measure_construction(std::vector<Sample>& samples, s6::DatasetView data,
                          std::uint64_t n, std::uint32_t block_size,
                          std::uint64_t& order) {
    const s6::CudaV2Options cuda_options{0, nullptr, block_size};
    for (unsigned iteration = 0U; iteration < kTotal; ++iteration) {
        const auto ready_start = Clock::now();
        const auto prep_start = Clock::now();
        auto prep_result = s6::cuda_v2_prepare_dataset_float32(data, cuda_options);
        const auto prep_stop = Clock::now();
        require(prep_result.invocation_status == s6::InvocationStatus::completed &&
                    prep_result.prepared_dataset,
                "prepared dataset construction failed");
        auto prepared = std::move(*prep_result.prepared_dataset);
        const auto workspace_start = Clock::now();
        auto workspace_result = s6::cuda_v2_create_workspace(prepared, cuda_options);
        const auto workspace_stop = Clock::now();
        require(workspace_result.invocation_status == s6::InvocationStatus::completed &&
                    workspace_result.workspace,
                "V2 workspace construction failed");
        if (iteration < kWarmups) continue;
        const auto retained = iteration - kWarmups;
        add_sample(samples, "prepared-construction",
                   "COST-PREP-DATASET-CONSTRUCTION", "v2", n,
                   std::numeric_limits<std::uint64_t>::max(), block_size, order,
                   retained, duration_ns(prep_start, prep_stop), "steady_clock",
                   "persistent-prepared-dataset-destruction-excluded");
        add_sample(samples, "prepared-construction",
                   "COST-V2-WORKSPACE-CONSTRUCTION", "v2", n,
                   std::numeric_limits<std::uint64_t>::max(), block_size, order,
                   retained, duration_ns(workspace_start, workspace_stop),
                   "steady_clock", "persistent-workspace-destruction-excluded");
        add_sample(samples, "prepared-construction",
                   "COST-PREPARED-READY-CONSTRUCTION", "v2", n,
                   std::numeric_limits<std::uint64_t>::max(), block_size, order,
                   retained, duration_ns(ready_start, workspace_stop),
                   "steady_clock",
                   "persistent-resource-destruction-excluded-not-lifetime-cost");
    }
}

enum class Lane { cpu_raw, v2_kernel, v2_raw, v2_prepared };
constexpr std::array<Lane, 4> kLanes = {
    Lane::cpu_raw, Lane::v2_kernel, Lane::v2_raw, Lane::v2_prepared};

void measure_workload(std::vector<Sample>& samples, s6::DatasetView data,
                      s6::QueryView query, std::uint64_t n,
                      std::uint64_t query_ordinal, std::uint32_t block_size,
                      std::size_t workload_ordinal,
                      const s6::CudaV2PreparedDataset& prepared,
                      s6::CudaV2Workspace& workspace, std::uint64_t& order) {
    const s6::CudaV2Options cuda_options{0, nullptr, block_size};
    const auto cpu_expected = s6::cpu_search_float32(data, query);
    require(cpu_expected.invocation_status == s6::InvocationStatus::completed &&
                cpu_expected.result,
            "untimed CPU result failed");
    const auto expected = *cpu_expected.result;
    for (unsigned iteration = 0U; iteration < kTotal; ++iteration) {
        const auto rotation = (workload_ordinal + iteration) % kLanes.size();
        for (std::size_t lane_index = 0U; lane_index < kLanes.size(); ++lane_index) {
            const auto lane = kLanes[(rotation + lane_index) % kLanes.size()];
            const auto start = Clock::now();
            s6::ProductionOutcome outcome;
            std::uint64_t elapsed = 0U;
            std::string_view timer = "steady_clock";
            if (lane == Lane::cpu_raw) {
                outcome = s6::cpu_search_float32(data, query);
            } else if (lane == Lane::v2_kernel) {
                const auto staged =
                    s6::detail::cuda_v2_search_float32_staged_for_benchmark(
                        data, query, cuda_options);
                require(staged.stages.device_timing_available,
                        "V2 kernel CUDA-event timing unavailable");
                outcome = staged.production;
                elapsed = staged.stages.kernel_ns;
                timer = "cuda_event";
            } else if (lane == Lane::v2_raw) {
                outcome = s6::cuda_v2_search_float32(data, query, cuda_options);
            } else {
                outcome = s6::cuda_v2_search_prepared_float32(
                    prepared, query, workspace, cuda_options);
            }
            const auto stop = Clock::now();
            require_matching_result(outcome, expected, "measured S15 invocation");
            if (iteration < kWarmups) continue;
            if (lane != Lane::v2_kernel) elapsed = duration_ns(start, stop);
            const auto retained = iteration - kWarmups;
            if (lane == Lane::cpu_raw)
                add_sample(samples,"raw-one-shot","CPU-E2E-RAW","cpu",n,
                           query_ordinal,0U,order,retained,elapsed,timer,"not-applicable");
            else if (lane == Lane::v2_kernel)
                add_sample(samples,"kernel-only","BND-KERNEL","v2",n,
                           query_ordinal,block_size,order,retained,elapsed,timer,
                           "device-resident-kernel-sequence");
            else if (lane == Lane::v2_raw)
                add_sample(samples,"raw-one-shot","BND-E2E-RAW","v2",n,
                           query_ordinal,block_size,order,retained,elapsed,timer,
                           "call-owned-transient-cleanup-included");
            else
                add_sample(samples,"prepared-reuse","BND-E2E-PREP","v2",n,
                           query_ordinal,block_size,order,retained,elapsed,timer,
                           "persistent-resources-reused-destruction-excluded");
        }
    }
}

void write_samples(const fs::path& path, const std::vector<Sample>& samples) {
    std::ofstream output(path, std::ios::binary);
    require(static_cast<bool>(output), "cannot create S15 raw samples");
    output << "schema_version,revision,tree_state,sample_role,workload,scenario,"
              "boundary,variant,N,D,query_ordinal,block_size,run_order,iteration,"
              "warmup,elapsed_ns,timer,status,resource_scope\n";
    for (const auto& sample : samples) {
        output << "S15-BENCHMARK-RAW-v1," << S15_BENCHMARK_REVISION << ','
               << S15_BENCHMARK_TREE_STATE << ",final,single-query,"
               << sample.scenario << ',' << sample.boundary << ','
               << sample.variant << ',' << sample.n << ",128,";
        if (sample.query_ordinal == std::numeric_limits<std::uint64_t>::max())
            output << "dataset";
        else output << sample.query_ordinal;
        output << ',' << sample.block_size << ',' << sample.run_order << ','
               << sample.iteration << ",false," << sample.elapsed_ns << ','
               << sample.timer << ",completed," << sample.resource_scope << '\n';
    }
}

void write_manifest(const fs::path& output, const Options& options,
                    const s10::LoadedSift1m& dataset,
                    const s6::CudaV2DeviceInfo& device,
                    std::string_view correctness_ledger_sha256) {
    std::ofstream manifest(output / "manifest.json", std::ios::binary);
    require(static_cast<bool>(manifest), "cannot create S15 manifest");
    manifest << "{\n  \"schema_version\": \"S15-BENCHMARK-MANIFEST-v1\",\n"
             << "  \"experiment_id\": \"" << json_escape(options.experiment_id) << "\",\n"
             << "  \"revision\": \"" << S15_BENCHMARK_REVISION << "\",\n"
             << "  \"working_tree_state\": \"" << S15_BENCHMARK_TREE_STATE << "\",\n"
             << "  \"experiment_config_sha256\": \"" << options.config_sha256 << "\",\n"
             << "  \"source_manifest_sha256\": \""
             << s6::build_identity::kSourceManifestSha256 << "\",\n"
             << "  \"sift_base_sha256\": \"" << dataset.base_sha256 << "\",\n"
             << "  \"sift_query_sha256\": \"" << dataset.query_sha256 << "\",\n"
             << "  \"correctness_ledger_sha256\": \""
             << correctness_ledger_sha256 << "\",\n"
             << "  \"query_selection\": \"S15-Q32-floor-k-times-9999-over-31-v1\",\n"
             << "  \"v2_block_mapping\": \"1024:256;16384:128;262144:512;1000000:256\",\n"
             << "  \"warmups\": 10,\n  \"measured_repetitions\": 100,\n"
             << "  \"captured_at\": \"" << json_escape(options.captured_at) << "\",\n"
             << "  \"timezone\": \"" << json_escape(options.timezone) << "\",\n"
             << "  \"cuda_architecture\": \""
             << json_escape(options.cuda_architecture) << "\",\n"
             << "  \"build_flags\": \"" << json_escape(options.build_flags) << "\",\n"
             << "  \"benchmark_binary_sha256\": \""
             << options.benchmark_binary_sha256 << "\",\n"
             << "  \"artifact_manifest_sha256\": \""
             << options.artifact_manifest_sha256 << "\",\n"
             << "  \"numerical_build_identity_sha256\": \""
             << options.numerical_build_identity_sha256 << "\",\n"
             << "  \"gpu_name\": \"" << json_escape(device.name) << "\",\n"
             << "  \"gpu_uuid\": \"" << json_escape(device.uuid) << "\",\n"
             << "  \"persistent_resource_destruction\": \"excluded; prepared results are not complete lifetime costs\"\n}\n";
}

int run_performance(const fs::path& output, const Options& options,
                    const s10::LoadedSift1m& dataset) {
    require(std::string_view(S15_BENCHMARK_TREE_STATE) == "clean",
            "formal S15 measurement requires clean compiled tree identity");
    validate_correctness_gate(options, dataset);
    const auto gate = read_text(options.correctness_reference);
    const auto ledger_key = std::string("\"correctness_ledger_sha256\": \"");
    const auto ledger_start = gate.find(ledger_key);
    require(ledger_start != std::string::npos, "correctness ledger hash is missing");
    const auto hash_start = ledger_start + ledger_key.size();
    const auto hash_stop = gate.find('"', hash_start);
    require(hash_stop == hash_start + 64U, "correctness ledger hash is malformed");
    const auto device = s6::inspect_cuda_v2_device(0);
    require(device.status == s6::EvidenceStatus::passed,
            "CUDA V2 device identity unavailable");
    fs::create_directories(output);
    std::vector<Sample> samples;
    samples.reserve(52400U);
    std::uint64_t order = 0U;
    std::size_t workload_ordinal = 0U;
    for (std::size_t size_index = 0U; size_index < kCandidateCounts.size();
         ++size_index) {
        const auto n = kCandidateCounts[size_index];
        const auto block_size = kBlockSizes[size_index];
        const auto data = dataset.prefix(n);
        measure_construction(samples, data, n, block_size, order);
        const s6::CudaV2Options cuda_options{0, nullptr, block_size};
        auto prep_result = s6::cuda_v2_prepare_dataset_float32(data, cuda_options);
        require(prep_result.invocation_status == s6::InvocationStatus::completed &&
                    prep_result.prepared_dataset,
                "stable prepared dataset construction failed");
        auto prepared = std::move(*prep_result.prepared_dataset);
        auto workspace_result = s6::cuda_v2_create_workspace(prepared, cuda_options);
        require(workspace_result.invocation_status == s6::InvocationStatus::completed &&
                    workspace_result.workspace,
                "stable workspace construction failed");
        auto workspace = std::move(*workspace_result.workspace);
        for (const auto query_ordinal : kQueryOrdinals) {
            measure_workload(samples, data,
                             dataset.selected_query_view(query_ordinal), n,
                             query_ordinal, block_size, workload_ordinal++,
                             prepared, workspace, order);
        }
    }
    require(samples.size() == 52400U, "S15 raw sample matrix is incomplete");
    write_samples(output / "raw_samples.csv", samples);
    write_manifest(output, options, dataset, device,
                   std::string_view(gate).substr(hash_start, 64U));
    return 0;
}

int run_preflight(const fs::path& output, const Options& options,
                  const s10::LoadedSift1m& dataset) {
    const auto device = s6::inspect_cuda_v2_device(0);
    require(device.status == s6::EvidenceStatus::passed,
            "CUDA V2 device unavailable for bounded preflight");
    fs::create_directories(output);
    const auto probe_start = Clock::now();
    std::ofstream stages(output / "probe-stages.jsonl", std::ios::binary);
    require(static_cast<bool>(stages), "cannot create preflight stage ledger");
    std::uint64_t invocation = 0U;
    for (std::size_t size_index = 0U; size_index < kCandidateCounts.size();
         ++size_index) {
        const auto n = kCandidateCounts[size_index];
        const auto data = dataset.prefix(n);
        const auto query = dataset.selected_query_view(0U);
        const s6::CudaV2Options cuda_options{0, nullptr, kBlockSizes[size_index]};
        const auto record = [&](std::string_view stage, Clock::time_point start) {
            stages << "{\"schema_version\":\"S15-PREFLIGHT-STAGE-v1\","
                   << "\"invocation\":" << invocation++ << ",\"N\":" << n
                   << ",\"query_ordinal\":0,\"stage\":\"" << stage
                   << "\",\"wall_ns\":" << duration_ns(start, Clock::now())
                   << "}\n";
        };
        auto start = Clock::now();
        auto prep_only = s6::cuda_v2_prepare_dataset_float32(data, cuda_options);
        require(static_cast<bool>(prep_only.prepared_dataset),
                "preflight dataset preparation failed");
        record("COST-PREP-DATASET-CONSTRUCTION", start);
        auto prepared = std::move(*prep_only.prepared_dataset);
        start = Clock::now();
        auto workspace_only = s6::cuda_v2_create_workspace(prepared, cuda_options);
        require(static_cast<bool>(workspace_only.workspace),
                "preflight workspace construction failed");
        record("COST-V2-WORKSPACE-CONSTRUCTION", start);
        start = Clock::now();
        auto ready_prep = s6::cuda_v2_prepare_dataset_float32(data, cuda_options);
        require(static_cast<bool>(ready_prep.prepared_dataset),
                "preflight ready preparation failed");
        auto ready_dataset = std::move(*ready_prep.prepared_dataset);
        auto ready_workspace_result =
            s6::cuda_v2_create_workspace(ready_dataset, cuda_options);
        require(static_cast<bool>(ready_workspace_result.workspace),
                "preflight ready workspace failed");
        record("COST-PREPARED-READY-CONSTRUCTION", start);
        auto ready_workspace = std::move(*ready_workspace_result.workspace);
        start = Clock::now();
        require(static_cast<bool>(s6::cpu_search_float32(data,query).result),
                "preflight CPU failed");
        record("CPU-E2E-RAW", start);
        start = Clock::now();
        const auto staged = s6::detail::cuda_v2_search_float32_staged_for_benchmark(
            data, query, cuda_options);
        require(staged.production.result && staged.stages.device_timing_available,
                "preflight V2 kernel failed");
        record("BND-KERNEL", start);
        start = Clock::now();
        require(static_cast<bool>(
                    s6::cuda_v2_search_float32(data,query,cuda_options).result),
                "preflight raw V2 failed");
        record("BND-E2E-RAW", start);
        start = Clock::now();
        require(static_cast<bool>(s6::cuda_v2_search_prepared_float32(
                    ready_dataset,query,ready_workspace,cuda_options).result),
                "preflight prepared V2 failed");
        record("BND-E2E-PREP", start);
    }
    require(invocation == 28U, "bounded preflight invocation cap mismatch");
    const auto probe_ns = duration_ns(probe_start, Clock::now());
    constexpr std::uint64_t kFormalLatencyInvocations = 128U * 4U * kTotal;
    constexpr std::uint64_t kFormalConstructionDistributionRows =
        4U * 3U * kRepetitions;
    constexpr std::uint64_t kFormalConstructionInvocations = 4U * 3U * kTotal;
    constexpr std::uint64_t kFormalInvocations =
        kFormalLatencyInvocations + kFormalConstructionInvocations;
    const long double projected_wall_ns =
        static_cast<long double>(probe_ns) * kFormalInvocations / 28.0L;
    const std::string representative_raw =
        "S15-BENCHMARK-RAW-v1,revision,clean,final,single-query,prepared-reuse,"
        "BND-E2E-PREP,v2,1000000,128,9999,256,99999,99,false,999999999,"
        "steady_clock,completed,persistent-resources-reused-destruction-excluded\n";
    const std::string representative_correctness =
        "{\"schema_version\":\"S15-CORRECTNESS-CASE-v1\",\"ordinal\":383,"
        "\"revision\":\"revision\",\"method\":\"v2-prepared\",\"N\":1000000,"
        "\"D\":128,\"query_ordinal\":9999,\"case_status\":\"passed\"}\n";
    const std::string representative_telemetry =
        "{\"schema_version\":\"S14-GPU-TELEMETRY-v1\",\"sequence\":0,"
        "\"captured_at\":\"timestamp\",\"uuid\":\"uuid\"}\n";
    const auto projected_telemetry_rows = static_cast<std::uint64_t>(
        projected_wall_ns / 1000000000.0L) + 1U;
    const auto projected_schema_bytes =
        52400U * representative_raw.size() +
        384U * representative_correctness.size() +
        projected_telemetry_rows * representative_telemetry.size() * 3U;
    std::ofstream projection(output / "preflight.json", std::ios::binary);
    projection << "{\"schema_version\":\"S15-PREFLIGHT-NON-PERFORMANCE-v1\","
                  "\"performance_evidence\":false,\"experiment_config_sha256\":\""
               << options.config_sha256
               << "\",\"formal_workloads\":128,"
                  "\"formal_latency_rows\":51200,\"formal_construction_rows\":"
               << kFormalConstructionDistributionRows << ','
               << "\"formal_correctness_rows\":384,\"formal_invocations\":"
               << kFormalInvocations
               << ",\"probe_invocations\":28,\"automatic_retries\":0,"
                  "\"whole_probe_wall_ns\":" << probe_ns
               << ",\"projected_formal_wall_ns_point\":" << std::fixed
               << std::setprecision(0) << projected_wall_ns
               << ",\"representative_raw_row_bytes\":" << representative_raw.size()
               << ",\"representative_correctness_row_bytes\":"
               << representative_correctness.size()
               << ",\"representative_telemetry_row_bytes\":"
               << representative_telemetry.size()
               << ",\"projected_telemetry_rows\":" << projected_telemetry_rows
               << ",\"projected_schema_bytes_point\":" << projected_schema_bytes
               << ",\"projection_assumption\":\"linear scaling from 28-invocation probe; three representative telemetry JSONL rows per 1000 ms; no safety factor\""
               << ",\"available_bytes\":"
               << fs::space(output).available << "}\n";
    return 0;
}

int self_test(const fs::path& output) {
    fs::create_directories(output);
    for (std::size_t index = 0U; index < kQueryOrdinals.size(); ++index) {
        require(kQueryOrdinals[index] == (index * 9999U) / 31U,
                "approved query formula/list mismatch");
    }
    require(kCandidateCounts.size() * kQueryOrdinals.size() == 128U &&
                128U * 3U == 384U && 128U * 4U * kRepetitions == 51200U &&
                4U * 3U * kRepetitions == 1200U,
            "S15 matrix/sample count mismatch");
    for (std::size_t workload = 0U; workload < 128U; ++workload) {
        for (unsigned iteration = 0U; iteration < kTotal; ++iteration) {
            std::array<bool, 4> seen{};
            const auto rotation = (workload + iteration) % kLanes.size();
            for (std::size_t lane = 0U; lane < kLanes.size(); ++lane)
                seen[(rotation + lane) % kLanes.size()] = true;
            require(std::all_of(seen.begin(), seen.end(), [](bool value){return value;}),
                    "deterministic lane rotation omitted a lane");
        }
    }
    std::ofstream result(output / "integrity-self-test.json", std::ios::binary);
    result << "{\"schema_version\":\"S15-BENCHMARK-INTEGRITY-v1\","
              "\"status\":\"passed\"}\n";
    return 0;
}

void require_fresh_output(const fs::path& output) {
    require(!fs::exists(output), "S15 output directory already exists");
}

void write_command(const fs::path& output, int argc, char** argv) {
    fs::create_directories(output);
    std::ofstream command(output / "benchmark-command.txt", std::ios::binary);
    for (int index = 0; index < argc; ++index) {
        if (index != 0) command << ' ';
        command << std::quoted(argv[index]);
    }
    command << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc >= 3,
                "usage: s15_public_sift1m_benchmark MODE OUTPUT [OPTIONS]");
        const std::string mode = argv[1];
        const fs::path output = argv[2];
        if (mode == "--self-test") return self_test(output);
        require(mode == "--correctness" || mode == "--run" ||
                    mode == "--preflight",
                "unknown S15 benchmark mode");
        require_fresh_output(output);
        const auto options = parse_options(argc, argv, 3);
        const auto loaded =
            s10::load_sift1m_selected(options.dataset_root, query_ordinals());
        require(loaded.status == s10::Sift1mLoadStatus::loaded && loaded.dataset,
                std::string("SIFT1M load failed: ") + loaded.detail);
        validate_loaded_dataset(*loaded.dataset, options);
        write_command(output, argc, argv);
        if (mode == "--correctness")
            return run_correctness(output, options, *loaded.dataset);
        if (mode == "--preflight")
            return run_preflight(output, options, *loaded.dataset);
        return run_performance(output, options, *loaded.dataset);
    } catch (const std::exception& error) {
        std::cerr << "S15 public SIFT1M benchmark: FAIL: " << error.what() << '\n';
        return 1;
    }
}
