#include "s6/cpu_baseline.hpp"
#include "s6/cuda_v2.hpp"
#include "s7_tuning_freeze.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef S14_BENCHMARK_REVISION
#define S14_BENCHMARK_REVISION "unknown"
#endif
#ifndef S14_BENCHMARK_TREE_STATE
#define S14_BENCHMARK_TREE_STATE "unknown"
#endif

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr std::array<std::uint64_t, 4> kCandidateCounts = {
    1024U, 16384U, 262144U, 1048576U};
constexpr std::array<std::uint64_t, 3> kDimensions = {8U, 32U, 128U};
constexpr std::array<std::uint64_t, 3> kBatchSizes = {1U, 8U, 32U};
constexpr std::uint64_t kMaximumBatchSize = 32U;
constexpr unsigned kWarmups = 10U;
constexpr unsigned kRepetitions = 100U;
constexpr unsigned kTotalIterations = kWarmups + kRepetitions;
constexpr std::string_view kGeneratorId = "S14-DISTINCT-BATCH-v1";

struct Workload {
    std::vector<float> dataset;
    std::vector<float> queries;
    std::array<std::uint64_t, kMaximumBatchSize> expected_indices{};
    std::uint64_t candidate_count = 0;
    std::uint64_t dimension = 0;
    std::uint64_t seed = 0;

    s6::DatasetView dataset_view() const {
        return {dataset.data(), candidate_count, dimension, dataset.size()};
    }
    s6::QueryBatchView batch_view(std::uint64_t batch_size) const {
        return {queries.data(), batch_size, dimension,
                static_cast<std::size_t>(batch_size * dimension)};
    }
    s6::QueryView query_view(std::uint64_t query_index) const {
        return {queries.data() + static_cast<std::size_t>(query_index * dimension),
                dimension, static_cast<std::size_t>(dimension)};
    }
    std::string query_set_id(std::uint64_t batch_size) const {
        return std::string(kGeneratorId) + "-S" + std::to_string(seed) + "-N" +
               std::to_string(candidate_count) + "-D" +
               std::to_string(dimension) + "-B" +
               std::to_string(batch_size);
    }
};

struct Sample {
    std::string scenario;
    std::string boundary;
    std::string variant;
    std::uint64_t n = 0;
    std::uint64_t d = 0;
    std::uint64_t batch_size = 0;
    std::uint64_t seed = 0;
    std::uint32_t block_size = 0;
    std::uint64_t run_order = 0;
    std::uint32_t iteration = 0;
    std::uint64_t elapsed_ns = 0;
    std::string resource_scope;
    std::string query_set_id;
};

struct Options {
    std::string experiment_id;
    fs::path experiment_config;
    std::string config_sha256;
    std::string expected_revision;
    std::uint64_t final_seed = 0;
    std::string captured_at;
    std::string timezone;
    std::string generator_id;
    std::string cuda_architecture;
    std::string build_flags;
    std::string stream_policy;
    std::string freeze_sha256;
    fs::path correctness_reference;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message) {
    if (!condition) fail(message);
}

std::string take(int& index, int argc, char** argv, std::string_view option) {
    require(index + 1 < argc, "missing value for " + std::string(option));
    return argv[++index];
}

