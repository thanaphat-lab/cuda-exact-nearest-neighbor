#include "s10_sift1m_loader.hpp"
#include "s6/cpu_baseline.hpp"
#include "s6/cpu_evidence.hpp"
#include "s6/cuda_v5.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sstream>
#include <tuple>
#include <vector>

#ifndef V5_BENCHMARK_REVISION
#define V5_BENCHMARK_REVISION "unknown"
#endif
#ifndef V5_BENCHMARK_TREE_STATE
#define V5_BENCHMARK_TREE_STATE "unknown"
#endif

namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr std::array<std::uint64_t, 4> kNs = {1024U, 16384U, 262144U, 1000000U};
constexpr std::array<std::uint64_t, 3> kBs = {8U, 32U, 128U};
constexpr std::array<std::uint32_t, 3> kTiles = {1U, 4U, 8U};
constexpr std::array<std::uint32_t, 3> kBlocks = {128U, 256U, 512U};
constexpr unsigned kWarmups = 10U;
constexpr unsigned kRetained = 100U;
constexpr unsigned kIterations = kWarmups + kRetained;
constexpr std::string_view kBaseHash =
    "21f66e2975057b5728ba56de1c825bac4f4d89d596609ae985741c6242631816";
constexpr std::string_view kQueryHash =
    "f7fc9be140accdfd64116c2fa2365ecdb69b8f084970c6b0532db5ff79ac8fdc";

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
void require(bool condition, const std::string& message) { if (!condition) fail(message); }

std::uint64_t ns(Clock::time_point begin, Clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
}
std::uint32_t bits(float value) {
    std::uint32_t result = 0U;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
std::string escape(std::string_view input) {
    std::string output;
    for (char c : input) {
        if (c == '\\' || c == '"') output.push_back('\\');
        output.push_back(c);
    }
    return output;
}
std::string read_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::uint64_t> query_ordinals() {
    std::vector<std::uint64_t> result;
    result.reserve(128U);
    for (std::uint64_t k = 0U; k < 128U; ++k) result.push_back(k * 9999U / 127U);
    return result;
}

struct Options {
    std::string experiment_id;
    fs::path config;
    std::string config_sha256;
    std::string expected_revision;
    std::string policy_approval_sha256;
    fs::path correctness_gate;
    std::string correctness_gate_sha256;
    fs::path cpu_manifest;
    fs::path fixture_evidence;
    std::string captured_at;
    std::string timezone;
    std::string cuda_architecture;
    std::string build_flags;
};

std::string take(int& index, int argc, char** argv, const std::string& option) {
    require(index + 1 < argc, "missing value for " + option);
    return argv[++index];
}
Options parse_options(int argc, char** argv, int first) {
    Options result;
    for (int i = first; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--experiment-id") result.experiment_id = take(i, argc, argv, option);
        else if (option == "--experiment-config") result.config = take(i, argc, argv, option);
        else if (option == "--config-sha256") result.config_sha256 = take(i, argc, argv, option);
        else if (option == "--expected-revision") result.expected_revision = take(i, argc, argv, option);
        else if (option == "--policy-approval-sha256") result.policy_approval_sha256 = take(i, argc, argv, option);
        else if (option == "--correctness-gate") result.correctness_gate = take(i, argc, argv, option);
        else if (option == "--correctness-gate-sha256") result.correctness_gate_sha256 = take(i, argc, argv, option);
        else if (option == "--cpu-manifest") result.cpu_manifest = take(i, argc, argv, option);
        else if (option == "--fixture-evidence") result.fixture_evidence = take(i, argc, argv, option);
        else if (option == "--captured-at") result.captured_at = take(i, argc, argv, option);
        else if (option == "--timezone") result.timezone = take(i, argc, argv, option);
        else if (option == "--cuda-architecture") result.cuda_architecture = take(i, argc, argv, option);
        else if (option == "--build-flags") result.build_flags = take(i, argc, argv, option);
        else fail("unknown V5 benchmark option: " + option);
    }
    require(!result.experiment_id.empty() && fs::exists(result.config),
            "experiment identity/config are mandatory");
    require(result.config_sha256.size() == 64U &&
                result.expected_revision == V5_BENCHMARK_REVISION,
            "config or compiled revision identity mismatch");
    require(!result.captured_at.empty() && !result.timezone.empty() &&
                !result.cuda_architecture.empty() && !result.build_flags.empty(),
            "capture/build identity is mandatory");
    return result;
}

s6::QueryBatchView batch_view(const s10::LoadedSift1m& data, std::uint64_t b) {
    return {data.selected_queries_aos.data(), b, 128U,
            static_cast<std::size_t>(b * 128U)};
}
s6::QueryView query_view(const s10::LoadedSift1m& data, std::uint64_t k) {
    return {data.selected_queries_aos.data() + static_cast<std::size_t>(k * 128U),
            128U, 128U};
}
void validate_dataset(const s10::LoadedSift1m& data) {
    require(data.base_sha256 == kBaseHash && data.query_sha256 == kQueryHash &&
                data.base_records == 1000000U && data.dimension == 128U &&
                data.selected_query_ordinals == query_ordinals(),
            "SIFT1M identity/schema/query-selection mismatch");
}
void require_results(const s6::BatchProductionOutcome& outcome,
                     const std::vector<s6::SearchResult>& expected,
                     std::string_view label) {
    require(outcome.invocation_status == s6::InvocationStatus::completed &&
                outcome.results.size() == expected.size(),
            std::string(label) + " did not complete");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        require(outcome.results[i].index == expected[i].index &&
                    bits(outcome.results[i].squared_distance) ==
                        bits(expected[i].squared_distance),
                std::string(label) + " differs from CPU Float32 at query " +
                    std::to_string(i));
    }
}

