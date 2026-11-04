#include "s6/benchmark_internal.hpp"
#include "s7_tuning_freeze.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef S8_BENCHMARK_REVISION
#define S8_BENCHMARK_REVISION "unknown"
#endif
#ifndef S8_BENCHMARK_TREE_STATE
#define S8_BENCHMARK_TREE_STATE "unknown"
#endif

namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using Impl = s6::detail::S8TransposeImplementation;

constexpr std::array<std::uint64_t, 4> kNs = {1024U, 16384U, 262144U, 1048576U};
constexpr std::array<std::uint64_t, 3> kDs = {8U, 32U, 128U};
constexpr std::array<Impl, 4> kImpls = {Impl::original, Impl::tile_32x32,
                                        Impl::tile_64x32, Impl::tile_64x64};

struct Workload {
    std::vector<float> dataset;
    std::vector<float> query;
    s6::DatasetView data() const {
        return {dataset.data(), static_cast<std::uint64_t>(dataset.size() / query.size()),
                static_cast<std::uint64_t>(query.size()), dataset.size()};
    }
    s6::QueryView request() const {
        return {query.data(), static_cast<std::uint64_t>(query.size()), query.size()};
    }
};

struct Sample {
    std::string role, variant, implementation, boundary, timer;
    std::uint64_t n, d, seed, elapsed_ns;
    std::uint32_t block_size, run_order, iteration;
};

struct Options {
    std::string id, config_hash, expected_revision, captured_at, timezone;
    std::string cuda_architecture, build_flags;
    fs::path config;
    fs::path correctness_reference;
    std::uint64_t tuning_seed = 0U, final_seed = 0U;
};

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

Options parse_options(int argc, char** argv, int first) {
    Options result;
    const auto take = [&](int& i, std::string_view name) {
        require(i + 1 < argc, std::string(name) + " requires a value");
        return std::string(argv[++i]);
    };
    for (int i = first; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--experiment-id") result.id = take(i, arg);
        else if (arg == "--experiment-config") result.config = take(i, arg);
        else if (arg == "--config-sha256") result.config_hash = take(i, arg);
        else if (arg == "--expected-revision") result.expected_revision = take(i, arg);
        else if (arg == "--tuning-seed") result.tuning_seed = std::stoull(take(i, arg));
        else if (arg == "--final-seed") result.final_seed = std::stoull(take(i, arg));
        else if (arg == "--captured-at") result.captured_at = take(i, arg);
        else if (arg == "--timezone") result.timezone = take(i, arg);
        else if (arg == "--cuda-architecture") result.cuda_architecture = take(i, arg);
        else if (arg == "--build-flags") result.build_flags = take(i, arg);
        else if (arg == "--correctness-reference") result.correctness_reference = take(i, arg);
        else throw std::runtime_error("unknown option: " + arg);
    }
    require(!result.id.empty() && fs::exists(result.config), "missing experiment identity");
    require(result.config_hash.size() == 64U, "invalid config hash");
    require(result.expected_revision == S8_BENCHMARK_REVISION, "revision mismatch");
    require(result.tuning_seed != result.final_seed, "tuning/final seeds must differ");
    require(!result.captured_at.empty() && !result.timezone.empty() &&
            !result.cuda_architecture.empty() && !result.build_flags.empty(),
            "missing environment/build identity");
    return result;
}

Workload make_workload(std::uint64_t n, std::uint64_t d, std::uint64_t seed) {
    Workload result;
    result.query.resize(static_cast<std::size_t>(d));
    for (std::uint64_t j = 0; j < d; ++j) {
        result.query[static_cast<std::size_t>(j)] =
            static_cast<float>(static_cast<int>((j + (seed & 15U)) % 17U) - 8) / 16.0F;
    }
    require(n <= std::numeric_limits<std::size_t>::max() / d, "workload overflow");
    result.dataset.resize(static_cast<std::size_t>(n * d));
    for (std::uint64_t i = 0; i < n; ++i) {
        for (std::uint64_t j = 0; j < d; ++j) {
            const auto mixed = i * 17U + j * 13U + (seed & 255U);
            result.dataset[static_cast<std::size_t>(i * d + j)] =
                static_cast<float>(static_cast<int>(mixed % 257U) - 128) / 64.0F;
        }
        result.dataset[static_cast<std::size_t>(i * d)] =
            result.query[0] + static_cast<float>((i % 31U) + 1U) / 64.0F;
    }
    return result;
}