Options parse_options(int argc, char** argv, int first,
                      bool require_correctness_reference) {
    Options result;
    for (int i = first; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--experiment-id") {
            result.experiment_id = take(i, argc, argv, option);
        } else if (option == "--experiment-config") {
            result.experiment_config = take(i, argc, argv, option);
        } else if (option == "--config-sha256") {
            result.config_sha256 = take(i, argc, argv, option);
        } else if (option == "--expected-revision") {
            result.expected_revision = take(i, argc, argv, option);
        } else if (option == "--final-seed") {
            result.final_seed = std::stoull(take(i, argc, argv, option));
        } else if (option == "--captured-at") {
            result.captured_at = take(i, argc, argv, option);
        } else if (option == "--timezone") {
            result.timezone = take(i, argc, argv, option);
        } else if (option == "--generator-id") {
            result.generator_id = take(i, argc, argv, option);
        } else if (option == "--cuda-architecture") {
            result.cuda_architecture = take(i, argc, argv, option);
        } else if (option == "--build-flags") {
            result.build_flags = take(i, argc, argv, option);
        } else if (option == "--stream-policy") {
            result.stream_policy = take(i, argc, argv, option);
        } else if (option == "--freeze-sha256") {
            result.freeze_sha256 = take(i, argc, argv, option);
        } else if (option == "--correctness-reference") {
            result.correctness_reference = take(i, argc, argv, option);
        } else {
            fail("unknown benchmark option: " + option);
        }
    }
    require(!result.experiment_id.empty(), "missing --experiment-id");
    require(!result.experiment_config.empty() && fs::exists(result.experiment_config),
            "missing or unreadable --experiment-config");
    require(result.config_sha256.size() == 64U, "invalid --config-sha256");
    require(result.expected_revision == S14_BENCHMARK_REVISION,
            "expected revision does not match compiled benchmark revision");
    require(result.final_seed != 0U, "missing --final-seed");
    require(!result.captured_at.empty() && !result.timezone.empty(),
            "capture timestamp and timezone are mandatory");
    require(result.generator_id == kGeneratorId,
            "unexpected S14 query generator identity");
    require(!result.cuda_architecture.empty() && !result.build_flags.empty() &&
                !result.stream_policy.empty(),
            "build and stream metadata are mandatory");
    require(result.freeze_sha256.size() == 64U, "invalid --freeze-sha256");
    if (require_correctness_reference) {
        require(!result.correctness_reference.empty() &&
                    fs::exists(result.correctness_reference),
                "matching correctness reference is mandatory");
    }
    return result;
}

std::uint64_t elapsed_ns(Clock::time_point start, Clock::time_point stop) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count());
}

std::uint32_t bits(float value) {
    std::uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
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

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

Workload make_workload(std::uint64_t n, std::uint64_t d,
                       std::uint64_t seed) {
    require(n >= 1024U && d >= 8U, "S14 workload is outside the approved matrix");
    require(n <= std::numeric_limits<std::size_t>::max() / d,
            "workload element count overflow");
    Workload result;
    result.candidate_count = n;
    result.dimension = d;
    result.seed = seed;
    std::vector<float> base_query(static_cast<std::size_t>(d));
    for (std::uint64_t j = 0; j < d; ++j) {
        const auto centered = static_cast<int>((j + (seed & 15U)) % 17U) - 8;
        base_query[static_cast<std::size_t>(j)] =
            static_cast<float>(centered) / 16.0F;
    }
    result.dataset.resize(static_cast<std::size_t>(n * d));
    for (std::uint64_t i = 0; i < n; ++i) {
        const auto offset = static_cast<std::size_t>(i * d);
        for (std::uint64_t j = 0; j < d; ++j) {
            const std::uint64_t mixed = i * 17U + j * 13U + (seed & 255U);
            const auto centered = static_cast<int>(mixed % 257U) - 128;
            result.dataset[offset + static_cast<std::size_t>(j)] =
                static_cast<float>(centered) / 64.0F;
        }
        result.dataset[offset] = base_query[0] +
                                 static_cast<float>((i % 31U) + 1U) / 64.0F;
    }
    result.queries.resize(static_cast<std::size_t>(kMaximumBatchSize * d));
    for (std::uint64_t q = 0; q < kMaximumBatchSize; ++q) {
        const auto query_offset = static_cast<std::size_t>(q * d);
        std::copy(base_query.begin(), base_query.end(),
                  result.queries.begin() +
                      static_cast<std::ptrdiff_t>(query_offset));
        result.queries[query_offset] =
            4.0F + static_cast<float>(q) / 64.0F;
        const std::uint64_t candidate = ((q + 1U) * n) / 33U;
        result.expected_indices[static_cast<std::size_t>(q)] = candidate;
        std::copy(result.queries.begin() +
                      static_cast<std::ptrdiff_t>(query_offset),
                  result.queries.begin() +
                      static_cast<std::ptrdiff_t>(query_offset + d),
                  result.dataset.begin() +
                      static_cast<std::ptrdiff_t>(candidate * d));
    }
    return result;
}

void require_results(const std::vector<s6::SearchResult>& actual,
                     const std::vector<s6::SearchResult>& expected,
                     std::string_view context) {
    require(actual.size() == expected.size(),
            std::string(context) + " result count differs");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        require(actual[i].index == expected[i].index &&
                    bits(actual[i].squared_distance) ==
                        bits(expected[i].squared_distance),
                std::string(context) + " result differs at query " +
                    std::to_string(i));
    }
}

