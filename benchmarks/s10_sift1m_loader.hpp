#pragma once

#include "s6/cpu_baseline.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace s10 {

enum class Sift1mLoadStatus {
    loaded,
    invalid_path,
    missing_file,
    io_failure,
    invalid_size,
    invalid_dimension,
    truncated_record,
    trailing_data,
    nonfinite_value,
    allocation_failure,
};

struct Sift1mSchema {
    std::uint64_t base_records = 1000000U;
    std::uint64_t query_records = 10000U;
    std::uint32_t dimension = 128U;
    std::uint64_t query_ordinal = 0U;
};

struct LoadedSift1m {
    std::vector<float> base_aos;
    std::vector<float> query;
    std::vector<float> selected_queries_aos;
    std::vector<std::uint64_t> selected_query_ordinals;
    std::uint64_t base_records = 0U;
    std::uint32_t dimension = 0U;
    std::string base_sha256;
    std::string query_sha256;
    std::uint64_t base_file_bytes = 0U;
    std::uint64_t query_file_bytes = 0U;

    s6::DatasetView prefix(std::uint64_t candidate_count) const;
    s6::QueryView query_view() const;
    s6::QueryView selected_query_view(std::uint64_t query_ordinal) const;
};

struct Sift1mLoadResult {
    Sift1mLoadStatus status = Sift1mLoadStatus::io_failure;
    std::optional<LoadedSift1m> dataset;
    std::string detail;
};

Sift1mLoadResult load_sift1m(const std::filesystem::path& dataset_root);
Sift1mLoadResult load_sift1m_selected(
    const std::filesystem::path& dataset_root,
    const std::vector<std::uint64_t>& query_ordinals);
const char* sift1m_load_status_name(Sift1mLoadStatus status) noexcept;

namespace detail {

std::string sha256_file(const std::filesystem::path& path);
std::string sha256_bytes(std::string_view bytes);

Sift1mLoadResult load_sift1m_for_testing(
    const std::filesystem::path& dataset_root,
    const std::filesystem::path& repository_root,
    const Sift1mSchema& schema);
Sift1mLoadResult load_sift1m_selected_for_testing(
    const std::filesystem::path& dataset_root,
    const std::filesystem::path& repository_root,
    const Sift1mSchema& schema,
    const std::vector<std::uint64_t>& query_ordinals);

}  // namespace detail

}  // namespace s10
