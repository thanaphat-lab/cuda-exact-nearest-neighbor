#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>

namespace s7_test {

class TuningFreeze {
  public:
    static TuningFreeze load(const std::string& path, const std::string& variant) {
        TuningFreeze result;
        std::ifstream input(path);
        if (!input) {
            throw std::runtime_error("could not open tuning freeze CSV");
        }
        std::string line;
        if (!std::getline(input, line) ||
            line != "schema_version,variant,N,D,block_size") {
            throw std::runtime_error("invalid tuning freeze CSV header");
        }
        while (std::getline(input, line)) {
            if (line.empty()) continue;
            std::stringstream stream(line);
            std::string schema;
            std::string row_variant;
            std::string n_text;
            std::string d_text;
            std::string block_text;
            if (!std::getline(stream, schema, ',') ||
                !std::getline(stream, row_variant, ',') ||
                !std::getline(stream, n_text, ',') ||
                !std::getline(stream, d_text, ',') ||
                !std::getline(stream, block_text, ',')) {
                throw std::runtime_error("malformed tuning freeze CSV row");
            }
            if (schema != "S7-TUNING-FREEZE-v1" || row_variant != variant) continue;
            const auto n = static_cast<std::uint64_t>(std::stoull(n_text));
            const auto d = static_cast<std::uint64_t>(std::stoull(d_text));
            const auto block = static_cast<std::uint32_t>(std::stoul(block_text));
            if (block != 128U && block != 256U && block != 512U) {
                throw std::runtime_error("unsupported frozen block size");
            }
            if (!result.values_.emplace(std::make_tuple(n, d), block).second) {
                throw std::runtime_error("duplicate tuning freeze case");
            }
        }
        return result;
    }

    std::uint32_t at(std::uint64_t n, std::uint64_t d) const {
        const auto found = values_.find(std::make_tuple(n, d));
        if (found == values_.end()) {
            throw std::runtime_error("missing tuning freeze case");
        }
        return found->second;
    }

    bool empty() const noexcept { return values_.empty(); }

  private:
    std::map<std::tuple<std::uint64_t, std::uint64_t>, std::uint32_t> values_;
};

}  // namespace s7_test