std::vector<s6::SearchResult> cpu_expected(const Workload& workload,
                                           std::uint64_t batch_size) {
    const auto outcome = s6::cpu_search_batch_float32(
        workload.dataset_view(), workload.batch_view(batch_size));
    require(outcome.invocation_status == s6::InvocationStatus::completed &&
                outcome.results.size() == static_cast<std::size_t>(batch_size),
            "untimed CPU batch reference failed");
    for (std::uint64_t q = 0; q < batch_size; ++q) {
        const auto& result = outcome.results[static_cast<std::size_t>(q)];
        require(result.index == workload.expected_indices[static_cast<std::size_t>(q)] &&
                    bits(result.squared_distance) == 0U,
                "CPU batch reference did not select the unique exact query row");
    }
    return outcome.results;
}

void append_sample(std::vector<Sample>& samples, std::string_view scenario,
                   std::string_view boundary, std::string_view variant,
                   const Workload& workload, std::uint64_t batch_size,
                   std::uint32_t block_size, std::uint64_t& order,
                   std::uint32_t iteration, std::uint64_t value,
                   std::string_view resource_scope) {
    samples.push_back({std::string(scenario), std::string(boundary),
                       std::string(variant), workload.candidate_count,
                       workload.dimension, batch_size, workload.seed, block_size,
                       order++, iteration, value, std::string(resource_scope),
                       workload.query_set_id(batch_size)});
}

void measure_construction(std::vector<Sample>& samples,
                          const Workload& workload, std::uint32_t block_size,
                          std::uint64_t& order) {
    const s6::CudaV2Options options{0, nullptr, block_size};
    for (unsigned iteration = 0; iteration < kTotalIterations; ++iteration) {
        const auto total_start = Clock::now();
        const auto prep_start = Clock::now();
        auto preparation = s6::cuda_v2_prepare_dataset_float32(
            workload.dataset_view(), options);
        const auto prep_stop = Clock::now();
        require(preparation.invocation_status == s6::InvocationStatus::completed &&
                    preparation.prepared_dataset.has_value(),
                "V2 construction dataset preparation failed");
        auto prepared = std::move(*preparation.prepared_dataset);
        const auto workspace_start = Clock::now();
        auto workspace = s6::cuda_v2_create_workspace(prepared, options);
        const auto workspace_stop = Clock::now();
        const auto total_stop = Clock::now();
        require(workspace.invocation_status == s6::InvocationStatus::completed &&
                    workspace.workspace.has_value(),
                "V2 workspace construction failed");
        if (iteration >= kWarmups) {
            const auto retained = iteration - kWarmups;
            append_sample(samples, "prepared-construction",
                          "COST-PREP-DATASET-CONSTRUCTION", "v2", workload,
                          0U, block_size, order, retained,
                          elapsed_ns(prep_start, prep_stop),
                          "persistent-prepared-dataset-destruction-excluded");
            append_sample(samples, "prepared-construction",
                          "COST-V2-WORKSPACE-CONSTRUCTION", "v2", workload,
                          0U, block_size, order, retained,
                          elapsed_ns(workspace_start, workspace_stop),
                          "persistent-v2-workspace-destruction-excluded");
            append_sample(samples, "prepared-construction",
                          "COST-V2-READY-CONSTRUCTION", "v2", workload, 0U,
                          block_size, order, retained,
                          elapsed_ns(total_start, total_stop),
                          "persistent-resource-destruction-excluded-not-lifetime-cost");
        }
    }
    for (const auto batch_size : kBatchSizes) {
        for (unsigned iteration = 0; iteration < kTotalIterations; ++iteration) {
            const auto total_start = Clock::now();
            auto preparation = s6::cuda_v2_prepare_dataset_float32(
                workload.dataset_view(), options);
            require(preparation.invocation_status ==
                        s6::InvocationStatus::completed &&
                        preparation.prepared_dataset.has_value(),
                    "V4 construction dataset preparation failed");
            auto prepared = std::move(*preparation.prepared_dataset);
            const auto workspace_start = Clock::now();
            auto workspace = s6::cuda_v4_create_batch_workspace(
                prepared, batch_size, options);
            const auto workspace_stop = Clock::now();
            const auto total_stop = Clock::now();
            require(workspace.invocation_status == s6::InvocationStatus::completed &&
                        workspace.workspace.has_value(),
                    "V4 batch workspace construction failed");
            if (iteration >= kWarmups) {
                const auto retained = iteration - kWarmups;
                append_sample(samples, "batch-construction",
                              "COST-V4-BATCH-WORKSPACE-CONSTRUCTION", "v4",
                              workload, batch_size, block_size, order, retained,
                              elapsed_ns(workspace_start, workspace_stop),
                              "persistent-v4-workspace-destruction-excluded");
                append_sample(samples, "batch-construction",
                              "COST-V4-BATCH-READY-CONSTRUCTION", "v4",
                              workload, batch_size, block_size, order, retained,
                              elapsed_ns(total_start, total_stop),
                              "persistent-resource-destruction-excluded-not-lifetime-cost");
            }
        }
    }
}