struct Candidate {
    std::uint32_t tile = 0U;
    std::uint32_t block = 0U;
};
std::array<Candidate, 9> candidates() {
    std::array<Candidate, 9> result{};
    std::size_t index = 0U;
    for (auto tile : kTiles) for (auto block : kBlocks) result[index++] = {tile, block};
    return result;
}

struct Sample {
    std::string phase, boundary, timer, scope;
    std::uint64_t n = 0U, b = 0U, order = 0U, elapsed = 0U;
    std::uint32_t tile = 0U, block = 0U, iteration = 0U;
};
void append(std::vector<Sample>& rows, std::string phase, std::string boundary,
            std::uint64_t n, std::uint64_t b, Candidate candidate,
            std::uint64_t& order, unsigned iteration, std::uint64_t elapsed,
            std::string timer, std::string scope) {
    if (iteration < kWarmups) return;
    rows.push_back({std::move(phase), std::move(boundary), std::move(timer),
                    std::move(scope), n, b, order++, elapsed, candidate.tile,
                    candidate.block, iteration - kWarmups});
}
void write_samples(const fs::path& path, const std::vector<Sample>& rows) {
    std::ofstream out(path, std::ios::binary);
    require(static_cast<bool>(out), "cannot create V5 raw samples");
    out << "schema_version,revision,tree_state,phase,workload,boundary,N,D,B,query_tile,block_size,run_order,iteration,warmup,elapsed_ns,timer,status,resource_scope\n";
    for (const auto& row : rows) {
        out << "V5-BATCH-RAW-v1," << V5_BENCHMARK_REVISION << ','
            << V5_BENCHMARK_TREE_STATE << ',' << row.phase
            << ",optional-secondary-batch-optimization," << row.boundary << ','
            << row.n << ",128," << row.b << ',' << row.tile << ',' << row.block
            << ',' << row.order << ',' << row.iteration << ",false," << row.elapsed
            << ',' << row.timer << ",completed," << row.scope << '\n';
    }
}
void write_manifest(const fs::path& output, std::string_view phase,
                    const Options& options, const s10::LoadedSift1m& data,
                    std::string_view freeze_sha = "") {
    std::ofstream out(output / "manifest.json", std::ios::binary);
    require(static_cast<bool>(out), "cannot create V5 manifest");
    out << "{\n  \"schema_version\": \"V5-BATCH-MANIFEST-v1\",\n"
        << "  \"phase\": \"" << phase << "\",\n"
        << "  \"revision\": \"" << V5_BENCHMARK_REVISION << "\",\n"
        << "  \"working_tree_state\": \"" << V5_BENCHMARK_TREE_STATE << "\",\n"
        << "  \"experiment_id\": \"" << escape(options.experiment_id) << "\",\n"
        << "  \"experiment_config_sha256\": \"" << options.config_sha256 << "\",\n"
        << "  \"policy_approval_sha256\": \"" << options.policy_approval_sha256 << "\",\n"
        << "  \"correctness_gate_sha256\": \"" << options.correctness_gate_sha256 << "\",\n"
        << "  \"freeze_sha256\": \"" << freeze_sha << "\",\n"
        << "  \"sift_base_sha256\": \"" << data.base_sha256 << "\",\n"
        << "  \"sift_query_sha256\": \"" << data.query_sha256 << "\",\n"
        << "  \"captured_at\": \"" << escape(options.captured_at) << "\",\n"
        << "  \"timezone\": \"" << escape(options.timezone) << "\",\n"
        << "  \"cuda_architecture\": \"" << escape(options.cuda_architecture) << "\",\n"
        << "  \"build_flags\": \"" << escape(options.build_flags) << "\",\n"
        << "  \"warmups\": 10,\n  \"measured_repetitions\": 100\n}\n";
}

