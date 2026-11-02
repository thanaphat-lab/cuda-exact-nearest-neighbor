#include "s6/benchmark_internal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef S7_BENCHMARK_REVISION
#define S7_BENCHMARK_REVISION "unknown"
#endif
#ifndef S7_BENCHMARK_TREE_STATE
#define S7_BENCHMARK_TREE_STATE "unknown"
#endif

namespace {

using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

constexpr std::uint64_t kTuningSeed = 0x243f6a8885a308d3ULL;
constexpr std::uint64_t kFinalSeed = 0x13198a2e03707344ULL;
constexpr std::array<std::uint64_t, 4> kCandidateCounts = {
    1024U, 16384U, 262144U, 1048576U};
constexpr std::array<std::uint64_t, 3> kDimensions = {8U, 32U, 128U};
constexpr std::array<std::uint32_t, 3> kBlockCandidates = {128U, 256U, 512U};

struct Workload {
    std::vector<float> dataset;
    std::vector<float> query;
    s6::DatasetView dataset_view() const {
        return {dataset.data(), static_cast<std::uint64_t>(dataset.size() / query.size()),
                static_cast<std::uint64_t>(query.size()), dataset.size()};
    }
    s6::QueryView query_view() const {
        return {query.data(), static_cast<std::uint64_t>(query.size()), query.size()};
    }
};

struct Sample {
    std::string role;
    std::string variant;
    std::string boundary;
    std::uint64_t n = 0;
    std::uint64_t d = 0;
    std::uint64_t seed = 0;
    std::uint32_t block_size = 0;
    std::uint32_t iteration = 0;
    std::uint64_t elapsed_ns = 0;
    std::string timer;
    std::string status;
};

struct Summary {
    std::string role;
    std::string variant;
    std::string boundary;
    std::uint64_t n = 0;
    std::uint64_t d = 0;
    std::uint64_t seed = 0;
    std::uint32_t block_size = 0;
    std::size_t count = 0;
    double median_ns = 0.0;
    double p5_ns = 0.0;
    double p95_ns = 0.0;
    double ci_low_ns = 0.0;
    double ci_high_ns = 0.0;
};

struct FrozenConfiguration {
    std::string variant;
    std::uint64_t n = 0;
    std::uint64_t d = 0;
    std::uint32_t block_size = 0;
    double tuning_median_ns = 0.0;
};

enum class RunMode { dry_run, tuning, final };

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::uint64_t elapsed_ns(Clock::time_point start, Clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

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

double percentile(std::vector<std::uint64_t> values, double q) {
    require(!values.empty(), "percentile requires samples");
    std::sort(values.begin(), values.end());
    const double position = q * static_cast<double>(values.size() - 1U);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    const double fraction = position - static_cast<double>(lower);
    return static_cast<double>(values[lower]) * (1.0 - fraction) +
           static_cast<double>(values[upper]) * fraction;
}

std::pair<double, double> bootstrap_median_ci(
    const std::vector<std::uint64_t>& values, std::uint64_t seed) {
    require(!values.empty(), "bootstrap requires samples");
    constexpr std::size_t kBootstrapReplicates = 2000U;
    std::mt19937_64 generator(seed);
    std::uniform_int_distribution<std::size_t> choose(0U, values.size() - 1U);
    std::vector<double> medians;
    std::vector<std::uint64_t> resample(values.size());
    medians.reserve(kBootstrapReplicates);
    for (std::size_t r = 0; r < kBootstrapReplicates; ++r) {
        for (auto& value : resample) {
            value = values[choose(generator)];
        }
        medians.push_back(percentile(resample, 0.5));
    }
    std::sort(medians.begin(), medians.end());
    const auto at = [&](double q) {
        const double position = q * static_cast<double>(medians.size() - 1U);
        const auto lower = static_cast<std::size_t>(std::floor(position));
        const auto upper = static_cast<std::size_t>(std::ceil(position));
        const double fraction = position - static_cast<double>(lower);
        return medians[lower] * (1.0 - fraction) + medians[upper] * fraction;
    };
    return {at(0.025), at(0.975)};
}

std::string json_escape(std::string_view value) {
    std::ostringstream out;
    for (const char c : value) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << c; break;
        }
    }
    return out.str();
}