void measure_searches(std::vector<Sample>& samples, const Workload& workload,
                      std::uint32_t block_size, std::size_t workload_ordinal,
                      std::uint64_t& order) {
    const s6::CudaV2Options options{0, nullptr, block_size};
    auto preparation = s6::cuda_v2_prepare_dataset_float32(
        workload.dataset_view(), options);
    require(preparation.invocation_status == s6::InvocationStatus::completed &&
                preparation.prepared_dataset.has_value(),
            "stable V2 dataset preparation failed");
    auto prepared = std::move(*preparation.prepared_dataset);
    auto sequential_workspace_result =
        s6::cuda_v2_create_workspace(prepared, options);
    require(sequential_workspace_result.invocation_status ==
                    s6::InvocationStatus::completed &&
                sequential_workspace_result.workspace.has_value(),
            "stable sequential V2 workspace failed");
    auto sequential_workspace =
        std::move(*sequential_workspace_result.workspace);

    enum class Lane { cpu_batch, v2_sequential, v4_batch };
    constexpr std::array<Lane, 3> lanes = {
        Lane::cpu_batch, Lane::v2_sequential, Lane::v4_batch};
    for (std::size_t batch_ordinal = 0; batch_ordinal < kBatchSizes.size();
         ++batch_ordinal) {
        const auto batch_size = kBatchSizes[batch_ordinal];
        const auto expected = cpu_expected(workload, batch_size);
        auto batch_workspace_result = s6::cuda_v4_create_batch_workspace(
            prepared, batch_size, options);
        require(batch_workspace_result.invocation_status ==
                    s6::InvocationStatus::completed &&
                    batch_workspace_result.workspace.has_value(),
                "stable V4 batch workspace failed");
        auto batch_workspace = std::move(*batch_workspace_result.workspace);
        std::vector<s6::SearchResult> sequential_results(
            static_cast<std::size_t>(batch_size));
        std::vector<s6::ProductionOutcome> sequential_outcomes(
            static_cast<std::size_t>(batch_size));
        for (unsigned iteration = 0; iteration < kTotalIterations; ++iteration) {
            const auto rotation =
                (workload_ordinal * kBatchSizes.size() + batch_ordinal +
                 iteration) %
                lanes.size();
            for (std::size_t lane_index = 0; lane_index < lanes.size();
                 ++lane_index) {
                const auto lane = lanes[(rotation + lane_index) % lanes.size()];
                const auto start = Clock::now();
                if (lane == Lane::cpu_batch) {
                    const auto outcome = s6::cpu_search_batch_float32(
                        workload.dataset_view(), workload.batch_view(batch_size));
                    const auto stop = Clock::now();
                    require(outcome.invocation_status ==
                                s6::InvocationStatus::completed,
                            "measured CPU batch search failed");
                    require_results(outcome.results, expected,
                                    "measured CPU batch");
                    if (iteration >= kWarmups) {
                        append_sample(samples, "batch-search",
                                      "BND-BATCH-E2E-CPU", "cpu-batch",
                                      workload, batch_size, 0U, order,
                                      iteration - kWarmups,
                                      elapsed_ns(start, stop),
                                      "dataset-and-query-validation-included");
                    }
                } else if (lane == Lane::v2_sequential) {
                    for (std::uint64_t q = 0; q < batch_size; ++q) {
                        sequential_outcomes[static_cast<std::size_t>(q)] =
                            s6::cuda_v2_search_prepared_float32(
                            prepared, workload.query_view(q),
                            sequential_workspace, options);
                    }
                    const auto stop = Clock::now();
                    for (std::uint64_t q = 0; q < batch_size; ++q) {
                        const auto& outcome =
                            sequential_outcomes[static_cast<std::size_t>(q)];
                        require(outcome.invocation_status ==
                                    s6::InvocationStatus::completed &&
                                    outcome.result.has_value(),
                                "measured sequential Prepared V2 search failed");
                        sequential_results[static_cast<std::size_t>(q)] =
                            *outcome.result;
                    }
                    require_results(sequential_results, expected,
                                    "measured sequential Prepared V2");
                    if (iteration >= kWarmups) {
                        append_sample(
                            samples, "batch-search",
                            "BND-BATCH-E2E-V2-SEQUENTIAL-PREP", "v2-sequential",
                            workload, batch_size, block_size, order,
                            iteration - kWarmups, elapsed_ns(start, stop),
                            "prepared-dataset-and-v2-workspace-reused-"
                            "persistent-destruction-excluded");
                    }
                } else {
                    const auto outcome =
                        s6::cuda_v4_search_prepared_batch_float32(
                            prepared, workload.batch_view(batch_size),
                            batch_workspace, options);
                    const auto stop = Clock::now();
                    require(outcome.invocation_status ==
                                s6::InvocationStatus::completed,
                            "measured V4 batch search failed");
                    require_results(outcome.results, expected,
                                    "measured V4 batch");
                    if (iteration >= kWarmups) {
                        append_sample(
                            samples, "batch-search", "BND-BATCH-E2E-V4",
                            "v4-batch", workload, batch_size, block_size, order,
                            iteration - kWarmups, elapsed_ns(start, stop),
                            "prepared-dataset-and-v4-workspace-reused-"
                            "persistent-destruction-excluded");
                    }
                }
            }
        }
    }
}