void require_activation(const Options& options, bool performance_phase) {
    require(std::string_view(V5_BENCHMARK_TREE_STATE) == "clean",
            "V5 evidence requires a clean compiled tree identity");
    if (!performance_phase) return;
    require(options.policy_approval_sha256.size() == 64U,
            "Owner-approved experiment-policy binding is mandatory");
    require(fs::exists(options.correctness_gate) &&
                options.correctness_gate_sha256.size() == 64U,
            "matching V5 correctness gate is mandatory");
    const auto gate = read_text(options.correctness_gate);
    require(gate.find("\"status\": \"passed-clean-candidate\"") != std::string::npos &&
                gate.find(V5_BENCHMARK_REVISION) != std::string::npos &&
                gate.find(options.config_sha256) != std::string::npos,
            "V5 correctness gate status/identity mismatch");
}

int self_test(const fs::path& output) {
    fs::create_directories(output);
    const auto ordinals = query_ordinals();
    require(ordinals.size() == 128U && ordinals.front() == 0U &&
                ordinals.back() == 9999U &&
                std::adjacent_find(ordinals.begin(), ordinals.end()) == ordinals.end(),
            "V5 128-query formula self-test failed");
    const auto all = candidates();
    require(all.front().tile == 1U && all.front().block == 128U &&
                all.back().tile == 8U && all.back().block == 512U,
            "V5 candidate ordering self-test failed");
    std::ofstream out(output / "integrity-self-test.json", std::ios::binary);
    out << "{\"schema_version\":\"V5-BATCH-INTEGRITY-v1\","
           "\"query_count\":128,\"workload_count\":12,"
           "\"candidate_count\":9,\"status\":\"passed\"}\n";
    return 0;
}

bool accepted(s6::NnVerdict verdict) {
    return verdict == s6::NnVerdict::accepted_unambiguous ||
           verdict == s6::NnVerdict::accepted_ambiguous;
}