std::string csv_escape(std::string_view value) {
    std::string result = "\"";
    for (const char c : value) {
        if (c == '"') result.push_back('"');
        result.push_back(c);
    }
    result.push_back('"');
    return result;
}

void append_sample(std::vector<Sample>& samples, std::string_view role,
                   std::string_view variant, std::string_view boundary,
                   std::uint64_t n, std::uint64_t d, std::uint64_t seed,
                   std::uint32_t block_size, std::uint32_t iteration,
                   std::uint64_t value, std::string_view timer) {
    samples.push_back({std::string(role), std::string(variant), std::string(boundary),
                       n, d, seed, block_size, iteration, value,
                       std::string(timer), "completed"});
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

s6::detail::S7StagedOutcome run_gpu(std::string_view variant,
                                    s6::DatasetView dataset,
                                    s6::QueryView query,
                                    std::uint32_t block_size) {
    if (variant == "v0") {
        return s6::detail::cuda_v0_search_float32_staged_for_benchmark(dataset, query);
    }
    if (variant == "v1") {
        return s6::detail::cuda_v1_search_float32_staged_for_benchmark(dataset, query);
    }
    if (variant == "v2") {
        return s6::detail::cuda_v2_search_float32_staged_for_benchmark(
            dataset, query, {0, nullptr, block_size});
    }
    if (variant == "v3") {
        return s6::detail::cuda_v3_search_float32_staged_for_benchmark(
            dataset, query, {0, nullptr, block_size});
    }
    throw std::runtime_error("unknown GPU variant");
}

s6::ProductionOutcome run_gpu_raw(std::string_view variant,
                                  s6::DatasetView dataset,
                                  s6::QueryView query,
                                  std::uint32_t block_size) {
    if (variant == "v0") return s6::cuda_v0_search_float32(dataset, query);
    if (variant == "v1") return s6::cuda_v1_search_float32(dataset, query);
    if (variant == "v2") {
        return s6::cuda_v2_search_float32(dataset, query, {0, nullptr, block_size});
    }
    if (variant == "v3") {
        return s6::cuda_v3_search_float32(dataset, query, {0, nullptr, block_size});
    }
    throw std::runtime_error("unknown GPU variant");
}

void append_gpu_stages(std::vector<Sample>& samples, std::string_view role,
                       std::string_view variant, std::uint64_t n, std::uint64_t d,
                       std::uint64_t seed, std::uint32_t block_size,
                       std::uint32_t iteration,
                       const s6::detail::S7CudaStageTiming& stages) {
    require(stages.device_timing_available,
            std::string("device stage timing unavailable: ") + stages.timing_detail);
    append_sample(samples, role, variant, "STAGE-HOST-INPUT-VALIDATION", n, d,
                  seed, block_size, iteration, stages.validation_ns, "steady_clock");
    append_sample(samples, role, variant, "STAGE-AOS-TO-SOA", n, d, seed,
                  block_size, iteration, stages.conversion_ns, "steady_clock");
    append_sample(samples, role, variant, "STAGE-DEVICE-ALLOCATION", n, d, seed,
                  block_size, iteration, stages.allocation_ns, "steady_clock");
    append_sample(samples, role, variant, "STAGE-H2D", n, d, seed, block_size,
                  iteration, stages.h2d_ns, "cuda_event");
    append_sample(samples, role, variant, "STAGE-WORKSPACE-RESET", n, d, seed,
                  block_size, iteration, stages.workspace_reset_ns, "cuda_event");
    append_sample(samples, role, variant, "BND-KERNEL", n, d, seed, block_size,
                  iteration, stages.kernel_ns, "cuda_event");
    append_sample(samples, role, variant, "STAGE-D2H", n, d, seed, block_size,
                  iteration, stages.d2h_ns, "cuda_event");
    append_sample(samples, role, variant, "STAGE-HOST-SYNCHRONIZE-WAIT", n, d,
                  seed, block_size, iteration, stages.synchronize_ns,
                  "steady_clock");
}

std::vector<std::uint64_t> values_for(const std::vector<Sample>& samples,
                                      std::string_view role,
                                      std::string_view variant,
                                      std::string_view boundary,
                                      std::uint64_t n, std::uint64_t d,
                                      std::uint32_t block_size) {
    std::vector<std::uint64_t> result;
    for (const auto& sample : samples) {
        if (sample.role == role && sample.variant == variant &&
            sample.boundary == boundary && sample.n == n && sample.d == d &&
            sample.block_size == block_size) {
            result.push_back(sample.elapsed_ns);
        }
    }
    return result;
}

std::uint32_t tune_variant(std::vector<Sample>& samples, std::string_view variant,
                           const Workload& workload, std::uint64_t n,
                           std::uint64_t d, unsigned warmups,
                           unsigned repetitions, std::string_view sample_role) {
    const auto cpu = s6::cpu_search_float32(workload.dataset_view(),
                                            workload.query_view());
    require(cpu.result.has_value(), "CPU tuning qualification failed");
    std::uint32_t selected = 0U;
    double selected_median = std::numeric_limits<double>::infinity();
    for (const auto block_size : kBlockCandidates) {
        const unsigned total = warmups + repetitions;
        for (unsigned iteration = 0; iteration < total; ++iteration) {
            const auto outcome = run_gpu(variant, workload.dataset_view(),
                                         workload.query_view(), block_size);
            require_matching_result(outcome.production, *cpu.result,
                                    "tuning candidate");
            if (iteration >= warmups) {
                const auto retained = iteration - warmups;
                append_gpu_stages(samples, sample_role, variant, n, d, kTuningSeed,
                                  block_size, retained, outcome.stages);
            }
        }
        const auto kernel = values_for(samples, sample_role, variant, "BND-KERNEL",
                                       n, d, block_size);
        const double candidate_median = percentile(kernel, 0.5);
        if (candidate_median < selected_median ||
            (candidate_median == selected_median && block_size < selected)) {
            selected = block_size;
            selected_median = candidate_median;
        }
    }
    require(selected != 0U, "tuning did not select a block size");
    return selected;
}

void run_final_case(std::vector<Sample>& samples, const Workload& workload,
                    std::uint64_t n, std::uint64_t d, unsigned warmups,
                    unsigned repetitions, std::uint32_t v2_block,
                    std::uint32_t v3_block, std::string_view sample_role) {
    const unsigned total = warmups + repetitions;
    std::optional<s6::SearchResult> expected;
    for (unsigned iteration = 0; iteration < total; ++iteration) {
        const auto start = Clock::now();
        const auto outcome = s6::cpu_search_float32(workload.dataset_view(),
                                                    workload.query_view());
        const auto stop = Clock::now();
        require(outcome.result.has_value(), "CPU final invocation failed");
        expected = outcome.result;
        if (iteration >= warmups) {
            append_sample(samples, sample_role, "cpu", "CPU-E2E-RAW", n, d,
                          kFinalSeed, 0U, iteration - warmups,
                          elapsed_ns(start, stop), "steady_clock");
        }
    }
    require(expected.has_value(), "CPU produced no expected result");

    const std::array<std::pair<std::string_view, std::uint32_t>, 4> variants = {{
        {"v0", 256U}, {"v1", 256U}, {"v2", v2_block}, {"v3", v3_block}}};
    for (const auto& [variant, block_size] : variants) {
        for (unsigned iteration = 0; iteration < total; ++iteration) {
            const auto start = Clock::now();
            const auto raw = run_gpu_raw(variant, workload.dataset_view(),
                                         workload.query_view(), block_size);
            const auto stop = Clock::now();
            require_matching_result(raw, *expected, "final GPU raw");
            const auto staged = run_gpu(variant, workload.dataset_view(),
                                        workload.query_view(), block_size);
            require_matching_result(staged.production, *expected,
                                    "final GPU staged");
            if (iteration >= warmups) {
                const auto retained = iteration - warmups;
                append_sample(samples, sample_role, variant, "BND-E2E-RAW", n, d,
                              kFinalSeed, block_size, retained,
                              elapsed_ns(start, stop), "steady_clock");
                append_gpu_stages(samples, sample_role, variant, n, d, kFinalSeed,
                                  block_size, retained, staged.stages);
            }
        }
    }
}

std::vector<Summary> summarize(const std::vector<Sample>& samples) {
    std::vector<Summary> result;
    for (const auto& sample : samples) {
        const auto existing = std::find_if(result.begin(), result.end(),
            [&](const Summary& value) {
                return value.role == sample.role && value.variant == sample.variant &&
                       value.boundary == sample.boundary && value.n == sample.n &&
                       value.d == sample.d && value.seed == sample.seed &&
                       value.block_size == sample.block_size;
            });
        if (existing == result.end()) {
            result.push_back({sample.role, sample.variant, sample.boundary,
                              sample.n, sample.d, sample.seed, sample.block_size});
        }
    }
    for (auto& summary : result) {
        const auto values = values_for(samples, summary.role, summary.variant,
                                       summary.boundary, summary.n, summary.d,
                                       summary.block_size);
        summary.count = values.size();
        summary.median_ns = percentile(values, 0.5);
        summary.p5_ns = percentile(values, 0.05);
        summary.p95_ns = percentile(values, 0.95);
        const std::uint64_t ci_seed = summary.seed ^ summary.n ^
            (summary.d << 32U) ^ static_cast<std::uint64_t>(summary.block_size);
        const auto ci = bootstrap_median_ci(values, ci_seed);
        summary.ci_low_ns = ci.first;
        summary.ci_high_ns = ci.second;
    }
    return result;
}

void write_raw(const fs::path& path, const std::vector<Sample>& samples) {
    std::ofstream out(path, std::ios::binary);
    require(static_cast<bool>(out), "cannot create raw_samples.csv");
    out << "schema_version,revision,tree_state,sample_role,workload,scenario,"
           "boundary,variant,N,D,seed,block_size,run_order,iteration,warmup,"
           "elapsed_ns,timer,status\n";
    std::size_t order = 0U;
    for (const auto& sample : samples) {
        out << "S7-BENCHMARK-RAW-v1," << csv_escape(S7_BENCHMARK_REVISION) << ','
            << csv_escape(S7_BENCHMARK_TREE_STATE) << ',' << sample.role
            << ",single-query,repeated-one-shot," << sample.boundary << ','
            << sample.variant << ',' << sample.n << ',' << sample.d << ','
            << sample.seed << ',' << sample.block_size << ',' << order++ << ','
            << sample.iteration << ",false," << sample.elapsed_ns << ','
            << sample.timer << ',' << sample.status << '\n';
    }
}

void write_summary(const fs::path& csv_path, const fs::path& json_path,
                   const std::vector<Summary>& summaries) {
    std::ofstream csv(csv_path, std::ios::binary);
    require(static_cast<bool>(csv), "cannot create summary.csv");
    csv << "schema_version,sample_role,variant,boundary,N,D,seed,block_size,"
           "sample_count,percentile_method,median_ns,p5_ns,p95_ns,ci_low_ns,"
           "ci_high_ns\n";
    csv << std::setprecision(17);
    for (const auto& value : summaries) {
        csv << "S7-BENCHMARK-SUMMARY-v1," << value.role << ',' << value.variant
            << ',' << value.boundary << ',' << value.n << ',' << value.d << ','
            << value.seed << ',' << value.block_size << ',' << value.count
            << ",linear_n_minus_1," << value.median_ns << ',' << value.p5_ns
            << ',' << value.p95_ns << ',' << value.ci_low_ns << ','
            << value.ci_high_ns << '\n';
    }
    std::ofstream json(json_path, std::ios::binary);
    require(static_cast<bool>(json), "cannot create summary.json");
    json << "{\n  \"schema_version\": \"S7-BENCHMARK-SUMMARY-v1\",\n"
            "  \"percentile_method\": \"linear_n_minus_1\",\n"
            "  \"uncertainty\": \"deterministic percentile bootstrap, 2000 resamples, 95% median interval\",\n"
            "  \"rows\": [\n";
    for (std::size_t i = 0; i < summaries.size(); ++i) {
        const auto& value = summaries[i];
        json << "    {\"sample_role\":\"" << json_escape(value.role)
             << "\",\"variant\":\"" << json_escape(value.variant)
             << "\",\"boundary\":\"" << json_escape(value.boundary)
             << "\",\"N\":" << value.n << ",\"D\":" << value.d
             << ",\"seed\":\"" << value.seed << "\",\"block_size\":"
             << value.block_size << ",\"sample_count\":" << value.count
             << ",\"median_ns\":" << value.median_ns << ",\"p5_ns\":"
             << value.p5_ns << ",\"p95_ns\":" << value.p95_ns
             << ",\"ci_low_ns\":" << value.ci_low_ns
             << ",\"ci_high_ns\":" << value.ci_high_ns << '}';
        json << (i + 1U == summaries.size() ? "\n" : ",\n");
    }
    json << "  ]\n}\n";
}

void write_manifest(const fs::path& output, RunMode mode, unsigned warmups,
                    unsigned repetitions, std::string_view correctness_reference,
                    const s6::CudaV3DeviceInfo& device) {
    std::ofstream out(output / "manifest.json", std::ios::binary);
    require(static_cast<bool>(out), "cannot create manifest.json");
    out << "{\n"
        << "  \"schema_version\": \"S7-BENCHMARK-MANIFEST-v1\",\n"
        << "  \"revision\": \"" << json_escape(S7_BENCHMARK_REVISION) << "\",\n"
        << "  \"working_tree_state\": \"" << json_escape(S7_BENCHMARK_TREE_STATE) << "\",\n"
        << "  \"sample_role\": \""
        << (mode == RunMode::dry_run ? "dry-run" :
            mode == RunMode::tuning ? "tuning" : "final") << "\",\n"
        << "  \"correctness_reference\": \"" << json_escape(correctness_reference) << "\",\n"
        << "  \"correctness_acceptance\": \""
        << (mode == RunMode::dry_run ? "not-applicable-dry-run" :
            mode == RunMode::tuning ? "required-before-final" : "externally-required")
        << "\",\n"
        << "  \"performance_acceptance\": \""
        << (std::string_view(S7_BENCHMARK_TREE_STATE) == "clean"
                ? "pending-complete-evidence"
                : "blocked-provisional-dirty-worktree") << "\",\n"
        << "  \"warmups\": " << warmups << ",\n"
        << "  \"measured_repetitions\": " << repetitions << ",\n"
        << "  \"tuning_seed\": \"" << kTuningSeed << "\",\n"
        << "  \"final_seed\": \"" << kFinalSeed << "\",\n"
        << "  \"run_order\": \"N ascending, D ascending, tune V2 then V3, final CPU/V0/V1/V2/V3\",\n"
        << "  \"fast_math\": \"disabled-by-target-contract\",\n"
        << "  \"prepared_query_public_scenario\": \"unsupported\",\n"
        << "  \"device_resident_public_scenario\": \"unsupported\",\n"
        << "  \"kernel_boundary\": \"benchmark-only CUDA events around the exact production kernel sequence\",\n"
        << "  \"host_allocation\": \"pageable std::vector\",\n"
        << "  \"gpu_name\": \"" << json_escape(device.name) << "\",\n"
        << "  \"gpu_uuid\": \"" << json_escape(device.uuid) << "\",\n"
        << "  \"driver_version\": " << device.driver_version << ",\n"
        << "  \"runtime_version\": " << device.runtime_version << "\n"
        << "}\n";
}

void write_environment(const fs::path& output,
                       const s6::CudaV3DeviceInfo& device) {
    const auto environment_value = [](const char* name) {
        char* buffer = nullptr;
        std::size_t length = 0U;
        const auto error = _dupenv_s(&buffer, &length, name);
        std::string value = error == 0 && buffer != nullptr ? buffer : "unknown";
        std::free(buffer);
        return value;
    };
    const auto processor = environment_value("PROCESSOR_IDENTIFIER");
    const auto computer = environment_value("COMPUTERNAME");
    std::ofstream out(output / "environment.json", std::ios::binary);
    require(static_cast<bool>(out), "cannot create environment.json");
    out << "{\n"
        << "  \"schema_version\": \"S7-ENVIRONMENT-v1\",\n"
        << "  \"operating_system\": \"Windows_NT\",\n"
        << "  \"os_build\": \"unknown\",\n"
        << "  \"computer_name\": \""
        << json_escape(computer) << "\",\n"
        << "  \"cpu\": \""
        << json_escape(processor) << "\",\n"
        << "  \"host_memory_bytes\": \"unknown\",\n"
        << "  \"gpu_name\": \"" << json_escape(device.name) << "\",\n"
        << "  \"gpu_uuid\": \"" << json_escape(device.uuid) << "\",\n"
        << "  \"gpu_memory_bytes\": " << device.total_global_memory << ",\n"
        << "  \"compute_capability\": \"" << device.compute_major << '.'
        << device.compute_minor << "\",\n"
        << "  \"driver_version\": " << device.driver_version << ",\n"
        << "  \"cuda_runtime_version\": " << device.runtime_version << ",\n"
        << "  \"cuda_toolkit_version\": " << CUDART_VERSION << ",\n"
        << "  \"host_compiler_msvc\": " << _MSC_VER << ",\n"
        << "  \"build_type\": \"Release\",\n"
        << "  \"fast_math\": \"disabled\",\n"
        << "  \"host_allocation\": \"pageable std::vector\",\n"
        << "  \"wddm_or_tcc\": \"unknown\",\n"
        << "  \"pcie_link\": \"unknown\",\n"
        << "  \"gpu_clock_temperature_power_trace\": \"not captured by CUDA runtime; external capture required for acceptance\",\n"
        << "  \"power_limit\": \"unknown\",\n"
        << "  \"display_load\": \"unknown\"\n"
        << "}\n";
}

void write_freeze(const fs::path& output,
                  const std::vector<FrozenConfiguration>& frozen) {
    std::ofstream out(output / "tuning-freeze.json", std::ios::binary);
    require(static_cast<bool>(out), "cannot create tuning-freeze.json");
    out << "{\n  \"schema_version\": \"S7-TUNING-FREEZE-v1\",\n"
           "  \"candidates\": [128,256,512],\n  \"selection_metric\": "
           "\"BND-KERNEL median\",\n  \"exact_tie_rule\": "
           "\"smaller block size\",\n  \"mapping\": [\n";
    for (std::size_t i = 0; i < frozen.size(); ++i) {
        const auto& value = frozen[i];
        out << "    {\"variant\":\"" << value.variant << "\",\"N\":"
            << value.n << ",\"D\":" << value.d << ",\"block_size\":"
            << value.block_size << ",\"tuning_median_ns\":"
            << value.tuning_median_ns << '}';
        out << (i + 1U == frozen.size() ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
    std::ofstream csv(output / "tuning-freeze.csv", std::ios::binary);
    require(static_cast<bool>(csv), "cannot create tuning-freeze.csv");
    csv << "schema_version,variant,N,D,block_size\n";
    for (const auto& value : frozen) {
        csv << "S7-TUNING-FREEZE-v1," << value.variant << ',' << value.n << ','
            << value.d << ',' << value.block_size << '\n';
    }
}

std::vector<FrozenConfiguration> load_freeze(const fs::path& path) {
    std::ifstream input(path);
    require(static_cast<bool>(input), "cannot open tuning freeze CSV");
    std::string line;
    require(static_cast<bool>(std::getline(input, line)) &&
                line == "schema_version,variant,N,D,block_size",
            "invalid tuning freeze CSV header");
    std::vector<FrozenConfiguration> result;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        std::stringstream stream(line);
        std::array<std::string, 5> fields;
        for (auto& field : fields) {
            require(static_cast<bool>(std::getline(stream, field, ',')),
                    "malformed tuning freeze CSV row");
        }
        require(fields[0] == "S7-TUNING-FREEZE-v1",
                "unknown tuning freeze schema");
        const auto block = static_cast<std::uint32_t>(std::stoul(fields[4]));
        require(block == 128U || block == 256U || block == 512U,
                "invalid frozen block size");
        result.push_back({fields[1], std::stoull(fields[2]), std::stoull(fields[3]),
                          block, 0.0});
    }
    return result;
}

std::uint32_t frozen_block(const std::vector<FrozenConfiguration>& frozen,
                           std::string_view variant, std::uint64_t n,
                           std::uint64_t d) {
    const auto found = std::find_if(frozen.begin(), frozen.end(),
        [&](const FrozenConfiguration& value) {
            return value.variant == variant && value.n == n && value.d == d;
        });
    require(found != frozen.end(), "frozen configuration is incomplete");
    return found->block_size;
}

void validate_correctness_gate(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot open correctness gate");
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    require(text.find("\"schema_version\": \"S7-CORRECTNESS-GATE-v1\"") !=
                std::string::npos,
            "correctness gate schema is invalid");
    require(text.find("\"status\": \"passed-") != std::string::npos,
            "correctness gate status is not passed");
    require(text.find(std::string("\"revision\": \"") +
                      S7_BENCHMARK_REVISION + "\"") != std::string::npos,
            "correctness gate revision does not match benchmark");
    require(text.find(std::string("\"working_tree_state\": \"") +
                      S7_BENCHMARK_TREE_STATE + "\"") != std::string::npos,
            "correctness gate tree state does not match benchmark");
}

void write_commands(const fs::path& output, int argc, char** argv) {
    std::ofstream out(output / "commands.txt", std::ios::binary);
    require(static_cast<bool>(out), "cannot create commands.txt");
    for (int i = 0; i < argc; ++i) {
        if (i != 0) out << ' ';
        out << '"' << argv[i] << '"';
    }
    out << '\n';
}

int self_test(const fs::path& output) {
    fs::create_directories(output);
    const std::vector<std::uint64_t> values = {1U, 2U, 3U, 4U, 5U};
    require(percentile(values, 0.5) == 3.0, "median self-test failed");
    require(std::abs(percentile(values, 0.05) - 1.2) < 1.0e-12,
            "p5 self-test failed");
    require(std::abs(percentile(values, 0.95) - 4.8) < 1.0e-12,
            "p95 self-test failed");
    const auto ci = bootstrap_median_ci(values, 42U);
    require(ci.first <= 3.0 && ci.second >= 3.0, "bootstrap self-test failed");
    std::ofstream out(output / "integrity-self-test.json", std::ios::binary);
    require(static_cast<bool>(out), "cannot create integrity self-test output");
    out << "{\"schema_version\":\"S7-INTEGRITY-v1\",\"status\":\"passed\"}\n";
    std::vector<FrozenConfiguration> frozen;
    for (const auto n : kCandidateCounts) {
        for (const auto d : kDimensions) {
            frozen.push_back({"v2", n, d, 256U, 1.0});
            frozen.push_back({"v3", n, d, 256U, 1.0});
        }
    }
    write_freeze(output, frozen);
    const auto loaded = load_freeze(output / "tuning-freeze.csv");
    require(loaded.size() == frozen.size() &&
                frozen_block(loaded, "v2", 1048576U, 128U) == 256U &&
                frozen_block(loaded, "v3", 1048576U, 128U) == 256U,
            "tuning freeze round-trip self-test failed");
    const auto gate_path = output / "correctness-gate-self-test.json";
    std::ofstream gate(gate_path, std::ios::binary);
    require(static_cast<bool>(gate), "cannot create correctness gate self-test");
    gate << "{\"schema_version\": \"S7-CORRECTNESS-GATE-v1\","
            "\"status\": \"passed-local-provisional\","
            "\"revision\": \"" << S7_BENCHMARK_REVISION << "\","
            "\"working_tree_state\": \"" << S7_BENCHMARK_TREE_STATE << "\"}\n";
    gate.close();
    validate_correctness_gate(gate_path);
    return 0;
}

int run_benchmark(RunMode mode, const fs::path& output,
                  std::string correctness_reference, fs::path freeze_path,
                  int argc, char** argv) {
    const bool dry_run = mode == RunMode::dry_run;
    const unsigned warmups = dry_run ? 1U : 10U;
    const unsigned repetitions = dry_run ? 3U : 100U;
    if (mode == RunMode::final) {
        require(!correctness_reference.empty(),
                "full run requires a correctness evidence reference");
        require(fs::exists(correctness_reference),
                "correctness evidence reference does not exist");
        validate_correctness_gate(correctness_reference);
        require(!freeze_path.empty() && fs::exists(freeze_path),
                "final run requires a frozen configuration CSV");
    }
    fs::create_directories(output / "plots");
    write_commands(output, argc, argv);
    const auto device = s6::inspect_cuda_v3_device(0);
    require(device.status == s6::EvidenceStatus::passed,
            std::string("CUDA device unavailable: ") + device.detail);
    write_manifest(output, mode, warmups, repetitions,
                   correctness_reference, device);
    write_environment(output, device);

    std::vector<Sample> samples;
    std::vector<FrozenConfiguration> frozen =
        mode == RunMode::final ? load_freeze(freeze_path) :
                                std::vector<FrozenConfiguration>{};
    const auto run_case = [&](std::uint64_t n, std::uint64_t d) {
        std::uint32_t v2_block = 0U;
        std::uint32_t v3_block = 0U;
        if (mode != RunMode::final) {
            auto tuning = make_workload(n, d, kTuningSeed);
            const std::string_view tuning_role = dry_run ? "dry-run-tuning" : "tuning";
            v2_block = tune_variant(samples, "v2", tuning, n, d,
                                    warmups, repetitions, tuning_role);
            v3_block = tune_variant(samples, "v3", tuning, n, d,
                                    warmups, repetitions, tuning_role);
            const auto v2_values = values_for(samples, tuning_role, "v2", "BND-KERNEL",
                                              n, d, v2_block);
            const auto v3_values = values_for(samples, tuning_role, "v3", "BND-KERNEL",
                                              n, d, v3_block);
            frozen.push_back({"v2", n, d, v2_block, percentile(v2_values, 0.5)});
            frozen.push_back({"v3", n, d, v3_block, percentile(v3_values, 0.5)});
        } else {
            v2_block = frozen_block(frozen, "v2", n, d);
            v3_block = frozen_block(frozen, "v3", n, d);
        }
        if (mode != RunMode::tuning) {
            auto final = make_workload(n, d, kFinalSeed);
            const std::string_view final_role = dry_run ? "dry-run-final" : "final";
            run_final_case(samples, final, n, d, warmups, repetitions,
                           v2_block, v3_block, final_role);
        }
    };

    if (dry_run) {
        run_case(1024U, 8U);
    } else {
        for (const auto n : kCandidateCounts) {
            for (const auto d : kDimensions) {
                run_case(n, d);
            }
        }
    }
    write_raw(output / "raw_samples.csv", samples);
    const auto summaries = summarize(samples);
    write_summary(output / "summary.csv", output / "summary.json", summaries);
    write_freeze(output, frozen);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc >= 3, "usage: s7_benchmark --self-test|--dry-run|--tune|--run OUTPUT [CORRECTNESS_EVIDENCE FREEZE_CSV]");
        const std::string mode = argv[1];
        const fs::path output = argv[2];
        if (mode == "--self-test") {
            return self_test(output);
        }
        if (mode == "--dry-run") {
            return run_benchmark(RunMode::dry_run, output,
                                 "not-applicable-dry-run", {}, argc, argv);
        }
        if (mode == "--tune") {
            require(argc == 3, "--tune accepts only an output directory");
            return run_benchmark(RunMode::tuning, output,
                                 "required-before-final", {}, argc, argv);
        }
        if (mode == "--run") {
            require(argc == 5,
                    "--run requires correctness evidence and freeze CSV paths");
            return run_benchmark(RunMode::final, output, argv[3], argv[4],
                                 argc, argv);
        }
        throw std::runtime_error("unknown benchmark mode");
    } catch (const std::exception& error) {
        std::cerr << "S7 benchmark: FAIL: " << error.what() << '\n';
        return 1;
    }
}