void write_raw(const fs::path& path, const std::vector<Sample>& samples) {
    std::ofstream out(path, std::ios::binary);
    require(static_cast<bool>(out), "cannot create raw_samples.csv");
    out << "schema_version,revision,tree_state,sample_role,workload,scenario,"
           "boundary,variant,N,D,batch_size,seed,block_size,query_set_id,"
           "run_order,iteration,warmup,elapsed_ns,timer,status,resource_scope\n";
    for (const auto& sample : samples) {
        out << "S14-BATCH-BENCHMARK-RAW-v1," << S14_BENCHMARK_REVISION << ','
            << S14_BENCHMARK_TREE_STATE
            << ",final,batch-query-secondary," << sample.scenario << ','
            << sample.boundary << ',' << sample.variant << ',' << sample.n
            << ',' << sample.d << ',' << sample.batch_size << ',' << sample.seed
            << ',' << sample.block_size << ',' << sample.query_set_id << ','
            << sample.run_order << ',' << sample.iteration << ",false,"
            << sample.elapsed_ns
            << ",steady_clock,completed," << sample.resource_scope << '\n';
    }
}

void write_manifest(const fs::path& output, const Options& options,
                    std::string_view freeze_path,
                    const s6::CudaV2DeviceInfo& device) {
    std::ofstream out(output / "manifest.json", std::ios::binary);
    require(static_cast<bool>(out), "cannot create manifest.json");
    out << "{\n"
        << "  \"schema_version\": \"S14-BATCH-BENCHMARK-MANIFEST-v1\",\n"
        << "  \"revision\": \"" << S14_BENCHMARK_REVISION << "\",\n"
        << "  \"working_tree_state\": \"" << S14_BENCHMARK_TREE_STATE
        << "\",\n"
        << "  \"experiment_id\": \"" << json_escape(options.experiment_id)
        << "\",\n"
        << "  \"experiment_config_sha256\": \"" << options.config_sha256
        << "\",\n"
        << "  \"freeze_path\": \"" << json_escape(freeze_path) << "\",\n"
        << "  \"freeze_sha256\": \"" << options.freeze_sha256 << "\",\n"
        << "  \"correctness_reference\": \""
        << json_escape(options.correctness_reference.string()) << "\",\n"
        << "  \"captured_at\": \"" << json_escape(options.captured_at)
        << "\",\n"
        << "  \"timezone\": \"" << json_escape(options.timezone) << "\",\n"
        << "  \"generator_id\": \"" << options.generator_id << "\",\n"
        << "  \"final_seed\": \"" << options.final_seed << "\",\n"
        << "  \"warmups\": 10,\n"
        << "  \"measured_repetitions\": 100,\n"
        << "  \"batch_sizes\": [1, 8, 32],\n"
        << "  \"cuda_architecture\": \""
        << json_escape(options.cuda_architecture) << "\",\n"
        << "  \"build_flags\": \"" << json_escape(options.build_flags)
        << "\",\n"
        << "  \"stream_policy\": \"" << json_escape(options.stream_policy)
        << "\",\n"
        << "  \"gpu_name\": \"" << json_escape(device.name) << "\",\n"
        << "  \"gpu_uuid\": \"" << json_escape(device.uuid) << "\",\n"
        << "  \"gpu_memory_bytes\": " << device.total_global_memory << ",\n"
        << "  \"compute_capability\": \"" << device.compute_major << '.'
        << device.compute_minor << "\",\n"
        << "  \"cuda_driver_version\": " << device.driver_version << ",\n"
        << "  \"cuda_runtime_version\": " << device.runtime_version << ",\n"
        << "  \"cuda_toolkit_version\": " << CUDART_VERSION << ",\n"
        << "  \"host_compiler_msvc\": " << _MSC_VER << ",\n"
        << "  \"dataset_validation\": \"included in CPU batch E2E; prepared GPU lanes reuse validated dataset\",\n"
        << "  \"persistent_resource_destruction\": \"excluded from construction and prepared searches; results are not complete lifetime costs\"\n"
        << "}\n";
}