s6::ProductionOutcome raw(std::string_view variant, const Workload& work, Impl impl,
                          std::uint32_t block) {
    if (variant == "v1") return s6::detail::cuda_v1_search_float32_for_s8_benchmark(
        work.data(), work.request(), impl);
    if (variant == "v2") return s6::detail::cuda_v2_search_float32_for_s8_benchmark(
        work.data(), work.request(), impl, {0, nullptr, block});
    if (variant == "v3") return s6::detail::cuda_v3_search_float32_for_s8_benchmark(
        work.data(), work.request(), impl, {0, nullptr, block});
    throw std::runtime_error("unknown variant");
}

s6::detail::S7StagedOutcome staged(std::string_view variant, const Workload& work,
                                   Impl impl, std::uint32_t block) {
    if (variant == "v1") return s6::detail::cuda_v1_search_float32_staged_for_s8_benchmark(
        work.data(), work.request(), impl);
    if (variant == "v2") return s6::detail::cuda_v2_search_float32_staged_for_s8_benchmark(
        work.data(), work.request(), impl, {0, nullptr, block});
    if (variant == "v3") return s6::detail::cuda_v3_search_float32_staged_for_s8_benchmark(
        work.data(), work.request(), impl, {0, nullptr, block});
    throw std::runtime_error("unknown variant");
}

void check(const s6::ProductionOutcome& outcome, const s6::SearchResult& expected) {
    require(outcome.invocation_status == s6::InvocationStatus::completed &&
            outcome.result && outcome.result->index == expected.index &&
            outcome.result->squared_distance == expected.squared_distance,
            "GPU/CPU mismatch");
}

void add(std::vector<Sample>& out, std::string_view role, std::string_view variant,
         Impl impl, std::string_view boundary, std::uint64_t n, std::uint64_t d,
         std::uint64_t seed, std::uint32_t block, std::uint32_t order,
         std::uint32_t iteration, std::uint64_t value, std::string_view timer) {
    out.push_back({std::string(role), std::string(variant),
        s6::detail::s8_transpose_implementation_name(impl), std::string(boundary),
        std::string(timer), n, d, seed, value, block, order, iteration});
}

void add_stages(std::vector<Sample>& out, std::string_view role,
                std::string_view variant, Impl impl, std::uint64_t n,
                std::uint64_t d, std::uint64_t seed, std::uint32_t block,
                std::uint32_t order, std::uint32_t iteration,
                const s6::detail::S7CudaStageTiming& t) {
    require(t.device_timing_available, "stage timing unavailable");
    const auto put = [&](std::string_view b, std::uint64_t value, std::string_view timer) {
        add(out, role, variant, impl, b, n, d, seed, block, order, iteration, value, timer);
    };
    put("STAGE-HOST-INPUT-VALIDATION", t.validation_ns, "steady_clock");
    put("STAGE-AOS-TO-SOA", t.conversion_ns, "steady_clock");
    put("BND-HOST-TRANSPOSE", t.transpose_ns, "steady_clock");
    put("STAGE-DEVICE-ALLOCATION", t.allocation_ns, "steady_clock");
    put("STAGE-H2D", t.h2d_ns, "cuda_event");
    put("STAGE-WORKSPACE-RESET", t.workspace_reset_ns, "cuda_event");
    put("BND-KERNEL", t.kernel_ns, "cuda_event");
    put("STAGE-D2H", t.d2h_ns, "cuda_event");
    put("STAGE-HOST-SYNCHRONIZE-WAIT", t.synchronize_ns, "steady_clock");
}

std::array<Impl, 4> order_for(unsigned iteration, unsigned variant_offset) {
    std::array<Impl, 4> result{};
    const auto offset = (iteration + variant_offset) % kImpls.size();
    for (std::size_t i = 0; i < result.size(); ++i) result[i] = kImpls[(i + offset) % kImpls.size()];
    return result;
}

