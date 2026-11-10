#include "s6/cpu_baseline.hpp"
#include "s6/cpu_build_identity.hpp"
#include "s6/cuda_v2.hpp"
#include "s7_tuning_freeze.hpp"

#include <algorithm>
#include <array>
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
constexpr std::array<std::uint64_t, 4> kCandidateCounts = {
    1024U, 16384U, 262144U, 1048576U};
constexpr std::array<std::uint64_t, 3> kDimensions = {8U, 32U, 128U};
constexpr std::array<std::uint64_t, 3> kBatchSizes = {1U, 8U, 32U};

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

std::map<std::string, std::string> read_fields(
    const std::filesystem::path& path) {
    std::ifstream input(path);
    require(input.good(), "could not read V4 batch binary evidence");
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

void verify_binary_evidence(const std::filesystem::path& path) {
    const auto fields = read_fields(path);
    const auto value = [&](const char* name) -> const std::string& {
        const auto found = fields.find(name);
        require(found != fields.end(), "V4 batch binary evidence field missing");
        return found->second;
    };
    require(value("revision") == s6::build_identity::kRevision &&
                value("working_tree_fingerprint") ==
                    s6::build_identity::kWorkingTreeFingerprint &&
                value("source_manifest_sha256") ==
                    s6::build_identity::kSourceManifestSha256,
            "V4 batch binary/source identity mismatch");
    for (const char* name : {
             "compile_policy_status", "batch_distance_kernel_symbol_status",
             "batch_record_kernel_symbol_status",
             "batch_barrier_instruction_status",
             "batch_distance_kernel_instruction_status",
             "batch_distance_kernel_dependency_status",
             "batch_distance_kernel_default_rn_no_ftz_status",
             "binary_graph_structure_status"}) {
        require(value(name) == "passed", "V4 batch binary/SASS status failed");
    }
}

void run_case(std::uint64_t n, std::uint64_t d, std::uint32_t block_size,
              std::ofstream& evidence) {
    std::vector<float> query(static_cast<std::size_t>(d));
    for (std::uint64_t j = 0; j < d; ++j) {
        const auto centered = static_cast<int>((j + (kSeed & 15U)) % 17U) - 8;
        query[static_cast<std::size_t>(j)] =
            static_cast<float>(centered) / 16.0F;
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
        dataset[offset] = query[0] +
                          static_cast<float>((i % 31U) + 1U) / 64.0F;
    }
    const std::uint64_t expected_index = n / 2U;
    std::copy(query.begin(), query.end(),
              dataset.begin() + static_cast<std::ptrdiff_t>(expected_index * d));

    const s6::DatasetView dataset_view{dataset.data(), n, d, dataset.size()};
    const s6::QueryView query_view{query.data(), d, query.size()};
    const s6::CudaV2Options options{0, nullptr, block_size};
    const auto cpu = s6::cpu_search_float32(dataset_view, query_view);
    const auto reference = s6::compute_float64_reference(dataset_view, query_view);
    require(cpu.result && reference.argmin_index &&
                cpu.result->index == expected_index &&
                *reference.argmin_index == expected_index,
            "V4 batch CPU/Float64 prerequisite failed");
    auto preparation = s6::cuda_v2_prepare_dataset_float32(dataset_view, options);
    require(preparation.prepared_dataset.has_value(),
            "V4 batch prepared dataset construction failed");
    auto prepared = std::move(*preparation.prepared_dataset);
    auto single_workspace_result = s6::cuda_v2_create_workspace(prepared, options);
    auto batch_workspace_result =
        s6::cuda_v4_create_batch_workspace(prepared, 32U, options);
    require(single_workspace_result.workspace.has_value() &&
                batch_workspace_result.workspace.has_value(),
            "V4 batch workspace construction failed");
    auto single_workspace = std::move(*single_workspace_result.workspace);
    auto batch_workspace = std::move(*batch_workspace_result.workspace);
    const auto single = s6::cuda_v2_search_prepared_float32(
        prepared, query_view, single_workspace, options);
    require(single.result && single.result->index == cpu.result->index &&
                bits(single.result->squared_distance) ==
                    bits(cpu.result->squared_distance),
            "V4 batch sequential Prepared V2 prerequisite failed");

    std::vector<float> batch_queries(static_cast<std::size_t>(32U * d));
    for (std::uint64_t q = 0; q < 32U; ++q) {
        std::copy(query.begin(), query.end(),
                  batch_queries.begin() + static_cast<std::ptrdiff_t>(q * d));
    }
    for (const auto batch_size : kBatchSizes) {
        const s6::QueryBatchView batch_view{
            batch_queries.data(), batch_size, d,
            static_cast<std::size_t>(batch_size * d)};
        const auto cpu_batch =
            s6::cpu_search_batch_float32(dataset_view, batch_view);
        const auto batch = s6::cuda_v4_search_prepared_batch_float32(
            prepared, batch_view, batch_workspace, options);
        require(cpu_batch.invocation_status == s6::InvocationStatus::completed &&
                    cpu_batch.results.size() ==
                        static_cast<std::size_t>(batch_size) &&
                    batch.invocation_status == s6::InvocationStatus::completed &&
                    batch.results.size() == static_cast<std::size_t>(batch_size),
                "V4 batch matrix execution failed");
        for (std::size_t query_index = 0;
             query_index < batch.results.size(); ++query_index) {
            const auto& result = batch.results[query_index];
            require(result.index == cpu_batch.results[query_index].index &&
                        bits(result.squared_distance) ==
                            bits(cpu_batch.results[query_index].squared_distance) &&
                        result.index == cpu.result->index &&
                        bits(result.squared_distance) ==
                            bits(cpu.result->squared_distance) &&
                        result.index == single.result->index &&
                        bits(result.squared_distance) ==
                            bits(single.result->squared_distance),
                    "V4 batch matrix result mismatch");
        }
    }

    evidence << "case=N" << n << "-D" << d << " seed=" << kSeed
             << " block_size=" << block_size
             << " workload=batch-query-secondary"
             << " batch_1=passed batch_8=passed batch_32=passed"
             << " cpu_batch_reference=passed sequential_prepared_v2=passed"
             << " float64_reference_index=" << *reference.argmin_index
             << " result_index=" << cpu.result->index
             << " result_distance_bits=" << bits(cpu.result->squared_distance)
             << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 4,
                "output, V2 binary manifest, and tuning freeze are required");
        verify_binary_evidence(argv[2]);
        const auto freeze = s7_test::TuningFreeze::load(argv[3], "v2");
        const std::filesystem::path output_path = argv[1];
        std::filesystem::create_directories(output_path.parent_path());
        std::ofstream evidence(output_path, std::ios::trunc);
        require(evidence.good(), "could not open V4 batch matrix evidence");
        evidence << "schema=S14-CUDA-V4-BATCH-MATRIX-v1.0\n"
                 << "evidence_class="
                 << (std::strcmp(s6::build_identity::kWorkingTreeState, "clean") == 0
                         ? "clean-candidate"
                         : "local-uncommitted-not-acceptance")
                 << '\n'
                 << "revision=" << s6::build_identity::kRevision << '\n'
                 << "working_tree_state="
                 << s6::build_identity::kWorkingTreeState << '\n'
                 << "working_tree_fingerprint="
                 << s6::build_identity::kWorkingTreeFingerprint << '\n'
                 << "source_manifest_sha256="
                 << s6::build_identity::kSourceManifestSha256 << '\n'
                 << "workload_kind=batch-query-secondary\n"
                 << "batch_sizes=1,8,32\n"
                 << "block_configuration=frozen-s7-v2\n";
        for (const auto n : kCandidateCounts) {
            for (const auto d : kDimensions) {
                run_case(n, d, freeze.at(n, d), evidence);
            }
        }
        evidence << "matrix_cases=12 batch_configurations=36 failed=0 blocked=0\n"
                 << "overall=passed\n";
        std::cout << "S14 CUDA V4 batch matrix: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "S14 CUDA V4 batch matrix: FAIL: " << error.what() << '\n';
        return 1;
    }
}