int correctness(const fs::path& output, const Options& options,
                const s10::LoadedSift1m& loaded) {
    require_activation(options, false);
    require(fs::exists(options.cpu_manifest) && fs::exists(options.fixture_evidence),
            "CPU numerical evidence inputs are mandatory");
    const auto build = s6::verify_cpu_build_evidence(options.cpu_manifest.string());
    require(build.identity_status() == s6::EvidenceStatus::passed &&
                build.graph_status() == s6::EvidenceStatus::passed,
            "CPU build numerical evidence did not pass");
    std::string gate_detail;
    const auto gates = s6::load_cpu_fixture_evidence(
        options.fixture_evidence.string(), build.source_identity(), gate_detail);
    require(gates.fixture_status == s6::EvidenceStatus::passed &&
                gates.oracle_status == s6::EvidenceStatus::passed &&
                gates.tie_status == s6::EvidenceStatus::passed &&
                gates.reduction_status == s6::EvidenceStatus::passed,
            "CPU fixture/oracle/tie/reduction evidence did not pass");
    fs::create_directories(output);
    std::ofstream ledger(output / "correctness-cases.jsonl", std::ios::binary);
    std::uint64_t numerical_cases = 0U, gpu_batches = 0U;
    const auto all = candidates();
    for (auto n : kNs) {
        const auto dataset = loaded.prefix(n);
        std::vector<s6::SearchResult> expected128;
        expected128.reserve(128U);
        for (std::uint64_t q = 0U; q < 128U; ++q) {
            const auto query = query_view(loaded, q);
            const auto cpu = s6::cpu_search_float32_evidenced(dataset, query, build);
            const auto reference = s6::compute_float64_reference(dataset, query);
            s6::ValidationOptions validation;
            validation.production_evidence = &cpu.evidence;
            validation.mandatory_gate_evidence = &gates;
            const auto report = s6::validate_cpu_search(
                dataset, query, cpu.production, reference, validation);
            require(cpu.production.result && accepted(report.nn_verdict) &&
                        report.production_match_status == s6::EvidenceStatus::passed &&
                        report.reference_match_status == s6::EvidenceStatus::passed &&
                        report.exact_ambiguity_known,
                    "CPU/Float64 V5 workload was not numerically accepted");
            expected128.push_back(*cpu.production.result);
            ledger << "{\"schema_version\":\"V5-CORRECTNESS-CASE-v1\","
                   << "\"kind\":\"cpu-f32-vs-f64\",\"N\":" << n
                   << ",\"query_ordinal\":" << query_ordinals()[q]
                   << ",\"argmin_status\":\"passed\","
                      "\"distance_error_status\":\"passed\","
                      "\"ambiguity_status\":\"passed\"}\n";
            ++numerical_cases;
        }
        auto prepared_result = s6::cuda_v5_prepare_dataset_float32(
            dataset, s6::CudaV5Options{0, nullptr, 128U, 1U});
        require(prepared_result.prepared_dataset.has_value(),
                "V5 correctness dataset preparation failed");
        auto prepared = std::move(*prepared_result.prepared_dataset);
        for (auto b : kBs) {
            const auto cpu_batch = s6::cpu_search_batch_float32(dataset, batch_view(loaded, b));
            require_results(cpu_batch,
                std::vector<s6::SearchResult>(expected128.begin(), expected128.begin() + b),
                "CPU batch");
            for (const auto candidate : all) {
                const s6::CudaV5Options cuda{0, nullptr, candidate.block, candidate.tile};
                auto workspace_result = s6::cuda_v5_create_workspace(prepared, b, cuda);
                require(workspace_result.workspace.has_value(),
                        "V5 correctness workspace construction failed");
                auto workspace = std::move(*workspace_result.workspace);
                const auto gpu = s6::cuda_v5_search_prepared_batch_float32(
                    prepared, batch_view(loaded, b), workspace, cuda);
                require_results(gpu, cpu_batch.results, "V5 correctness candidate");
                ledger << "{\"schema_version\":\"V5-CORRECTNESS-CASE-v1\","
                       << "\"kind\":\"v5-candidate-batch\",\"N\":" << n
                       << ",\"B\":" << b << ",\"query_tile\":"
                       << candidate.tile << ",\"block_size\":" << candidate.block
                       << ",\"float32_bitwise_status\":\"passed\"}\n";
                ++gpu_batches;
                const auto raw = s6::cuda_v5_search_batch_float32(
                    dataset, batch_view(loaded, b), cuda);
                require_results(raw, cpu_batch.results, "V5 raw correctness candidate");
                ledger << "{\"schema_version\":\"V5-CORRECTNESS-CASE-v1\","
                       << "\"kind\":\"v5-raw-candidate-batch\",\"N\":" << n
                       << ",\"B\":" << b << ",\"query_tile\":"
                       << candidate.tile << ",\"block_size\":" << candidate.block
                       << ",\"float32_bitwise_status\":\"passed\"}\n";
                ++gpu_batches;
            }
        }
    }
    ledger.close();
    require(numerical_cases == 512U && gpu_batches == 216U,
            "V5 correctness matrix is incomplete");
    const auto ledger_sha = s10::detail::sha256_file(output / "correctness-cases.jsonl");
    std::ofstream gate(output / "correctness-gate.json", std::ios::binary);
    gate << "{\n  \"schema_version\": \"V5-CORRECTNESS-GATE-v1\",\n"
         << "  \"status\": \"passed-clean-candidate\",\n"
         << "  \"revision\": \"" << V5_BENCHMARK_REVISION << "\",\n"
         << "  \"working_tree_state\": \"clean\",\n"
         << "  \"experiment_config_sha256\": \"" << options.config_sha256 << "\",\n"
         << "  \"policy_approval_sha256\": \"" << options.policy_approval_sha256 << "\",\n"
         << "  \"correctness_ledger_sha256\": \"" << ledger_sha << "\",\n"
         << "  \"cpu_float32_float64_cases\": 512,\n"
         << "  \"v5_candidate_batch_cases\": 216,\n"
         << "  \"mandatory_workloads\": 12,\n"
         << "  \"performance_authorized\": true\n}\n";
    write_manifest(output, "correctness", options, loaded);
    return 0;
}

