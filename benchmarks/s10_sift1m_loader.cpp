#include "s10_sift1m_loader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>

#ifndef S10_REPOSITORY_ROOT
#define S10_REPOSITORY_ROOT "."
#endif

namespace s10 {
namespace {
namespace fs = std::filesystem;

class LoadError : public std::runtime_error {
  public:
    LoadError(Sift1mLoadStatus status, const std::string& detail)
        : std::runtime_error(detail), status_(status) {}
    Sift1mLoadStatus status() const noexcept { return status_; }

  private:
    Sift1mLoadStatus status_;
};

std::uint32_t rotate_right(std::uint32_t value, unsigned amount) {
    return (value >> amount) | (value << (32U - amount));
}

class Sha256 {
  public:
    void update(const unsigned char* bytes, std::size_t size) {
        total_bytes_ += static_cast<std::uint64_t>(size);
        while (size != 0U) {
            const auto copied = std::min(size, block_.size() - block_size_);
            std::memcpy(block_.data() + block_size_, bytes, copied);
            block_size_ += copied;
            bytes += copied;
            size -= copied;
            if (block_size_ == block_.size()) {
                transform(block_.data());
                block_size_ = 0U;
            }
        }
    }

    std::string finish() {
        const std::uint64_t bit_count = total_bytes_ * 8U;
        block_[block_size_++] = 0x80U;
        if (block_size_ > 56U) {
            std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_),
                      block_.end(), static_cast<unsigned char>(0U));
            transform(block_.data());
            block_size_ = 0U;
        }
        std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_),
                  block_.begin() + 56, static_cast<unsigned char>(0U));
        for (unsigned i = 0U; i < 8U; ++i) {
            block_[63U - i] = static_cast<unsigned char>(bit_count >> (i * 8U));
        }
        transform(block_.data());
        std::ostringstream result;
        result << std::hex << std::setfill('0');
        for (const auto word : state_) result << std::setw(8) << word;
        return result.str();
    }

  private:
    void transform(const unsigned char* block) {
        static constexpr std::array<std::uint32_t, 64> constants = {
            0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
            0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
            0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
            0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
            0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
            0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
            0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
            0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};
        std::array<std::uint32_t, 64> schedule{};
        for (std::size_t i = 0; i < 16U; ++i) {
            const auto offset = i * 4U;
            schedule[i] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                          (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                          (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                          static_cast<std::uint32_t>(block[offset + 3U]);
        }
        for (std::size_t i = 16U; i < schedule.size(); ++i) {
            const auto a = schedule[i - 15U];
            const auto b = schedule[i - 2U];
            const auto s0 = rotate_right(a, 7U) ^ rotate_right(a, 18U) ^ (a >> 3U);
            const auto s1 = rotate_right(b, 17U) ^ rotate_right(b, 19U) ^ (b >> 10U);
            schedule[i] = schedule[i - 16U] + s0 + schedule[i - 7U] + s1;
        }
        auto a = state_[0]; auto b = state_[1]; auto c = state_[2]; auto d = state_[3];
        auto e = state_[4]; auto f = state_[5]; auto g = state_[6]; auto h = state_[7];
        for (std::size_t i = 0; i < schedule.size(); ++i) {
            const auto sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
            const auto choice = (e & f) ^ (~e & g);
            const auto temp1 = h + sum1 + choice + constants[i] + schedule[i];
            const auto sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = sum0 + majority;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_ = {
        0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
        0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    std::array<unsigned char, 64> block_{};
    std::size_t block_size_ = 0U;
    std::uint64_t total_bytes_ = 0U;
};

bool is_within(const fs::path& child, const fs::path& parent) {
    std::error_code error;
    const auto relative = fs::relative(child, parent, error);
    if (error || relative.empty()) return !error && child == parent;
    const auto first = *relative.begin();
    return first != ".." && !relative.is_absolute();
}

fs::path canonical_directory(const fs::path& value, Sift1mLoadStatus missing_status) {
    std::error_code error;
    const auto canonical = fs::weakly_canonical(value, error);
    if (error || !fs::exists(canonical) || !fs::is_directory(canonical)) {
        throw LoadError(missing_status, "dataset root is missing or is not a directory");
    }
    return canonical;
}

std::uint64_t expected_file_bytes(std::uint64_t records, std::uint32_t dimension) {
    const std::uint64_t record_bytes = 4U + static_cast<std::uint64_t>(dimension) * 4U;
    if (dimension == 0U || records > std::numeric_limits<std::uint64_t>::max() / record_bytes) {
        throw LoadError(Sift1mLoadStatus::invalid_size, "fvecs schema size overflow");
    }
    return records * record_bytes;
}

std::uint32_t little_u32(const std::array<unsigned char, 4>& bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

float little_float(const unsigned char* bytes) {
    const std::array<unsigned char, 4> packed = {bytes[0], bytes[1], bytes[2], bytes[3]};
    const auto bits = little_u32(packed);
    float value = 0.0F;
    static_assert(sizeof(value) == sizeof(bits), "Float32 is required");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void read_exact(std::ifstream& input, unsigned char* destination, std::size_t size,
                Sha256& hash, const char* label) {
    input.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(size));
    if (input.gcount() != static_cast<std::streamsize>(size)) {
        throw LoadError(Sift1mLoadStatus::truncated_record,
                        std::string(label) + " contains a truncated record");
    }
    hash.update(destination, size);
}

struct ParsedFile {
    std::vector<float> values;
    std::vector<float> selected;
    std::string sha256;
    std::uint64_t bytes = 0U;
};

ParsedFile parse_file(const fs::path& path, std::uint64_t records,
                      std::uint32_t dimension, bool retain_all,
                      std::uint64_t selected_ordinal, bool check_all_finite) {
    std::error_code error;
    if (!fs::exists(path) || !fs::is_regular_file(path)) {
        throw LoadError(Sift1mLoadStatus::missing_file,
                        path.filename().string() + " is missing or is not a regular file");
    }
    const auto expected = expected_file_bytes(records, dimension);
    const auto observed = fs::file_size(path, error);
    if (error) throw LoadError(Sift1mLoadStatus::io_failure, "cannot read fvecs file size");
    if (observed < expected) {
        throw LoadError(Sift1mLoadStatus::truncated_record,
                        path.filename().string() + " is truncated");
    }
    if (observed > expected) {
        throw LoadError(Sift1mLoadStatus::trailing_data,
                        path.filename().string() + " contains trailing data");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) throw LoadError(Sift1mLoadStatus::io_failure, "cannot open fvecs file");
    ParsedFile result;
    result.bytes = observed;
    if (retain_all) {
        if (records > std::numeric_limits<std::size_t>::max() / dimension) {
            throw LoadError(Sift1mLoadStatus::invalid_size, "fvecs element count overflow");
        }
        result.values.resize(static_cast<std::size_t>(records * dimension));
    } else {
        result.selected.resize(dimension);
    }
    std::vector<unsigned char> payload(static_cast<std::size_t>(dimension) * 4U);
    std::array<unsigned char, 4> header{};
    Sha256 hash;
    for (std::uint64_t record = 0U; record < records; ++record) {
        read_exact(input, header.data(), header.size(), hash, "fvecs file");
        if (little_u32(header) != dimension) {
            throw LoadError(Sift1mLoadStatus::invalid_dimension,
                            path.filename().string() + " contains an invalid dimension header");
        }
        read_exact(input, payload.data(), payload.size(), hash, "fvecs file");
        const bool selected = retain_all || record == selected_ordinal;
        if (!selected && !check_all_finite) continue;
        for (std::uint32_t column = 0U; column < dimension; ++column) {
            const auto value = little_float(payload.data() + column * 4U);
            if (!std::isfinite(value)) {
                throw LoadError(Sift1mLoadStatus::nonfinite_value,
                                path.filename().string() + " contains a nonfinite value");
            }
            if (retain_all) {
                result.values[static_cast<std::size_t>(record * dimension + column)] = value;
            } else if (record == selected_ordinal) {
                result.selected[column] = value;
            }
        }
    }
    if (input.peek() != std::ifstream::traits_type::eof()) {
        throw LoadError(Sift1mLoadStatus::trailing_data, "fvecs file has trailing data");
    }
    result.sha256 = hash.finish();
    return result;
}

Sift1mLoadResult load_impl(const fs::path& dataset_root,
                           const fs::path& repository_root,
                           const Sift1mSchema& schema) {
    try {
        if (schema.base_records == 0U || schema.query_records == 0U ||
            schema.dimension == 0U || schema.query_ordinal >= schema.query_records) {
            throw LoadError(Sift1mLoadStatus::invalid_size, "invalid SIFT1M schema");
        }
        const auto root = canonical_directory(dataset_root, Sift1mLoadStatus::invalid_path);
        const auto repository = fs::weakly_canonical(repository_root);
        if (is_within(root, repository)) {
            throw LoadError(Sift1mLoadStatus::invalid_path,
                            "dataset root must be outside the repository");
        }
        const auto unresolved_base = root / "sift_base.fvecs";
        const auto unresolved_query = root / "sift_query.fvecs";
        if (!fs::exists(unresolved_base) || !fs::is_regular_file(unresolved_base) ||
            !fs::exists(unresolved_query) || !fs::is_regular_file(unresolved_query)) {
            throw LoadError(Sift1mLoadStatus::missing_file,
                            "required SIFT1M base/query file is missing or not regular");
        }
        const auto base_path = fs::weakly_canonical(unresolved_base);
        const auto query_path = fs::weakly_canonical(unresolved_query);
        if (!is_within(base_path, root) || !is_within(query_path, root)) {
            throw LoadError(Sift1mLoadStatus::invalid_path,
                            "dataset files must remain inside the external dataset root");
        }
        auto base = parse_file(base_path, schema.base_records, schema.dimension,
                               true, 0U, true);
        auto query = parse_file(query_path, schema.query_records, schema.dimension,
                                false, schema.query_ordinal, true);
        LoadedSift1m loaded;
        loaded.base_aos = std::move(base.values);
        loaded.query = std::move(query.selected);
        loaded.base_records = schema.base_records;
        loaded.dimension = schema.dimension;
        loaded.base_sha256 = std::move(base.sha256);
        loaded.query_sha256 = std::move(query.sha256);
        loaded.base_file_bytes = base.bytes;
        loaded.query_file_bytes = query.bytes;
        return {Sift1mLoadStatus::loaded, std::move(loaded), "loaded"};
    } catch (const LoadError& error) {
        return {error.status(), std::nullopt, error.what()};
    } catch (const std::bad_alloc&) {
        return {Sift1mLoadStatus::allocation_failure, std::nullopt,
                "allocation failed while loading SIFT1M"};
    } catch (const std::exception& error) {
        return {Sift1mLoadStatus::io_failure, std::nullopt, error.what()};
    }
}

}  // namespace

s6::DatasetView LoadedSift1m::prefix(std::uint64_t candidate_count) const {
    if (candidate_count == 0U || candidate_count > base_records || dimension == 0U ||
        candidate_count > std::numeric_limits<std::size_t>::max() / dimension) {
        throw std::out_of_range("invalid SIFT1M prefix");
    }
    const auto elements = static_cast<std::size_t>(candidate_count * dimension);
    return {base_aos.data(), candidate_count, dimension, elements};
}

s6::QueryView LoadedSift1m::query_view() const {
    return {query.data(), dimension, query.size()};
}

Sift1mLoadResult load_sift1m(const fs::path& dataset_root) {
    return load_impl(dataset_root, fs::path(S10_REPOSITORY_ROOT), Sift1mSchema{});
}

const char* sift1m_load_status_name(Sift1mLoadStatus status) noexcept {
    switch (status) {
        case Sift1mLoadStatus::loaded: return "loaded";
        case Sift1mLoadStatus::invalid_path: return "invalid-path";
        case Sift1mLoadStatus::missing_file: return "missing-file";
        case Sift1mLoadStatus::io_failure: return "io-failure";
        case Sift1mLoadStatus::invalid_size: return "invalid-size";
        case Sift1mLoadStatus::invalid_dimension: return "invalid-dimension";
        case Sift1mLoadStatus::truncated_record: return "truncated-record";
        case Sift1mLoadStatus::trailing_data: return "trailing-data";
        case Sift1mLoadStatus::nonfinite_value: return "nonfinite-value";
        case Sift1mLoadStatus::allocation_failure: return "allocation-failure";
    }
    return "io-failure";
}

namespace detail {

std::string sha256_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open file for SHA-256");
    Sha256 hash;
    std::array<unsigned char, 64U * 1024U> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) hash.update(buffer.data(), static_cast<std::size_t>(count));
    }
    if (!input.eof()) throw std::runtime_error("file read failed during SHA-256");
    return hash.finish();
}

std::string sha256_bytes(std::string_view bytes) {
    Sha256 hash;
    hash.update(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
    return hash.finish();
}

Sift1mLoadResult load_sift1m_for_testing(const fs::path& dataset_root,
                                         const fs::path& repository_root,
                                         const Sift1mSchema& schema) {
    return load_impl(dataset_root, repository_root, schema);
}

}  // namespace detail
}  // namespace s10
