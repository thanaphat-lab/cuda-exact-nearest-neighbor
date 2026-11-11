#include "s10_sift1m_loader.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef S10_TEST_REPOSITORY_ROOT
#define S10_TEST_REPOSITORY_ROOT "."
#endif

namespace {
namespace fs = std::filesystem;

constexpr s10::Sift1mSchema kSchema{3U, 2U, 3U, 0U};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void write_u32(std::ofstream& output, std::uint32_t value) {
    const std::array<unsigned char, 4> bytes = {
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8U),
        static_cast<unsigned char>(value >> 16U),
        static_cast<unsigned char>(value >> 24U)};
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void write_float(std::ofstream& output, float value) {
    std::uint32_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(value), "Float32 fixture required");
    std::memcpy(&bits, &value, sizeof(bits));
    write_u32(output, bits);
}

float float_from_bits(std::uint32_t bits) {
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void write_fvecs(const fs::path& path, const std::vector<std::vector<float>>& records,
                 std::uint32_t header_dimension = kSchema.dimension) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "cannot create fvecs fixture");
    for (const auto& record : records) {
        write_u32(output, header_dimension);
        for (const auto value : record) write_float(output, value);
    }
    require(static_cast<bool>(output), "cannot write fvecs fixture");
}

struct FixtureDirectory {
    fs::path path;
    explicit FixtureDirectory(const char* label) {
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch().count();
        path = fs::temp_directory_path() /
               (std::string("s10-loader-") + label + "-" + std::to_string(nonce));
        fs::create_directories(path);
    }
    ~FixtureDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

std::vector<std::vector<float>> base_values() {
    return {{1.0F, -0.0F, 3.0F}, {4.0F, 5.0F, 6.0F}, {7.0F, 8.0F, 9.0F}};
}

std::vector<std::vector<float>> query_values() {
    return {{-0.0F, 11.0F, 12.0F}, {13.0F, 14.0F, 15.0F}};
}

void write_valid_fixture(const fs::path& root) {
    write_fvecs(root / "sift_base.fvecs", base_values());
    write_fvecs(root / "sift_query.fvecs", query_values());
}

s10::Sift1mLoadResult load(const fs::path& root) {
    return s10::detail::load_sift1m_for_testing(
        root, fs::path(S10_TEST_REPOSITORY_ROOT), kSchema);
}

s10::Sift1mLoadResult load_selected(
    const fs::path& root, const std::vector<std::uint64_t>& ordinals) {
    return s10::detail::load_sift1m_selected_for_testing(
        root, fs::path(S10_TEST_REPOSITORY_ROOT), kSchema, ordinals);
}

void test_valid_order_signed_zero_and_hashes() {
    FixtureDirectory fixture("valid");
    write_valid_fixture(fixture.path);
    auto result = load(fixture.path);
    require(result.status == s10::Sift1mLoadStatus::loaded && result.dataset,
            "valid fixture did not load");
    const auto& dataset = *result.dataset;
    require(dataset.base_aos == std::vector<float>({1.0F, -0.0F, 3.0F, 4.0F,
                                                     5.0F, 6.0F, 7.0F, 8.0F, 9.0F}),
            "AoS source ordering changed");
    require(std::signbit(dataset.base_aos[1]) && std::signbit(dataset.query[0]),
            "signed zero was not preserved");
    const auto prefix = dataset.prefix(2U);
    require(prefix.candidate_count == 2U && prefix.dimension == 3U &&
                prefix.element_count == 6U && prefix.data == dataset.base_aos.data(),
            "prefix view is not the leading zero-copy source prefix");
    const auto query = dataset.query_view();
    require(query.dimension == 3U && query.element_count == 3U &&
                query.data == dataset.query.data(),
            "query view is invalid");
    require(dataset.base_sha256 ==
                "abaa30191eca92e80ca11111adecba90d4998fb1aeec24f4b8978bda91dc9c8f" &&
                dataset.query_sha256 ==
                "e7a9028465da52e4ce5ce4402f2df77a03999fbaca29bd87a56a0faf1b274e6a" &&
                s10::detail::sha256_file(fixture.path / "sift_base.fvecs") ==
                    dataset.base_sha256,
            "dataset SHA-256 capture is invalid");
}

void test_invalid_dimension() {
    FixtureDirectory base_fixture("dimension-base");
    write_fvecs(base_fixture.path / "sift_base.fvecs", base_values(), 2U);
    write_fvecs(base_fixture.path / "sift_query.fvecs", query_values());
    require(load(base_fixture.path).status == s10::Sift1mLoadStatus::invalid_dimension,
            "invalid fvecs dimension was accepted");

    FixtureDirectory query_fixture("dimension-query");
    write_fvecs(query_fixture.path / "sift_base.fvecs", base_values());
    write_fvecs(query_fixture.path / "sift_query.fvecs", query_values(), 4U);
    require(load(query_fixture.path).status == s10::Sift1mLoadStatus::invalid_dimension,
            "invalid query fvecs dimension was accepted");
}

void test_selected_queries_and_ordinal_validation() {
    FixtureDirectory fixture("selected-queries");
    write_valid_fixture(fixture.path);
    const auto result = load_selected(fixture.path, {1U, 0U});
    require(result.status == s10::Sift1mLoadStatus::loaded && result.dataset,
            "selected query fixture did not load");
    const auto& dataset = *result.dataset;
    require(dataset.selected_query_ordinals ==
                std::vector<std::uint64_t>({1U, 0U}) &&
                dataset.selected_queries_aos ==
                std::vector<float>({13.0F, 14.0F, 15.0F, -0.0F, 11.0F, 12.0F}),
            "selected queries did not preserve requested ordinal order");
    const auto query_one = dataset.selected_query_view(1U);
    const auto query_zero = dataset.selected_query_view(0U);
    require(query_one.data[0] == 13.0F && query_zero.data[1] == 11.0F &&
                dataset.query[0] == 13.0F,
            "selected query views or backward-compatible primary query are invalid");
    bool missing_rejected = false;
    try {
        (void)dataset.selected_query_view(2U);
    } catch (const std::out_of_range&) {
        missing_rejected = true;
    }
    require(missing_rejected, "unselected query ordinal was accepted");
    require(load_selected(fixture.path, {}).status ==
                s10::Sift1mLoadStatus::invalid_size &&
                load_selected(fixture.path, {0U, 0U}).status ==
                s10::Sift1mLoadStatus::invalid_size &&
                load_selected(fixture.path, {2U}).status ==
                s10::Sift1mLoadStatus::invalid_size,
            "empty, duplicate, or out-of-range selected query ordinals were accepted");
}

void test_missing_files() {
    FixtureDirectory empty("missing-both");
    require(load(empty.path).status == s10::Sift1mLoadStatus::missing_file,
            "missing base file was accepted");
    FixtureDirectory missing_query("missing-query");
    write_fvecs(missing_query.path / "sift_base.fvecs", base_values());
    require(load(missing_query.path).status == s10::Sift1mLoadStatus::missing_file,
            "missing query file was accepted");
}

void test_arbitrary_float32_bits() {
    FixtureDirectory fixture("float-bits");
    auto base = base_values();
    auto query = query_values();
    base[0][0] = float_from_bits(0x3f800001U);
    base[0][1] = float_from_bits(0x80000000U);
    query[0][0] = float_from_bits(0x00800001U);
    write_fvecs(fixture.path / "sift_base.fvecs", base);
    write_fvecs(fixture.path / "sift_query.fvecs", query);
    const auto result = load(fixture.path);
    require(result.status == s10::Sift1mLoadStatus::loaded && result.dataset,
            "arbitrary finite Float32 fixture did not load");
    require(float_bits(result.dataset->base_aos[0]) == 0x3f800001U &&
                float_bits(result.dataset->base_aos[1]) == 0x80000000U &&
                float_bits(result.dataset->query[0]) == 0x00800001U,
            "Float32 payload bits were not preserved");
}

void test_truncated_file() {
    FixtureDirectory fixture("truncated");
    write_valid_fixture(fixture.path);
    const auto base = fixture.path / "sift_base.fvecs";
    fs::resize_file(base, fs::file_size(base) - 1U);
    require(load(fixture.path).status == s10::Sift1mLoadStatus::truncated_record,
            "truncated fvecs file was accepted");
}

void test_trailing_data() {
    FixtureDirectory fixture("trailing");
    write_valid_fixture(fixture.path);
    std::ofstream output(fixture.path / "sift_query.fvecs",
                         std::ios::binary | std::ios::app);
    output.put('\0');
    output.close();
    require(load(fixture.path).status == s10::Sift1mLoadStatus::trailing_data,
            "trailing fvecs data was accepted");
}

void test_nonfinite_base_and_query() {
    FixtureDirectory base_fixture("nonfinite-base");
    auto base = base_values();
    base[1][2] = std::numeric_limits<float>::infinity();
    write_fvecs(base_fixture.path / "sift_base.fvecs", base);
    write_fvecs(base_fixture.path / "sift_query.fvecs", query_values());
    require(load(base_fixture.path).status == s10::Sift1mLoadStatus::nonfinite_value,
            "nonfinite base input was accepted");

    FixtureDirectory query_fixture("nonfinite-query");
    auto query = query_values();
    query[0][1] = std::numeric_limits<float>::quiet_NaN();
    write_fvecs(query_fixture.path / "sift_base.fvecs", base_values());
    write_fvecs(query_fixture.path / "sift_query.fvecs", query);
    require(load(query_fixture.path).status == s10::Sift1mLoadStatus::nonfinite_value,
            "nonfinite selected query was accepted");

    FixtureDirectory unselected_query_fixture("nonfinite-unselected-query");
    query = query_values();
    query[1][1] = std::numeric_limits<float>::infinity();
    write_fvecs(unselected_query_fixture.path / "sift_base.fvecs", base_values());
    write_fvecs(unselected_query_fixture.path / "sift_query.fvecs", query);
    require(load(unselected_query_fixture.path).status == s10::Sift1mLoadStatus::nonfinite_value,
            "nonfinite unselected canonical query record was accepted");
}

void test_repository_path_rejection() {
    const auto result = s10::detail::load_sift1m_for_testing(
        fs::path(S10_TEST_REPOSITORY_ROOT), fs::path(S10_TEST_REPOSITORY_ROOT), kSchema);
    require(result.status == s10::Sift1mLoadStatus::invalid_path,
            "repository-contained dataset root was accepted");
}

void test_canonical_alias_and_symlink_escape() {
    FixtureDirectory fixture("canonical-alias");
    write_valid_fixture(fixture.path);
    fs::create_directory(fixture.path / "alias-component");
    const auto alias = fixture.path / "alias-component" / "..";
    require(load(alias).status == s10::Sift1mLoadStatus::loaded,
            "canonical external dataset alias was rejected");

    FixtureDirectory escaped("symlink-escape");
    FixtureDirectory target("symlink-target");
    write_fvecs(target.path / "outside-base.fvecs", base_values());
    write_fvecs(escaped.path / "sift_query.fvecs", query_values());
    std::error_code error;
    fs::create_symlink(target.path / "outside-base.fvecs",
                       escaped.path / "sift_base.fvecs", error);
    if (!error) {
        require(load(escaped.path).status == s10::Sift1mLoadStatus::invalid_path,
                "dataset-file symlink escape was accepted");
    }
}

}  // namespace

int main() {
    try {
        test_valid_order_signed_zero_and_hashes();
        test_invalid_dimension();
        test_selected_queries_and_ordinal_validation();
        test_missing_files();
        test_arbitrary_float32_bits();
        test_truncated_file();
        test_trailing_data();
        test_nonfinite_base_and_query();
        test_repository_path_rejection();
        test_canonical_alias_and_symlink_escape();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "S10 loader fixtures: FAIL: " << error.what() << '\n';
        return 1;
    }
}