int diagnostic(const fs::path& output, const Options& options,
               const s10::LoadedSift1m& loaded) {
    require_activation(options, true);
    fs::create_directories(output);
    write_manifest(output, "diagnostic", options, loaded);
    std::vector<Sample> samples;
    std::uint64_t order = 0U, workload = 0U;
    const auto all = candidates();
    for (auto n : kNs) for (auto b : kBs) {
        const auto dataset = loaded.prefix(n);
        const auto queries = batch_view(loaded, b);
        const auto expected = s6::cpu_search_batch_float32(dataset, queries);
        require(expected.invocation_status == s6::InvocationStatus::completed,
                "diagnostic CPU reference failed");
        auto preparation = s6::cuda_v5_prepare_dataset_float32(
            dataset, s6::CudaV5Options{0, nullptr, 128U, 1U});
        require(preparation.prepared_dataset.has_value(),
                "diagnostic V5 preparation failed");
        auto prepared = std::move(*preparation.prepared_dataset);
        std::vector<s6::CudaV5Workspace> workspaces;
        workspaces.reserve(all.size());
        for (const auto candidate : all) {
            const s6::CudaV5Options cuda{0, nullptr, candidate.block, candidate.tile};
            auto result = s6::cuda_v5_create_workspace(prepared, b, cuda);
            require(result.workspace.has_value(), "diagnostic workspace failed");
            workspaces.push_back(std::move(*result.workspace));
        }
        for (unsigned iteration = 0U; iteration < kIterations; ++iteration) {
            const auto rotation = (workload + iteration) % all.size();
            for (std::size_t j = 0U; j < all.size(); ++j) {
                const auto index = (rotation + j) % all.size();
                const auto candidate = all[index];
                const s6::CudaV5Options cuda{0, nullptr, candidate.block, candidate.tile};
                const auto start = Clock::now();
                const auto result = s6::cuda_v5_search_prepared_batch_float32(
                    prepared, queries, workspaces[index], cuda);
                const auto stop = Clock::now();
                require_results(result, expected.results, "diagnostic V5 candidate");
                append(samples, "diagnostic", "V5-BATCH-PREPARED", n, b,
                       candidate, order, iteration, ns(start, stop), "steady_clock",
                       "prepared-resources-reused-destruction-excluded");
            }
        }
        ++workload;
    }
    write_samples(output / "raw_samples.csv", samples);
    return 0;
}