void write_outputs(const fs::path& output, const Options& options,
                   std::string_view role, const fs::path& s7_freeze,
                   const std::vector<Sample>& samples) {
    fs::create_directories(output);
    fs::copy_file(options.config, output / "experiment-config.json",
                  fs::copy_options::overwrite_existing);
    const auto device = s6::inspect_cuda_v3_device(0);
    require(device.status == s6::EvidenceStatus::passed,
            "CUDA device identity unavailable");
    std::ofstream manifest(output / "manifest.json", std::ios::binary);
    manifest << "{\n  \"schema_version\": \"S8-BENCHMARK-MANIFEST-v1\",\n"
             << "  \"experiment_id\": \"" << options.id << "\",\n"
             << "  \"revision\": \"" << S8_BENCHMARK_REVISION << "\",\n"
             << "  \"working_tree_state\": \"" << S8_BENCHMARK_TREE_STATE << "\",\n"
             << "  \"sample_role\": \"" << role << "\",\n"
             << "  \"config_sha256\": \"" << options.config_hash << "\",\n"
             << "  \"s7_block_freeze\": \"" << s7_freeze.generic_string() << "\",\n"
             << "  \"correctness_reference\": \""
             << options.correctness_reference.generic_string() << "\",\n"
             << "  \"warmups\": 10,\n  \"repetitions\": 100,\n"
             << "  \"ordering\": \"deterministic rotated A/B order\",\n"
             << "  \"tuning_seed\": \"" << options.tuning_seed << "\",\n"
             << "  \"final_seed\": \"" << options.final_seed << "\",\n"
             << "  \"cuda_architecture\": \"" << options.cuda_architecture << "\",\n"
             << "  \"build_flags\": \"" << options.build_flags << "\",\n"
             << "  \"fast_math\": \"disabled\",\n"
             << "  \"captured_at\": \"" << options.captured_at << "\",\n"
             << "  \"timezone\": \"" << options.timezone << "\"\n}\n";
    std::ofstream environment(output / "environment.json", std::ios::binary);
    environment << "{\n  \"schema_version\": \"S8-ENVIRONMENT-v1\",\n"
                << "  \"gpu_name\": \"" << device.name << "\",\n"
                << "  \"gpu_uuid\": \"" << device.uuid << "\",\n"
                << "  \"gpu_memory_bytes\": " << device.total_global_memory << ",\n"
                << "  \"compute_capability\": \"" << device.compute_major << '.'
                << device.compute_minor << "\",\n"
                << "  \"driver_version\": " << device.driver_version << ",\n"
                << "  \"cuda_runtime_version\": " << device.runtime_version << ",\n"
                << "  \"cuda_architecture\": \"" << options.cuda_architecture << "\",\n"
                << "  \"build_flags\": \"" << options.build_flags << "\",\n"
                << "  \"fast_math\": \"disabled\"\n}\n";
    std::ofstream raw_out(output / "raw_samples.csv", std::ios::binary);
    raw_out << "schema_version,revision,tree_state,sample_role,boundary,variant,"
               "transpose_impl,N,D,seed,block_size,run_order,iteration,warmup,"
               "elapsed_ns,timer,status\n";
    for (const auto& s : samples) {
        raw_out << "S8-RAW-SAMPLE-v1," << S8_BENCHMARK_REVISION << ','
                << S8_BENCHMARK_TREE_STATE << ',' << s.role << ',' << s.boundary
                << ',' << s.variant << ',' << s.implementation << ',' << s.n << ','
                << s.d << ',' << s.seed << ',' << s.block_size << ',' << s.run_order
                << ',' << s.iteration << ",false," << s.elapsed_ns << ',' << s.timer
                << ",completed\n";
    }
}

int self_test(const fs::path& output) {
    fs::create_directories(output);
    require(std::string(s6::detail::s8_transpose_implementation_name(Impl::tile_64x32)) ==
            "64x32", "selector self-test failed");
    require(order_for(1U, 1U)[0] == Impl::tile_64x32, "ordering self-test failed");
    std::ofstream out(output / "self-test.json", std::ios::binary);
    out << "{\"schema_version\":\"S8-BENCHMARK-SELF-TEST-v1\","
           "\"status\":\"passed\",\"revision\":\"" << S8_BENCHMARK_REVISION
        << "\"}\n";
    return 0;
}