int self_test(const fs::path& output) {
    fs::create_directories(output);
    const auto workload = make_workload(1024U, 8U, 589684135938649225ULL);
    require(workload.dataset.size() == 8192U &&
                workload.queries.size() == 256U,
            "S14 generator shape self-test failed");
    for (std::uint64_t q = 0; q < kMaximumBatchSize; ++q) {
        const auto expected = ((q + 1U) * 1024U) / 33U;
        require(workload.expected_indices[static_cast<std::size_t>(q)] == expected,
                "S14 expected-index self-test failed");
        require(workload.queries[static_cast<std::size_t>(q * 8U)] ==
                    4.0F + static_cast<float>(q) / 64.0F,
                "S14 distinct-query self-test failed");
    }
    const auto expected = cpu_expected(workload, kMaximumBatchSize);
    require(expected.size() == kMaximumBatchSize,
            "S14 CPU batch self-test failed");
    std::ofstream result(output / "integrity-self-test.json", std::ios::binary);
    require(static_cast<bool>(result), "cannot create S14 integrity result");
    result << "{\"schema_version\":\"S14-BATCH-INTEGRITY-v1\","
              "\"generator_id\":\"S14-DISTINCT-BATCH-v1\","
              "\"status\":\"passed\"}\n";
    return 0;
}

