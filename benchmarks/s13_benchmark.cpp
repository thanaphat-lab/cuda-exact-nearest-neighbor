#include "s6/cpu_baseline.hpp"
#include "s6/cuda_v2.hpp"
#include "s7_tuning_freeze.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef S13_BENCHMARK_REVISION
#define S13_BENCHMARK_REVISION "unknown"
#endif
#ifndef S13_BENCHMARK_TREE_STATE
#define S13_BENCHMARK_TREE_STATE "unknown"
#endif

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr std::array<std::uint64_t, 4> kCandidateCounts = {
    1024U, 16384U, 262144U, 1048576U};
constexpr std::array<std::uint64_t, 3> kDimensions = {8U, 32U, 128U};

struct Workload {
    std::vector<float> dataset;
    std::vector<float> query;

    s6::DatasetView dataset_view() const {
        return {dataset.data(),
                static_cast<std::uint64_t>(dataset.size() / query.size()),
                static_cast<std::uint64_t>(query.size()), dataset.size()};
    }
    s6::QueryView query_view() const {
        return {query.data(), static_cast<std::uint64_t>(query.size()),
                query.size()};
    }
};

struct Sample {
    std::string scenario;
    std::string boundary;
    std::string variant;
    std::uint64_t n = 0;
    std::uint64_t d = 0;
    std::uint64_t seed = 0;
    std::uint32_t block_size = 0;
    std::uint64_t run_order = 0;
    std::uint32_t iteration = 0;
    std::uint64_t elapsed_ns = 0;
    std::string resource_scope;
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

Options parse_options(int argc, char** argv, int first) {
    Options result;
    for (int i = first; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--experiment-id") result.experiment_id = take(i, argc, argv, option);
        else if (option == "--experiment-config") result.experiment_config = take(i, argc, argv, option);
        else if (option == "--config-sha256") result.config_sha256 = take(i, argc, argv, option);
        else if (option == "--expected-revision") result.expected_revision = take(i, argc, argv, option);
        else if (option == "--final-seed") result.final_seed = std::stoull(take(i, argc, argv, option));
        else if (option == "--captured-at") result.captured_at = take(i, argc, argv, option);
        else if (option == "--timezone") result.timezone = take(i, argc, argv, option);
        else if (option == "--generator-id") result.generator_id = take(i, argc, argv, option);
        else if (option == "--cuda-architecture") result.cuda_architecture = take(i, argc, argv, option);
        else if (option == "--build-flags") result.build_flags = take(i, argc, argv, option);
        else if (option == "--stream-policy") result.stream_policy = take(i, argc, argv, option);
        else if (option == "--freeze-sha256") result.freeze_sha256 = take(i, argc, argv, option);
        else if (option == "--correctness-reference") result.correctness_reference = take(i, argc, argv, option);
        else fail("unknown benchmark option: " + option);
    }
    require(!result.experiment_id.empty(), "missing --experiment-id");
    require(!result.experiment_config.empty() && fs::exists(result.experiment_config),
            "missing or unreadable --experiment-config");
    require(result.config_sha256.size() == 64U, "invalid --config-sha256");
    require(result.expected_revision == S13_BENCHMARK_REVISION,
            "expected revision does not match compiled benchmark revision");
    require(result.final_seed != 0U, "missing --final-seed");
    require(!result.captured_at.empty() && !result.timezone.empty(),
            "capture timestamp and timezone are mandatory");
    require(!result.generator_id.empty() && !result.cuda_architecture.empty() &&
                !result.build_flags.empty() && !result.stream_policy.empty(),
            "generator, build and stream metadata are mandatory");
    require(result.freeze_sha256.size() == 64U, "invalid --freeze-sha256");
    require(!result.correctness_reference.empty() &&
                fs::exists(result.correctness_reference),
            "matching correctness reference is mandatory");
    return result;
}

std::uint64_t elapsed_ns(Clock::time_point start, Clock::time_point stop) {
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

// Kept byte-for-byte equivalent in behavior to S7 rather than refactoring the
// historical harness and invalidating its source identity.
Workload make_workload(std::uint64_t n, std::uint64_t d, std::uint64_t seed) {
    Workload result;
    result.query.resize(static_cast<std::size_t>(d));
    for (std::uint64_t j = 0; j < d; ++j) {
        const auto centered = static_cast<int>((j + (seed & 15U)) % 17U) - 8;
        result.query[static_cast<std::size_t>(j)] =
            static_cast<float>(centered) / 16.0F;
    }
    require(n <= std::numeric_limits<std::size_t>::max() / d,
            "workload element count overflow");
    result.dataset.resize(static_cast<std::size_t>(n * d));
    for (std::uint64_t i = 0; i < n; ++i) {
        const auto offset = static_cast<std::size_t>(i * d);
        for (std::uint64_t j = 0; j < d; ++j) {
            const std::uint64_t mixed = i * 17U + j * 13U + (seed & 255U);
            const auto centered = static_cast<int>(mixed % 257U) - 128;
            result.dataset[offset + static_cast<std::size_t>(j)] =
                static_cast<float>(centered) / 64.0F;
        }
        result.dataset[offset] = result.query[0] +
                                 static_cast<float>((i % 31U) + 1U) / 64.0F;
    }
    const auto expected_offset = static_cast<std::size_t>((n / 2U) * d);
    std::copy(result.query.begin(), result.query.end(),
              result.dataset.begin() + static_cast<std::ptrdiff_t>(expected_offset));
    return result;
}

void require_matching_result(const s6::ProductionOutcome& outcome,
                             const s6::SearchResult& expected,
                             std::string_view context) {
    require(outcome.invocation_status == s6::InvocationStatus::completed &&
                outcome.result.has_value(),
            std::string(context) + " did not complete");
    require(outcome.result->index == expected.index &&
                outcome.result->squared_distance == expected.squared_distance,
            std::string(context) + " disagrees with CPU Float32 result");
}

void append_sample(std::vector<Sample>& samples, std::string_view scenario,
                   std::string_view boundary, std::string_view variant,
                   std::uint64_t n, std::uint64_t d, std::uint64_t seed,
                   std::uint32_t block_size, std::uint64_t& order,
                   std::uint32_t iteration, std::uint64_t value,
                   std::string_view resource_scope) {
    samples.push_back({std::string(scenario), std::string(boundary),
                       std::string(variant), n, d, seed, block_size, order++,
                       iteration, value, std::string(resource_scope)});
}

void measure_case(std::vector<Sample>& samples, const Workload& workload,
                  std::uint64_t n, std::uint64_t d, std::uint64_t seed,
                  std::uint32_t block_size, std::size_t workload_ordinal,
                  std::uint64_t& order) {
    constexpr unsigned kWarmups = 10U;
    constexpr unsigned kRepetitions = 100U;
    constexpr unsigned kTotal = kWarmups + kRepetitions;
    const s6::CudaV2Options cuda_options{0, nullptr, block_size};
    const auto expected_outcome =
        s6::cpu_search_float32(workload.dataset_view(), workload.query_view());
    require(expected_outcome.invocation_status == s6::InvocationStatus::completed &&
                expected_outcome.result.has_value(),
            "untimed CPU correctness reference failed");
    const s6::SearchResult expected = *expected_outcome.result;

    for (unsigned iteration = 0; iteration < kTotal; ++iteration) {
        const auto total_start = Clock::now();
        const auto preparation_start = Clock::now();
        auto preparation = s6::cuda_v2_prepare_dataset_float32(
            workload.dataset_view(), cuda_options);
        const auto preparation_stop = Clock::now();
        require(preparation.invocation_status == s6::InvocationStatus::completed &&
                    preparation.prepared_dataset.has_value(),
                "V2 dataset preparation failed");
        auto prepared = std::move(*preparation.prepared_dataset);
        const auto workspace_start = Clock::now();
        auto workspace_result = s6::cuda_v2_create_workspace(prepared, cuda_options);
        const auto workspace_stop = Clock::now();
        const auto total_stop = Clock::now();
        require(workspace_result.invocation_status == s6::InvocationStatus::completed &&
                    workspace_result.workspace.has_value(),
                "V2 workspace construction failed");
        if (iteration >= kWarmups) {
            const auto retained = iteration - kWarmups;
            append_sample(samples, "prepared-construction",
                          "COST-PREP-DATASET-CONSTRUCTION", "v2", n, d, seed,
                          block_size, order, retained,
                          elapsed_ns(preparation_start, preparation_stop),
                          "persistent-prepared-dataset-destruction-excluded");
            append_sample(samples, "prepared-construction",
                          "COST-WORKSPACE-CONSTRUCTION", "v2", n, d, seed,
                          block_size, order, retained,
                          elapsed_ns(workspace_start, workspace_stop),
                          "persistent-workspace-destruction-excluded");
            append_sample(samples, "prepared-construction",
                          "COST-PREPARED-READY-CONSTRUCTION", "v2", n, d, seed,
                          block_size, order, retained,
                          elapsed_ns(total_start, total_stop),
                          "persistent-resource-destruction-excluded-not-lifetime-cost");
        }
    }

    auto stable_preparation = s6::cuda_v2_prepare_dataset_float32(
        workload.dataset_view(), cuda_options);
    require(stable_preparation.invocation_status == s6::InvocationStatus::completed &&
                stable_preparation.prepared_dataset.has_value(),
            "stable V2 dataset preparation failed");
    auto stable_dataset = std::move(*stable_preparation.prepared_dataset);
    auto stable_workspace_result =
        s6::cuda_v2_create_workspace(stable_dataset, cuda_options);
    require(stable_workspace_result.invocation_status ==
                    s6::InvocationStatus::completed &&
                stable_workspace_result.workspace.has_value(),
            "stable V2 workspace construction failed");
    auto stable_workspace = std::move(*stable_workspace_result.workspace);

    enum class Lane { cpu_raw, v2_raw, v2_prepared };
    constexpr std::array<Lane, 3> lanes = {
        Lane::cpu_raw, Lane::v2_raw, Lane::v2_prepared};
    for (unsigned iteration = 0; iteration < kTotal; ++iteration) {
        const auto rotation = (workload_ordinal + iteration) % lanes.size();
        for (std::size_t lane_index = 0; lane_index < lanes.size(); ++lane_index) {
            const Lane lane = lanes[(rotation + lane_index) % lanes.size()];
            const auto start = Clock::now();
            s6::ProductionOutcome outcome;
            if (lane == Lane::cpu_raw) {
                outcome = s6::cpu_search_float32(workload.dataset_view(),
                                                 workload.query_view());
            } else if (lane == Lane::v2_raw) {
                outcome = s6::cuda_v2_search_float32(
                    workload.dataset_view(), workload.query_view(), cuda_options);
            } else {
                outcome = s6::cuda_v2_search_prepared_float32(
                    stable_dataset, workload.query_view(), stable_workspace,
                    cuda_options);
            }
            const auto stop = Clock::now();
            require_matching_result(outcome, expected, "S13 measured invocation");
            if (iteration < kWarmups) continue;
            const auto retained = iteration - kWarmups;
            if (lane == Lane::cpu_raw) {
                append_sample(samples, "raw-one-shot", "CPU-E2E-RAW", "cpu",
                              n, d, seed, 0U, order, retained,
                              elapsed_ns(start, stop), "not-applicable");
            } else if (lane == Lane::v2_raw) {
                append_sample(samples, "raw-one-shot", "BND-E2E-RAW", "v2",
                              n, d, seed, block_size, order, retained,
                              elapsed_ns(start, stop),
                              "call-owned-transient-cleanup-included");
            } else {
                append_sample(samples, "prepared-reuse", "BND-E2E-PREP", "v2",
                              n, d, seed, block_size, order, retained,
                              elapsed_ns(start, stop),
                              "persistent-resources-reused-destruction-excluded");
            }
        }
    }
}

void write_raw(const fs::path& path, const std::vector<Sample>& samples) {
    std::ofstream out(path, std::ios::binary);
    require(static_cast<bool>(out), "cannot create raw_samples.csv");
    out << "schema_version,revision,tree_state,sample_role,workload,scenario,"
           "boundary,variant,N,D,seed,block_size,run_order,iteration,warmup,"
           "elapsed_ns,timer,status,resource_scope\n";
    for (const auto& sample : samples) {
        out << "S13-BENCHMARK-RAW-v1," << S13_BENCHMARK_REVISION << ','
            << S13_BENCHMARK_TREE_STATE << ",final,single-query,"
            << sample.scenario << ',' << sample.boundary << ',' << sample.variant
            << ',' << sample.n << ',' << sample.d << ',' << sample.seed << ','
            << sample.block_size << ',' << sample.run_order << ','
            << sample.iteration << ",false," << sample.elapsed_ns
            << ",steady_clock,completed," << sample.resource_scope << '\n';
    }
}

void write_manifest(const fs::path& output, const Options& options,
                    std::string_view freeze_path,
                    const s6::CudaV2DeviceInfo& device) {
    std::ofstream out(output / "manifest.json", std::ios::binary);
    require(static_cast<bool>(out), "cannot create manifest.json");
    out << "{\n"
        << "  \"schema_version\": \"S13-BENCHMARK-MANIFEST-v1\",\n"
        << "  \"revision\": \"" << S13_BENCHMARK_REVISION << "\",\n"
        << "  \"working_tree_state\": \"" << S13_BENCHMARK_TREE_STATE << "\",\n"
        << "  \"experiment_id\": \"" << json_escape(options.experiment_id) << "\",\n"
        << "  \"experiment_config_sha256\": \"" << options.config_sha256 << "\",\n"
        << "  \"freeze_path\": \"" << json_escape(freeze_path) << "\",\n"
        << "  \"freeze_sha256\": \"" << options.freeze_sha256 << "\",\n"
        << "  \"correctness_reference\": \"" << json_escape(options.correctness_reference.string()) << "\",\n"
        << "  \"captured_at\": \"" << json_escape(options.captured_at) << "\",\n"
        << "  \"timezone\": \"" << json_escape(options.timezone) << "\",\n"
        << "  \"generator_id\": \"" << json_escape(options.generator_id) << "\",\n"
        << "  \"final_seed\": \"" << options.final_seed << "\",\n"
        << "  \"warmups\": 10,\n"
        << "  \"measured_repetitions\": 100,\n"
        << "  \"cuda_architecture\": \"" << json_escape(options.cuda_architecture) << "\",\n"
        << "  \"build_flags\": \"" << json_escape(options.build_flags) << "\",\n"
        << "  \"stream_policy\": \"" << json_escape(options.stream_policy) << "\",\n"
        << "  \"gpu_name\": \"" << json_escape(device.name) << "\",\n"
        << "  \"gpu_uuid\": \"" << json_escape(device.uuid) << "\",\n"
        << "  \"gpu_memory_bytes\": " << device.total_global_memory << ",\n"
        << "  \"compute_capability\": \"" << device.compute_major << '.'
        << device.compute_minor << "\",\n"
        << "  \"cuda_driver_version\": " << device.driver_version << ",\n"
        << "  \"cuda_runtime_version\": " << device.runtime_version << ",\n"
        << "  \"cuda_toolkit_version\": " << CUDART_VERSION << ",\n"
        << "  \"host_compiler_msvc\": " << _MSC_VER << ",\n"
        << "  \"persistent_resource_destruction\": \"excluded from construction costs and prepared reuse; results are not complete lifetime costs\"\n"
        << "}\n";
}

int self_test(const fs::path& output) {
    fs::create_directories(output);
    constexpr std::uint64_t seed = 589684135938649225ULL;
    const auto workload = make_workload(1024U, 8U, seed);
    require(workload.dataset.size() == 8192U && workload.query.size() == 8U,
            "workload shape self-test failed");
    const auto offset = static_cast<std::size_t>(512U * 8U);
    require(std::equal(workload.query.begin(), workload.query.end(),
                       workload.dataset.begin() + static_cast<std::ptrdiff_t>(offset)),
            "S7 generator identity self-test failed");
    const fs::path freeze_path = output / "freeze.csv";
    {
        std::ofstream freeze(freeze_path, std::ios::binary);
        require(static_cast<bool>(freeze), "cannot create self-test freeze");
        freeze << "schema_version,variant,N,D,block_size\n";
        for (const auto n : kCandidateCounts) {
            for (const auto d : kDimensions) {
                freeze << "S7-TUNING-FREEZE-v1,v2," << n << ',' << d
                       << ",256\n";
            }
        }
    }
    const auto freeze = s7_test::TuningFreeze::load(freeze_path.string(), "v2");
    for (const auto n : kCandidateCounts) {
        for (const auto d : kDimensions) {
            require(freeze.at(n, d) == 256U, "freeze coverage self-test failed");
        }
    }
    std::ofstream result(output / "integrity-self-test.json", std::ios::binary);
    require(static_cast<bool>(result), "cannot create integrity result");
    result << "{\"schema_version\":\"S13-INTEGRITY-v1\",\"status\":\"passed\"}\n";
    return 0;
}

int run(const fs::path& output, const fs::path& freeze_path,
        const Options& options) {
    require(std::string_view(S13_BENCHMARK_TREE_STATE) == "clean",
            "final S13 measurement requires a clean compiled working-tree identity");
    require(fs::exists(freeze_path), "V2 tuning freeze does not exist");
    fs::create_directories(output);
    const auto device = s6::inspect_cuda_v2_device(0);
    require(device.status == s6::EvidenceStatus::passed,
            std::string("CUDA V2 device unavailable: ") + device.detail);
    const auto freeze = s7_test::TuningFreeze::load(freeze_path.string(), "v2");
    write_manifest(output, options, freeze_path.string(), device);
    std::vector<Sample> samples;
    samples.reserve(kCandidateCounts.size() * kDimensions.size() * 600U);
    std::uint64_t order = 0U;
    std::size_t workload_ordinal = 0U;
    for (const auto n : kCandidateCounts) {
        for (const auto d : kDimensions) {
            auto workload = make_workload(n, d, options.final_seed);
            measure_case(samples, workload, n, d, options.final_seed,
                         freeze.at(n, d), workload_ordinal++, order);
        }
    }
    write_raw(output / "raw_samples.csv", samples);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc >= 3,
                "usage: s13_benchmark --self-test OUTPUT or --run OUTPUT FREEZE OPTIONS");
        const std::string mode = argv[1];
        const fs::path output = argv[2];
        if (mode == "--self-test") return self_test(output);
        if (mode == "--run") {
            require(argc >= 5, "--run requires output, freeze and options");
            const fs::path freeze = argv[3];
            return run(output, freeze, parse_options(argc, argv, 4));
        }
        fail("unknown benchmark mode");
    } catch (const std::exception& error) {
        std::cerr << "S13 benchmark: FAIL: " << error.what() << '\n';
        return 1;
    }
}