using FreezeKey = std::pair<std::uint64_t, std::uint64_t>;
std::map<FreezeKey, Candidate> read_freeze(const fs::path& path) {
    std::ifstream input(path);
    require(static_cast<bool>(input), "cannot read V5 selection freeze");
    std::string line;
    require(static_cast<bool>(std::getline(input, line)) &&
                line == "N,B,query_tile,block_size,diagnostic_median_ns",
            "invalid V5 selection freeze header");
    std::map<FreezeKey, Candidate> result;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        std::uint64_t n = 0U, b = 0U;
        double median = 0.0;
        Candidate candidate;
        require(static_cast<bool>(row >> n >> b >> candidate.tile >> candidate.block >> median) &&
                    std::isfinite(median) && median > 0.0 &&
                    s6::cuda_v5_is_supported_query_tile(candidate.tile) &&
                    s6::cuda_v5_is_supported_block_size(candidate.block) &&
                    result.emplace(FreezeKey{n, b}, candidate).second,
                "invalid/duplicate V5 selection freeze row");
    }
    require(result.size() == 12U, "V5 selection freeze must contain 12 workloads");
    for (auto n : kNs) for (auto b : kBs)
        require(result.count({n, b}) == 1U, "V5 selection freeze is incomplete");
    return result;
}

void measure_construction(std::vector<Sample>& samples, s6::DatasetView dataset,
                          std::uint64_t n, std::uint64_t b, Candidate candidate,
                          std::uint64_t& order) {
    const s6::CudaV5Options cuda{0, nullptr, candidate.block, candidate.tile};
    for (unsigned iteration = 0U; iteration < kIterations; ++iteration) {
        auto start = Clock::now();
        auto prepared_only = s6::cuda_v5_prepare_dataset_float32(dataset, cuda);
        auto stop = Clock::now();
        require(prepared_only.prepared_dataset.has_value(), "construction preparation failed");
        append(samples, "formal", "V5-PREPARED-DATASET-CONSTRUCTION", n, b,
               candidate, order, iteration, ns(start, stop), "steady_clock",
               "persistent-destruction-excluded");
        auto stable_prepared = std::move(*prepared_only.prepared_dataset);
        start = Clock::now();
        auto workspace_only = s6::cuda_v5_create_workspace(stable_prepared, b, cuda);
        stop = Clock::now();
        require(workspace_only.workspace.has_value(), "construction workspace failed");
        append(samples, "formal", "V5-WORKSPACE-CONSTRUCTION", n, b,
               candidate, order, iteration, ns(start, stop), "steady_clock",
               "persistent-destruction-excluded");
        start = Clock::now();
        auto ready_prepared = s6::cuda_v5_prepare_dataset_float32(dataset, cuda);
        require(ready_prepared.prepared_dataset.has_value(), "ready preparation failed");
        auto ready_workspace = s6::cuda_v5_create_workspace(
            *ready_prepared.prepared_dataset, b, cuda);
        stop = Clock::now();
        require(ready_workspace.workspace.has_value(), "ready workspace failed");
        append(samples, "formal", "V5-PREPARED-READY-CONSTRUCTION", n, b,
               candidate, order, iteration, ns(start, stop), "steady_clock",
               "persistent-destruction-excluded-not-lifetime-cost");
    }
}