int verify(const fs::path& output, const fs::path& freeze_path,
           const Options& options) {
    require(std::string_view(S14_BENCHMARK_TREE_STATE) == "clean" ||
                std::string_view(S14_BENCHMARK_TREE_STATE) ==
                    "dirty-authorized-local",
            "S14 verification requires an identified build tree state");
    fs::create_directories(output);
    const auto freeze = s7_test::TuningFreeze::load(freeze_path.string(), "v2");
    std::ofstream matrix(output / "distinct-batch-matrix.txt", std::ios::binary);
    require(static_cast<bool>(matrix), "cannot create S14 correctness matrix");
    matrix << "schema=S14-DISTINCT-BATCH-CORRECTNESS-v1\n"
           << "revision=" << S14_BENCHMARK_REVISION << '\n'
           << "working_tree_state=" << S14_BENCHMARK_TREE_STATE << '\n'
           << "generator_id=" << kGeneratorId << '\n';
    std::uint64_t configurations = 0;
    std::uint64_t float64_queries = 0;
    for (const auto n : kCandidateCounts) {
        for (const auto d : kDimensions) {
            const auto workload = make_workload(n, d, options.final_seed);
            const auto expected = cpu_expected(workload, kMaximumBatchSize);
            for (std::uint64_t q = 0; q < kMaximumBatchSize; ++q) {
                const auto reference = s6::compute_float64_reference(
                    workload.dataset_view(), workload.query_view(q));
                require(reference.invocation_status ==
                            s6::InvocationStatus::completed &&
                            reference.argmin_index.has_value() &&
                            *reference.argmin_index ==
                                workload.expected_indices[static_cast<std::size_t>(q)] &&
                            reference.tied_argmin_indices.size() == 1U &&
                            reference.distances[static_cast<std::size_t>(
                                *reference.argmin_index)] == 0.0,
                        "Float64 distinct-query reference failed");
                ++float64_queries;
            }
            const auto block_size = freeze.at(n, d);
            const s6::CudaV2Options cuda_options{0, nullptr, block_size};
            auto preparation = s6::cuda_v2_prepare_dataset_float32(
                workload.dataset_view(), cuda_options);
            require(preparation.invocation_status ==
                        s6::InvocationStatus::completed &&
                        preparation.prepared_dataset.has_value(),
                    "distinct-query prepared dataset failed");
            auto prepared = std::move(*preparation.prepared_dataset);
            auto sequential_workspace_result =
                s6::cuda_v2_create_workspace(prepared, cuda_options);
            auto batch_workspace_result = s6::cuda_v4_create_batch_workspace(
                prepared, kMaximumBatchSize, cuda_options);
            require(sequential_workspace_result.workspace.has_value() &&
                        batch_workspace_result.workspace.has_value(),
                    "distinct-query workspace construction failed");
            auto sequential_workspace =
                std::move(*sequential_workspace_result.workspace);
            auto batch_workspace = std::move(*batch_workspace_result.workspace);
            std::vector<s6::SearchResult> sequential(kMaximumBatchSize);
            for (std::uint64_t q = 0; q < kMaximumBatchSize; ++q) {
                const auto outcome = s6::cuda_v2_search_prepared_float32(
                    prepared, workload.query_view(q), sequential_workspace,
                    cuda_options);
                require(outcome.result.has_value(),
                        "distinct-query sequential V2 failed");
                sequential[static_cast<std::size_t>(q)] = *outcome.result;
            }
            require_results(sequential, expected,
                            "distinct-query sequential V2");
            for (const auto batch_size : kBatchSizes) {
                const auto outcome = s6::cuda_v4_search_prepared_batch_float32(
                    prepared, workload.batch_view(batch_size), batch_workspace,
                    cuda_options);
                require(outcome.invocation_status ==
                            s6::InvocationStatus::completed,
                        "distinct-query V4 batch failed");
                const std::vector<s6::SearchResult> prefix(
                    expected.begin(),
                    expected.begin() + static_cast<std::ptrdiff_t>(batch_size));
                require_results(outcome.results, prefix,
                                "distinct-query V4 batch");
                matrix << "case=N" << n << "-D" << d << "-B" << batch_size
                       << " block_size=" << block_size
                       << " distinct_queries=passed cpu_batch=passed"
                          " sequential_prepared_v2=passed v4_batch=passed"
                          " bitwise_equivalence=passed float64=passed\n";
                ++configurations;
            }
        }
    }
    matrix << "configurations=" << configurations
           << " float64_queries=" << float64_queries << " failed=0 blocked=0\n"
           << "overall=passed\n";
    std::ofstream gate(output / "correctness-gate.json", std::ios::binary);
    require(static_cast<bool>(gate), "cannot create S14 correctness gate");
    gate << "{\n"
         << "  \"schema_version\": \"S14-BATCH-CORRECTNESS-GATE-v1\",\n"
         << "  \"status\": \"passed-"
         << (std::string_view(S14_BENCHMARK_TREE_STATE) == "clean"
                 ? "clean-candidate"
                 : "local-uncommitted-not-acceptance")
         << "\",\n"
         << "  \"revision\": \"" << S14_BENCHMARK_REVISION << "\",\n"
         << "  \"working_tree_state\": \"" << S14_BENCHMARK_TREE_STATE
         << "\",\n"
         << "  \"experiment_id\": \"" << json_escape(options.experiment_id)
         << "\",\n"
         << "  \"experiment_config_sha256\": \"" << options.config_sha256
         << "\",\n"
         << "  \"freeze_sha256\": \"" << options.freeze_sha256 << "\",\n"
         << "  \"generator_id\": \"" << kGeneratorId << "\",\n"
         << "  \"workload_cases\": 12,\n"
         << "  \"batch_configurations\": 36,\n"
         << "  \"float64_queries\": 384,\n"
         << "  \"matrix\": \""
         << json_escape((output / "distinct-batch-matrix.txt").string())
         << "\"\n"
         << "}\n";
    std::cout << "S14 distinct batch correctness: PASS\n";
    return 0;
}