int run(bool tuning, const fs::path& output, const fs::path& freeze_path,
        const Options& options) {
    if (!tuning) {
        require(!options.correctness_reference.empty() &&
                    fs::exists(options.correctness_reference),
                "final run requires matching correctness evidence");
    }
    const auto v2_freeze = s7_test::TuningFreeze::load(freeze_path.string(), "v2");
    const auto v3_freeze = s7_test::TuningFreeze::load(freeze_path.string(), "v3");
    const auto role = tuning ? "tuning" : "final";
    const auto seed = tuning ? options.tuning_seed : options.final_seed;
    std::vector<Sample> samples;
    for (const auto n : kNs) for (const auto d : kDs) {
        const auto work = make_workload(n, d, seed);
        const auto cpu = s6::cpu_search_float32(work.data(), work.request());
        require(cpu.result.has_value(), "CPU reference failed");
        if (!tuning) {
            for (unsigned iteration = 0; iteration < 110U; ++iteration) {
                const auto start = Clock::now();
                const auto outcome = s6::cpu_search_float32(work.data(), work.request());
                const auto stop = Clock::now();
                require(outcome.result.has_value(), "CPU final failed");
                if (iteration >= 10U) add(samples, role, "cpu", Impl::original,
                    "CPU-E2E-RAW", n, d, seed, 0U, 0U, iteration - 10U,
                    static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        stop - start).count()), "steady_clock");
            }
        }
        const std::array<std::string_view, 3> variants = {"v1", "v2", "v3"};
        for (std::size_t vi = 0; vi < variants.size(); ++vi) {
            const auto variant = variants[vi];
            const auto block = variant == "v1" ? 256U :
                (variant == "v2" ? v2_freeze.at(n, d) : v3_freeze.at(n, d));
            for (unsigned iteration = 0; iteration < 110U; ++iteration) {
                const auto order = order_for(iteration, static_cast<unsigned>(vi));
                for (std::size_t oi = 0; oi < order.size(); ++oi) {
                    const auto impl = order[oi];
                    if (tuning && impl == Impl::original) continue;
                    const auto start = Clock::now();
                    const auto raw_outcome = raw(variant, work, impl, block);
                    const auto stop = Clock::now();
                    check(raw_outcome, *cpu.result);
                    const auto staged_outcome = staged(variant, work, impl, block);
                    check(staged_outcome.production, *cpu.result);
                    if (iteration >= 10U) {
                        const auto retained = iteration - 10U;
                        add(samples, role, variant, impl, "BND-E2E-RAW", n, d, seed,
                            block, static_cast<std::uint32_t>(oi), retained,
                            static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    stop - start).count()), "steady_clock");
                        add_stages(samples, role, variant, impl, n, d, seed, block,
                                   static_cast<std::uint32_t>(oi), retained,
                                   staged_outcome.stages);
                    }
                }
            }
        }
    }
    write_outputs(output, options, role, freeze_path, samples);
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc >= 3, "usage: s8_benchmark --self-test OUTPUT or --tune|--run OUTPUT S7_FREEZE OPTIONS");
        const std::string mode = argv[1];
        const fs::path output = argv[2];
        if (mode == "--self-test") return self_test(output);
        require((mode == "--tune" || mode == "--run") && argc >= 5,
                "benchmark mode requires S7 freeze and options");
        const fs::path freeze = argv[3];
        require(fs::exists(freeze), "S7 block freeze missing");
        fs::create_directories(output);
        std::ofstream command(output / "commands.txt", std::ios::binary);
        for (int i = 0; i < argc; ++i) {
            if (i != 0) command << ' ';
            command << argv[i];
        }
        command << '\n';
        return run(mode == "--tune", output, freeze, parse_options(argc, argv, 4));
    } catch (const std::exception& error) {
        std::cerr << "S8 benchmark: FAIL: " << error.what() << '\n';
        return 1;
    }
}