int formal(const fs::path& output, const fs::path& freeze_path,
           std::string_view freeze_sha, const Options& options,
           const s10::LoadedSift1m& loaded) {
    require_activation(options, true);
    require(freeze_sha.size() == 64U, "selection freeze SHA-256 is mandatory");
    const auto freeze = read_freeze(freeze_path);
    fs::create_directories(output);
    write_manifest(output, "formal", options, loaded, freeze_sha);
    std::vector<Sample> samples;
    std::uint64_t order = 0U, workload = 0U;
    enum class Lane { cpu, raw, prepared, kernel };
    constexpr std::array<Lane, 4> lanes = {Lane::cpu, Lane::raw,
                                           Lane::prepared, Lane::kernel};
    for (auto n : kNs) for (auto b : kBs) {
        const auto candidate = freeze.at({n, b});
        const s6::CudaV5Options cuda{0, nullptr, candidate.block, candidate.tile};
        const auto dataset = loaded.prefix(n);
        const auto queries = batch_view(loaded, b);
        const auto expected = s6::cpu_search_batch_float32(dataset, queries);
        require(expected.invocation_status == s6::InvocationStatus::completed,
                "formal CPU reference failed");
        measure_construction(samples, dataset, n, b, candidate, order);
        auto preparation = s6::cuda_v5_prepare_dataset_float32(dataset, cuda);
        require(preparation.prepared_dataset.has_value(), "formal preparation failed");
        auto prepared = std::move(*preparation.prepared_dataset);
        auto workspace_result = s6::cuda_v5_create_workspace(prepared, b, cuda);
        require(workspace_result.workspace.has_value(), "formal workspace failed");
        auto workspace = std::move(*workspace_result.workspace);
        for (unsigned iteration = 0U; iteration < kIterations; ++iteration) {
            const auto rotation = (workload + iteration) % lanes.size();
            for (std::size_t j = 0U; j < lanes.size(); ++j) {
                const auto lane = lanes[(rotation + j) % lanes.size()];
                const auto start = Clock::now();
                if (lane == Lane::cpu) {
                    const auto result = s6::cpu_search_batch_float32(dataset, queries);
                    const auto stop = Clock::now();
                    require_results(result, expected.results, "formal CPU batch");
                    append(samples, "formal", "CPU-BATCH-RAW", n, b, candidate,
                           order, iteration, ns(start, stop), "steady_clock",
                           "dataset-and-query-validation-included");
                } else if (lane == Lane::raw) {
                    const auto result = s6::cuda_v5_search_batch_float32(dataset, queries, cuda);
                    const auto stop = Clock::now();
                    require_results(result, expected.results, "formal V5 raw");
                    append(samples, "formal", "V5-BATCH-RAW", n, b, candidate,
                           order, iteration, ns(start, stop), "steady_clock",
                           "call-owned-transient-resources-and-cleanup-included");
                } else if (lane == Lane::prepared) {
                    const auto result = s6::cuda_v5_search_prepared_batch_float32(
                        prepared, queries, workspace, cuda);
                    const auto stop = Clock::now();
                    require_results(result, expected.results, "formal V5 prepared");
                    append(samples, "formal", "V5-BATCH-PREPARED", n, b, candidate,
                           order, iteration, ns(start, stop), "steady_clock",
                           "prepared-resources-reused-destruction-excluded");
                } else {
                    const auto staged =
                        s6::detail::cuda_v5_search_prepared_batch_float32_for_benchmark(
                            prepared, queries, workspace, cuda);
                    require(staged.device_timing_available,
                            "formal V5 CUDA-event kernel timing unavailable: " +
                                staged.timing_detail);
                    require_results(staged.production, expected.results,
                                    "formal V5 kernel lane");
                    append(samples, "formal", "V5-BATCH-KERNEL", n, b, candidate,
                           order, iteration, staged.kernel_ns, "cuda_event",
                           "device-resident-complete-kernel-sequence");
                }
            }
        }
        ++workload;
    }
    write_samples(output / "raw_samples.csv", samples);
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc >= 3, "usage: v5_batch_benchmark MODE OUTPUT [DATASET] [FREEZE] OPTIONS");
        const std::string mode = argv[1];
        const fs::path output = argv[2];
        if (mode == "--self-test") return self_test(output);
        require(argc >= 5, "V5 non-self-test mode requires output and dataset root");
        const fs::path dataset_root = argv[3];
        int option_start = 4;
        fs::path freeze;
        std::string freeze_sha;
        if (mode == "--formal") {
            require(argc >= 7, "V5 formal mode requires selection freeze and SHA-256");
            freeze = argv[4];
            freeze_sha = argv[5];
            option_start = 6;
        }
        const auto options = parse_options(argc, argv, option_start);
        const auto loaded = s10::load_sift1m_selected(dataset_root, query_ordinals());
        require(loaded.status == s10::Sift1mLoadStatus::loaded && loaded.dataset,
                "SIFT1M load failed: " + loaded.detail);
        validate_dataset(*loaded.dataset);
        if (mode == "--correctness") return correctness(output, options, *loaded.dataset);
        if (mode == "--diagnostic") return diagnostic(output, options, *loaded.dataset);
        if (mode == "--formal")
            return formal(output, freeze, freeze_sha, options, *loaded.dataset);
        fail("unknown V5 benchmark mode");
    } catch (const std::exception& error) {
        std::cerr << "V5 batch benchmark: FAIL: " << error.what() << '\n';
        return 1;
    }
}