int run(const fs::path& output, const fs::path& freeze_path,
        const Options& options) {
    require(std::string_view(S14_BENCHMARK_TREE_STATE) == "clean",
            "final S14 measurement requires a clean compiled tree identity");
    const auto gate = read_file(options.correctness_reference);
    require(gate.find("\"status\": \"passed-clean-candidate\"") !=
                    std::string::npos &&
                gate.find(S14_BENCHMARK_REVISION) != std::string::npos &&
                gate.find(options.config_sha256) != std::string::npos &&
                gate.find(options.freeze_sha256) != std::string::npos &&
                gate.find(std::string(kGeneratorId)) != std::string::npos,
            "correctness gate identity/status mismatch");
    fs::create_directories(output);
    const auto device = s6::inspect_cuda_v2_device(0);
    require(device.status == s6::EvidenceStatus::passed,
            "CUDA device unavailable: " + device.detail);
    const auto freeze = s7_test::TuningFreeze::load(freeze_path.string(), "v2");
    write_manifest(output, options, freeze_path.string(), device);
    std::vector<Sample> samples;
    samples.reserve(21600U);
    std::uint64_t order = 0;
    std::size_t workload_ordinal = 0;
    for (const auto n : kCandidateCounts) {
        for (const auto d : kDimensions) {
            const auto workload = make_workload(n, d, options.final_seed);
            const auto block_size = freeze.at(n, d);
            measure_construction(samples, workload, block_size, order);
            measure_searches(samples, workload, block_size,
                             workload_ordinal++, order);
        }
    }
    write_raw(output / "raw_samples.csv", samples);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc >= 3,
                "usage: s14_batch_benchmark --self-test OUTPUT or "
                "--verify/--run OUTPUT FREEZE OPTIONS");
        const std::string mode = argv[1];
        const fs::path output = argv[2];
        if (mode == "--self-test") return self_test(output);
        require(argc >= 5, mode + " requires output, freeze, and options");
        const fs::path freeze = argv[3];
        require(fs::exists(freeze), "V2 tuning freeze does not exist");
        if (mode == "--verify") {
            return verify(output, freeze,
                          parse_options(argc, argv, 4, false));
        }
        if (mode == "--run") {
            return run(output, freeze,
                       parse_options(argc, argv, 4, true));
        }
        fail("unknown benchmark mode");
    } catch (const std::exception& error) {
        std::cerr << "S14 batch benchmark: FAIL: " << error.what() << '\n';
        return 1;
    }
}
