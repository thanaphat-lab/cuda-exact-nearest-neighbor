#include "s6/cpu_evidence.hpp"

#include "cpu_evidence_internal.hpp"
#include "s6/cpu_build_identity.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

namespace s6 {
namespace {

constexpr std::uint64_t kMaximumPolicyDimension =
    (std::uint64_t{1} << 24) - 1;

class EvidenceUnavailable final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class Sha256 {
  public:
    Sha256() {
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
            throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
        }
        DWORD copied = 0;
        DWORD object_size = 0;
        if (BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                              &copied, 0) < 0) {
            throw std::runtime_error("BCrypt OBJECT_LENGTH failed");
        }
        object_.resize(object_size);
        if (BCryptCreateHash(algorithm_, &hash_, object_.data(), object_size, nullptr, 0, 0) <
            0) {
            throw std::runtime_error("BCryptCreateHash failed");
        }
    }

    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    ~Sha256() {
        if (hash_ != nullptr) {
            BCryptDestroyHash(hash_);
        }
        if (algorithm_ != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm_, 0);
        }
    }

    void update(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        while (size != 0) {
            const auto chunk = static_cast<ULONG>(
                std::min<std::size_t>(size, std::numeric_limits<ULONG>::max()));
            if (BCryptHashData(hash_, const_cast<PUCHAR>(bytes), chunk, 0) < 0) {
                throw std::runtime_error("BCryptHashData failed");
            }
            bytes += chunk;
            size -= chunk;
        }
    }

    std::string finish() {
        std::array<unsigned char, 32> digest{};
        if (BCryptFinishHash(hash_, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
            throw std::runtime_error("BCryptFinishHash failed");
        }
        static constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(digest.size() * 2);
        for (const auto byte : digest) {
            result.push_back(digits[byte >> 4U]);
            result.push_back(digits[byte & 0x0fU]);
        }
        return result;
    }

  private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<unsigned char> object_;
};

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot hash " + path.string());
    }
    Sha256 hash;
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            hash.update(buffer.data(), static_cast<std::size_t>(count));
        }
    }
    return hash.finish();
}

std::string sha256_text(const std::string& text) {
    Sha256 hash;
    if (!text.empty()) {
        hash.update(text.data(), text.size());
    }
    return hash.finish();
}

std::string normalize_command_output(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    return value;
}

std::string run_git(const std::string& arguments) {
    const std::string command = "\"\"" + std::string(build_identity::kGitPath) +
                                "\" -C \"" + build_identity::kSourceRoot +
                                "\" " + arguments + " 2>NUL\"";
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"),
                                                   &_pclose);
    if (!pipe) {
        throw EvidenceUnavailable("cannot execute configured Git");
    }
    std::string output;
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        output.append(buffer.data());
    }
    const int exit_code = _pclose(pipe.release());
    if (exit_code != 0) {
        throw EvidenceUnavailable("configured Git identity query failed");
    }
    return normalize_command_output(std::move(output));
}

struct RuntimeSourceIdentity {
    std::string revision;
    std::string working_tree_state;
    std::string working_tree_fingerprint;
    std::string source_manifest_sha256;
    std::string combined;
};

RuntimeSourceIdentity inspect_runtime_source_identity() {
    RuntimeSourceIdentity result;
    result.revision = run_git("rev-parse HEAD");
    const auto status = run_git("status --porcelain=v1 --untracked-files=all");
    result.working_tree_state = status.empty() ? "clean" : "dirty";
    result.working_tree_fingerprint = sha256_text(status);
    std::string source_manifest;
    const auto source_root = std::filesystem::path(build_identity::kSourceRoot);
    for (const auto& input : build_identity::kEvidenceInputs) {
        const auto path = source_root / input.relative_path;
        if (!std::filesystem::exists(path)) {
            throw EvidenceUnavailable(std::string("evidence-driving source missing: ") +
                                      input.relative_path);
        }
        const auto actual = sha256_file(path);
        if (actual != input.sha256) {
            throw std::runtime_error(std::string("evidence-driving source mismatch: ") +
                                     input.relative_path);
        }
        source_manifest += "source=";
        source_manifest += input.relative_path;
        source_manifest += "|";
        source_manifest += actual;
        source_manifest += "\n";
    }
    result.source_manifest_sha256 = sha256_text(source_manifest);
    result.combined = result.revision + "|" + result.working_tree_state + "|" +
                      result.working_tree_fingerprint + "|" +
                      result.source_manifest_sha256;
    return result;
}

std::filesystem::path current_executable_path() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= static_cast<DWORD>(buffer.size())) {
        throw std::runtime_error("cannot resolve current executable path");
    }
    return std::filesystem::weakly_canonical(
        std::filesystem::path(buffer.data(), buffer.data() + length));
}

std::uint32_t float_bits(float value) noexcept {
    std::uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

std::string workload_hash(DatasetView dataset, QueryView query) {
    Sha256 hash;
    hash.update(&dataset.candidate_count, sizeof(dataset.candidate_count));
    hash.update(&dataset.dimension, sizeof(dataset.dimension));
    hash.update(&dataset.element_count, sizeof(dataset.element_count));
    hash.update(&query.dimension, sizeof(query.dimension));
    hash.update(&query.element_count, sizeof(query.element_count));
    if (dataset.data != nullptr && dataset.element_count <=
                                       std::numeric_limits<std::size_t>::max() / sizeof(float)) {
        hash.update(dataset.data, dataset.element_count * sizeof(float));
    }
    if (query.data != nullptr &&
        query.element_count <= std::numeric_limits<std::size_t>::max() / sizeof(float)) {
        hash.update(query.data, query.element_count * sizeof(float));
    }
    return hash.finish();
}

std::map<std::string, std::string> parse_manifest(const std::string& text) {
    std::map<std::string, std::string> fields;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos || equals == 0) {
            continue;
        }
        const auto inserted = fields.emplace(line.substr(0, equals),
                                             line.substr(equals + 1));
        if (!inserted.second) {
            throw std::runtime_error("duplicate manifest field: " +
                                     line.substr(0, equals));
        }
    }
    return fields;
}

bool source_contains_in_order(std::string_view text,
                              const std::vector<std::string_view>& needles) {
    std::size_t position = 0;
    for (const auto needle : needles) {
        position = text.find(needle, position);
        if (position == std::string_view::npos) {
            return false;
        }
        position += needle.size();
    }
    return true;
}

const std::string& require_field(const std::map<std::string, std::string>& fields,
                                 const std::string& name) {
    const auto found = fields.find(name);
    if (found == fields.end() || found->second.empty()) {
        throw std::runtime_error("manifest field missing: " + name);
    }
    return found->second;
}

const std::string& require_present_field(
    const std::map<std::string, std::string>& fields, const std::string& name) {
    const auto found = fields.find(name);
    if (found == fields.end()) {
        throw std::runtime_error("manifest field missing: " + name);
    }
    return found->second;
}

std::string object_function_body(const std::string& disassembly,
                                 const std::string& symbol_prefix) {
    const auto heading = disassembly.find("\n" + symbol_prefix);
    if (heading == std::string::npos) {
        return {};
    }
    const auto next = disassembly.find("\n?", heading + 2);
    return disassembly.substr(heading, next == std::string::npos ? std::string::npos
                                                                 : next - heading);
}

std::string compact_lower(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char character) {
                    return std::isspace(character) != 0;
                }),
                value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

struct Instruction {
    std::uint64_t address = 0;
    std::string mnemonic;
    std::string operands;
};

std::vector<Instruction> parse_instructions(const std::string& body) {
    std::vector<Instruction> instructions;
    std::istringstream lines(body);
    std::string line;
    while (std::getline(lines, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        auto address_text = line.substr(0, colon);
        address_text.erase(0, address_text.find_first_not_of(" \t"));
        if (address_text.size() != 16 ||
            !std::all_of(address_text.begin(), address_text.end(), [](unsigned char value) {
                return std::isxdigit(value) != 0;
            })) {
            continue;
        }
        std::size_t position = colon + 1;
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position]))) {
            ++position;
        }
        std::size_t byte_count = 0;
        while (position + 2 <= line.size() &&
               std::isxdigit(static_cast<unsigned char>(line[position])) != 0 &&
               std::isxdigit(static_cast<unsigned char>(line[position + 1])) != 0 &&
               (position + 2 == line.size() ||
                std::isspace(static_cast<unsigned char>(line[position + 2])) != 0)) {
            position += 2;
            ++byte_count;
            while (position < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[position]))) {
                ++position;
            }
        }
        if (byte_count == 0 || position >= line.size() ||
            std::isalpha(static_cast<unsigned char>(line[position])) == 0) {
            continue;
        }
        const auto mnemonic_start = position;
        while (position < line.size() &&
               std::isalnum(static_cast<unsigned char>(line[position])) != 0) {
            ++position;
        }
        Instruction instruction;
        instruction.address = std::stoull(address_text, nullptr, 16);
        instruction.mnemonic = compact_lower(line.substr(mnemonic_start,
                                                          position - mnemonic_start));
        instruction.operands = compact_lower(line.substr(position));
        instructions.push_back(std::move(instruction));
    }
    return instructions;
}

bool parse_direct_address(const std::string& operands, std::uint64_t& address) {
    static const std::regex direct_pattern(R"(^([0-9a-f]{16})h?$)");
    std::smatch match;
    if (!std::regex_match(operands, match, direct_pattern)) {
        return false;
    }
    address = std::stoull(match[1].str(), nullptr, 16);
    return true;
}

bool parse_indirect_address(const std::string& operands, std::uint64_t& address) {
    static const std::regex indirect_pattern(R"(^qwordptr\[([0-9a-f]{16})h\]$)");
    std::smatch match;
    if (!std::regex_match(operands, match, indirect_pattern)) {
        return false;
    }
    address = std::stoull(match[1].str(), nullptr, 16);
    return true;
}

std::string address_from_map(const std::string& linker_map,
                              const std::string& symbol_fragment,
                              const std::string& object_owner) {
    std::istringstream lines(linker_map);
    std::string line;
    const std::regex absolute_address("(^|[[:space:]])([0-9A-Fa-f]{16})([[:space:]]|$)");
    std::string result;
    while (std::getline(lines, line)) {
        const auto symbol_position = line.find(symbol_fragment);
        if (symbol_position == std::string::npos ||
            (symbol_position != 0 &&
             std::isspace(static_cast<unsigned char>(line[symbol_position - 1])) == 0) ||
            line.find(object_owner) == std::string::npos ||
            line.find(" f   ") == std::string::npos) {
            continue;
        }
        std::smatch match;
        if (std::regex_search(line, match, absolute_address)) {
            std::string address = match[2].str();
            std::transform(address.begin(), address.end(), address.begin(),
                           [](unsigned char value) {
                               return static_cast<char>(std::toupper(value));
                           });
            if (!result.empty()) {
                return {};
            }
            result = address;
        }
    }
    return result;
}

bool validate_distance_loop(const std::vector<Instruction>& instructions,
                            bool object_code, std::uint64_t expected_fmaf_iat,
                             const std::string& expected_back_edge,
                            std::string& detail) {
    if (instructions.empty()) {
        detail = "production distance contains no decodable instructions";
        return false;
    }
    const auto forbidden = std::find_if(instructions.begin(), instructions.end(),
                                        [](const Instruction& instruction) {
                                            return instruction.mnemonic == "mulss" ||
                                                   instruction.mnemonic == "addss";
                                        });
    if (forbidden != instructions.end()) {
        detail = "production distance contains non-fused Float32 arithmetic";
        return false;
    }

    std::size_t loop_count = 0;
    for (std::size_t index = 0; index + 9 < instructions.size(); ++index) {
        const auto& load = instructions[index];
        if (load.mnemonic != "movss" ||
            load.operands != "xmm0,dwordptr[rbp+rbx*4]") {
            continue;
        }
        const auto& subtract = instructions[index + 1];
        const auto& store = instructions[index + 2];
        const auto& reload_rhs = instructions[index + 3];
        const auto& reload_lhs = instructions[index + 4];
        const auto& fma_call = instructions[index + 5];
        const auto& increment = instructions[index + 6];
        const auto& carry = instructions[index + 7];
        const auto& compare = instructions[index + 8];
        const auto& back_edge = instructions[index + 9];
        std::uint64_t branch_target = 0;
        if (subtract.mnemonic != "subss" ||
            subtract.operands != "xmm0,dwordptr[rsi+rbx*4]" ||
            store.mnemonic != "movss" || store.operands != "dwordptr[rsp+60h],xmm0" ||
            reload_rhs.mnemonic != "movss" ||
            reload_rhs.operands != "xmm1,dwordptr[rsp+60h]" ||
            reload_lhs.mnemonic != "movss" ||
            reload_lhs.operands != "xmm0,dwordptr[rsp+60h]" ||
            fma_call.mnemonic != "call" || increment.mnemonic != "inc" ||
            increment.operands != "rbx" || carry.mnemonic != "movaps" ||
            carry.operands != "xmm2,xmm0" || compare.mnemonic != "cmp" ||
             compare.operands != "rbx,rdi" ||
             back_edge.mnemonic != expected_back_edge ||
            !parse_direct_address(back_edge.operands, branch_target) ||
            branch_target != load.address) {
            continue;
        }
        if (object_code) {
            if (fma_call.operands != "qwordptr[__imp_fmaf]") {
                continue;
            }
        } else {
            std::uint64_t call_target = 0;
            if (!parse_indirect_address(fma_call.operands, call_target) ||
                (expected_fmaf_iat != 0 && call_target != expected_fmaf_iat)) {
                continue;
            }
        }

        bool index_initialized = false;
        bool accumulator_initialized = false;
        bool dimension_bound = false;
        for (std::size_t prefix = 0; prefix < index; ++prefix) {
            index_initialized = index_initialized ||
                                (instructions[prefix].mnemonic == "xor" &&
                                 instructions[prefix].operands == "ebx,ebx");
            accumulator_initialized = accumulator_initialized ||
                                      (instructions[prefix].mnemonic == "xorps" &&
                                       instructions[prefix].operands == "xmm2,xmm2");
            dimension_bound = dimension_bound ||
                              (instructions[prefix].mnemonic == "mov" &&
                               instructions[prefix].operands == "rdi,r8");
        }
        if (index_initialized && accumulator_initialized && dimension_bound) {
            ++loop_count;
        }
    }
    if (loop_count != 1) {
        detail = "production distance loop/control/data dependency is not uniquely established";
        return false;
    }
    return true;
}

bool validate_float64_distance_loop(const std::vector<Instruction>& instructions,
                                    bool object_code, std::uint64_t expected_fma_iat,
                                    const std::string& expected_back_edge,
                                    std::string& detail) {
    if (instructions.empty()) {
        detail = "Float64 distance contains no decodable instructions";
        return false;
    }
    const auto forbidden = std::find_if(instructions.begin(), instructions.end(),
                                        [](const Instruction& instruction) {
                                            return instruction.mnemonic == "mulsd" ||
                                                   instruction.mnemonic == "addsd" ||
                                                   instruction.mnemonic == "subss";
                                        });
    if (forbidden != instructions.end()) {
        detail = "Float64 distance contains wrong-type or non-fused arithmetic";
        return false;
    }

    std::size_t loop_count = 0;
    for (std::size_t index = 0; index + 12 < instructions.size(); ++index) {
        const auto& load_left = instructions[index];
        const auto& load_right = instructions[index + 1];
        const auto& convert_left = instructions[index + 2];
        const auto& convert_right = instructions[index + 3];
        const auto& subtract = instructions[index + 4];
        const auto& store = instructions[index + 5];
        const auto& reload_rhs = instructions[index + 6];
        const auto& reload_lhs = instructions[index + 7];
        const auto& fma_call = instructions[index + 8];
        const auto& increment = instructions[index + 9];
        const auto& carry = instructions[index + 10];
        const auto& compare = instructions[index + 11];
        const auto& back_edge = instructions[index + 12];
        std::uint64_t branch_target = 0;
        if (load_left.mnemonic != "movss" ||
            load_left.operands != "xmm1,dwordptr[rbp+rbx*4]" ||
            load_right.mnemonic != "movss" ||
            load_right.operands != "xmm0,dwordptr[rsi+rbx*4]" ||
            convert_left.mnemonic != "cvtps2pd" || convert_left.operands != "xmm1,xmm1" ||
            convert_right.mnemonic != "cvtps2pd" || convert_right.operands != "xmm0,xmm0" ||
            subtract.mnemonic != "subsd" || subtract.operands != "xmm1,xmm0" ||
            store.mnemonic != "movsd" || store.operands != "mmwordptr[rsp+60h],xmm1" ||
            reload_rhs.mnemonic != "movsd" ||
            reload_rhs.operands != "xmm1,mmwordptr[rsp+60h]" ||
            reload_lhs.mnemonic != "movsd" ||
            reload_lhs.operands != "xmm0,mmwordptr[rsp+60h]" ||
            fma_call.mnemonic != "call" || increment.mnemonic != "inc" ||
            increment.operands != "rbx" || carry.mnemonic != "movaps" ||
            carry.operands != "xmm2,xmm0" || compare.mnemonic != "cmp" ||
            compare.operands != "rbx,rdi" || back_edge.mnemonic != expected_back_edge ||
            !parse_direct_address(back_edge.operands, branch_target) ||
            branch_target != load_left.address) {
            continue;
        }
        if (object_code) {
            if (fma_call.operands != "qwordptr[__imp_fma]") {
                continue;
            }
        } else {
            std::uint64_t call_target = 0;
            if (!parse_indirect_address(fma_call.operands, call_target) ||
                (expected_fma_iat != 0 && call_target != expected_fma_iat)) {
                continue;
            }
        }
        bool index_initialized = false;
        bool accumulator_initialized = false;
        bool dimension_bound = false;
        for (std::size_t prefix = 0; prefix < index; ++prefix) {
            index_initialized = index_initialized ||
                                (instructions[prefix].mnemonic == "xor" &&
                                 instructions[prefix].operands == "ebx,ebx");
            accumulator_initialized = accumulator_initialized ||
                                      (instructions[prefix].mnemonic == "xorps" &&
                                       instructions[prefix].operands == "xmm2,xmm2");
            dimension_bound = dimension_bound ||
                              (instructions[prefix].mnemonic == "mov" &&
                               instructions[prefix].operands == "rdi,r8");
        }
        if (index_initialized && accumulator_initialized && dimension_bound) {
            ++loop_count;
        }
    }
    if (loop_count != 1) {
        detail = "Float64 distance loop/control/type/data dependency is not uniquely established";
        return false;
    }
    return true;
}

bool validate_candidate_loop(const std::vector<Instruction>& instructions,
                             bool object_code, std::uint64_t expected_production_address,
                             std::string& detail) {
    std::size_t loop_count = 0;
    for (std::size_t index = 0; index + 1 < instructions.size(); ++index) {
        if (instructions[index].mnemonic != "cmp" ||
            instructions[index].operands != "rsi,qwordptr[r13+8]" ||
            instructions[index + 1].mnemonic != "jae") {
            continue;
        }
        std::uint64_t exit_target = 0;
        if (!parse_direct_address(instructions[index + 1].operands, exit_target) ||
            exit_target <= instructions[index].address) {
            continue;
        }
        bool observer_call = false;
        bool offset_multiply = false;
        bool production_call = false;
        bool candidate_increment = false;
        bool correct_back_edge = false;
        for (std::size_t cursor = index + 2;
             cursor < instructions.size() && instructions[cursor].address < exit_target;
             ++cursor) {
            const auto& instruction = instructions[cursor];
            if (!production_call && instruction.mnemonic == "call" &&
                instruction.operands == "qwordptr[rax+8]") {
                observer_call = true;
            }
            if (!production_call && instruction.mnemonic == "imul" &&
                instruction.operands == "rcx,rdx") {
                offset_multiply = true;
            }
            if (instruction.mnemonic == "call") {
                if (object_code && instruction.operands.find("?production_distance@") == 0) {
                    production_call = observer_call && offset_multiply;
                } else if (!object_code) {
                    std::uint64_t target = 0;
                    if (parse_direct_address(instruction.operands, target) &&
                        target == expected_production_address) {
                        production_call = observer_call && offset_multiply;
                    }
                }
            }
            if (production_call && instruction.mnemonic == "inc" &&
                instruction.operands == "rsi") {
                candidate_increment = true;
            }
            if (candidate_increment && instruction.mnemonic == "jmp") {
                std::uint64_t target = 0;
                correct_back_edge = parse_direct_address(instruction.operands, target) &&
                                    target == instructions[index].address;
            }
        }
        if (observer_call && offset_multiply && production_call && candidate_increment &&
            correct_back_edge) {
            ++loop_count;
        }
    }
    if (loop_count != 1) {
        detail = "production candidate loop/call target/back-edge is not uniquely established";
        return false;
    }
    return true;
}

struct ExpectedGraphAddresses {
    std::uint64_t production_float32 = 0;
    std::uint64_t validator_float32 = 0;
    std::uint64_t reference_float64 = 0;
    std::uint64_t validator_float64 = 0;
    std::uint64_t production_caller = 0;
    std::uint64_t fmaf_iat = 0;
    std::uint64_t fma_iat = 0;
};

GraphInspectionAssessment inspect_graph(
    const std::string& object_disassembly,
    const std::string& executable_disassembly,
    const std::string& linker_map,
    const ExpectedGraphAddresses& expected = {}) {
    GraphInspectionAssessment assessment;
    const std::string object_owner = "s6_cpu_baseline:cpu_baseline.cpp.obj";

    const auto inspect_path = [&](const std::string& label,
                                  const std::string& symbol_prefix,
                                  const std::string& map_fragment,
                                  bool float64_path,
                                  const std::string& back_edge,
                                  std::uint64_t runtime_address,
                                  std::uint64_t expected_iat,
                                  EvidenceStatus& status,
                                  std::string& detail) {
        const auto object_body = object_function_body(object_disassembly, symbol_prefix);
        if (object_body.empty()) {
            status = EvidenceStatus::failed;
            detail = label + " symbol body missing from object";
            return std::uint64_t{0};
        }
        const auto object_instructions = parse_instructions(object_body);
        const bool object_valid = float64_path
                                      ? validate_float64_distance_loop(
                                            object_instructions, true, 0, back_edge, detail)
                                      : validate_distance_loop(
                                            object_instructions, true, 0, back_edge, detail);
        if (!object_valid) {
            status = EvidenceStatus::failed;
            detail = label + ": " + detail;
            return std::uint64_t{0};
        }

        const auto address_text = address_from_map(linker_map, map_fragment, object_owner);
        if (address_text.empty()) {
            status = EvidenceStatus::failed;
            detail = label + " address missing or ambiguous in linker map";
            return std::uint64_t{0};
        }
        const auto mapped_address = std::stoull(address_text, nullptr, 16);
        if (runtime_address != 0 && mapped_address != runtime_address) {
            status = EvidenceStatus::failed;
            detail = label + " linker-map address disagrees with running function";
            return std::uint64_t{0};
        }
        const auto executable_position = executable_disassembly.find(address_text + ":");
        if (executable_position == std::string::npos) {
            status = EvidenceStatus::failed;
            detail = label + " mapped body missing from executable disassembly";
            return std::uint64_t{0};
        }
        const auto return_position = executable_disassembly.find("ret", executable_position);
        const auto executable_body = executable_disassembly.substr(
            executable_position,
            return_position == std::string::npos
                ? 2200
                : return_position - executable_position + 3);
        const auto executable_instructions = parse_instructions(executable_body);
        const bool executable_valid = float64_path
                                           ? validate_float64_distance_loop(
                                                 executable_instructions, false,
                                                 expected_iat, back_edge, detail)
                                           : validate_distance_loop(
                                                 executable_instructions, false,
                                                 expected_iat, back_edge, detail);
        if (!executable_valid) {
            status = EvidenceStatus::failed;
            detail = label + ": " + detail;
            return std::uint64_t{0};
        }
        status = EvidenceStatus::passed;
        detail = label + " object/executable graph and call target verified";
        return mapped_address;
    };

    const auto production_address = inspect_path(
        "production-float32", "?production_distance@", "?production_distance@?A0",
        false, "jb", expected.production_float32, expected.fmaf_iat,
        assessment.production_float32_status, assessment.production_float32_detail);
    inspect_path("validator-float32", "?validator_distance@",
                 "?validator_distance@?A0", false, "jne",
                 expected.validator_float32, expected.fmaf_iat,
                 assessment.validator_float32_status,
                 assessment.validator_float32_detail);
    inspect_path("reference-float64", "?reference_distance@",
                 "?reference_distance@?A0", true, "jb",
                 expected.reference_float64, expected.fma_iat,
                 assessment.reference_float64_status,
                 assessment.reference_float64_detail);
    inspect_path("validator-float64", "?validator_reference_distance@",
                 "?validator_reference_distance@?A0", true, "jne",
                 expected.validator_float64, expected.fma_iat,
                 assessment.validator_float64_status,
                 assessment.validator_float64_detail);

    if (assessment.production_float32_status == EvidenceStatus::passed) {
        const auto caller_object_body = object_function_body(
            object_disassembly, "?cpu_search_float32_impl@detail@s6@@");
        std::string caller_detail;
        if (caller_object_body.empty() ||
            !validate_candidate_loop(parse_instructions(caller_object_body), true,
                                     production_address, caller_detail)) {
            assessment.production_float32_status = EvidenceStatus::failed;
            assessment.production_float32_detail = caller_detail.empty()
                ? "production candidate caller missing from object"
                : caller_detail;
        } else {
            const auto caller_address_text = address_from_map(
                linker_map, "?cpu_search_float32_impl@detail@s6@@", object_owner);
            const auto caller_position =
                executable_disassembly.find(caller_address_text + ":");
            if (caller_address_text.empty() || caller_position == std::string::npos) {
                assessment.production_float32_status = EvidenceStatus::failed;
                assessment.production_float32_detail =
                    "mapped production candidate loop missing";
            } else {
                const auto caller_address =
                    std::stoull(caller_address_text, nullptr, 16);
                const auto caller_return =
                    executable_disassembly.find("ret", caller_position);
                const auto caller_body = executable_disassembly.substr(
                    caller_position,
                    caller_return == std::string::npos
                        ? 8000
                        : caller_return - caller_position + 3);
                if ((expected.production_caller != 0 &&
                     caller_address != expected.production_caller) ||
                    !validate_candidate_loop(parse_instructions(caller_body), false,
                                             production_address, caller_detail)) {
                    assessment.production_float32_status = EvidenceStatus::failed;
                    assessment.production_float32_detail =
                        caller_detail.empty()
                            ? "production caller runtime address mismatch"
                            : caller_detail;
                }
            }
        }
    }

    const std::array statuses = {
        assessment.production_float32_status,
        assessment.validator_float32_status,
        assessment.reference_float64_status,
        assessment.validator_float64_status};
    assessment.status = std::all_of(statuses.begin(), statuses.end(),
                                    [](EvidenceStatus status) {
                                        return status == EvidenceStatus::passed;
                                    })
                            ? EvidenceStatus::passed
                            : EvidenceStatus::failed;
    assessment.detail = assessment.status == EvidenceStatus::passed
                            ? "all four numerical path graphs verified"
                            : assessment.production_float32_detail + "; " +
                                  assessment.validator_float32_detail + "; " +
                                  assessment.reference_float64_detail + "; " +
                                  assessment.validator_float64_detail;
    return assessment;
}

struct ExpectedArtifacts {
    std::string target;
    std::filesystem::path compiler;
    std::filesystem::path policy;
    std::filesystem::path baseline_header;
    std::filesystem::path evidence_header;
    std::filesystem::path baseline_source;
    std::filesystem::path evidence_source;
    std::filesystem::path cmake_source;
    std::filesystem::path compile_commands;
    std::filesystem::path object;
    std::filesystem::path library;
    std::filesystem::path executable;
    std::filesystem::path map;
    std::filesystem::path object_disassembly;
    std::filesystem::path executable_disassembly;
    std::filesystem::path symbols;
    std::filesystem::path library_symbols;
};

ExpectedArtifacts expected_artifacts() {
    ExpectedArtifacts result;
    const auto source_root = std::filesystem::weakly_canonical(build_identity::kSourceRoot);
    const auto build_root = std::filesystem::weakly_canonical(build_identity::kBuildRoot);
    result.executable = current_executable_path();
    if (result.executable.parent_path() != build_root) {
        throw std::runtime_error("running executable is outside configured build root");
    }
    result.target = result.executable.stem().string();
    result.compiler = std::filesystem::weakly_canonical(build_identity::kCompilerPath);
    result.policy = source_root / "docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md";
    result.baseline_header = source_root / "include/s6/cpu_baseline.hpp";
    result.evidence_header = source_root / "include/s6/cpu_evidence.hpp";
    result.baseline_source = source_root / "src/cpu_baseline.cpp";
    result.evidence_source = source_root / "src/cpu_evidence.cpp";
    result.cmake_source = source_root / "CMakeLists.txt";
    result.compile_commands = build_root / build_identity::kCompileCommandsName;
    result.object = build_root / build_identity::kBaselineObjectRelativePath;
    result.library = build_root / build_identity::kBaselineLibraryName;
    result.map = build_root / (result.target + ".map");
    const auto prefix = build_root / "evidence" / result.target;
    result.object_disassembly = prefix.string() + ".object.disasm.txt";
    result.executable_disassembly = prefix.string() + ".executable.disasm.txt";
    result.symbols = prefix.string() + ".symbols.txt";
    result.library_symbols = prefix.string() + ".library.symbols.txt";
    return result;
}

void require_expected_path(const std::map<std::string, std::string>& fields,
                           const std::string& field,
                           const std::filesystem::path& expected) {
    const auto actual = std::filesystem::weakly_canonical(require_field(fields, field));
    if (actual != std::filesystem::weakly_canonical(expected)) {
        throw std::runtime_error("artifact path is not independently expected: " + field);
    }
}

std::vector<unsigned char> read_prefix(const std::filesystem::path& path,
                                       std::size_t count) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot inspect artifact role: " + path.string());
    }
    std::vector<unsigned char> bytes(count);
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(input.gcount()));
    return bytes;
}

bool is_amd64_coff_object(const std::filesystem::path& path) {
    const auto bytes = read_prefix(path, 20);
    return bytes.size() == 20 && bytes[0] == 0x64U && bytes[1] == 0x86U &&
           (bytes[2] != 0U || bytes[3] != 0U);
}

bool is_coff_archive(const std::filesystem::path& path) {
    const auto bytes = read_prefix(path, 8);
    static constexpr std::array<unsigned char, 8> signature = {
        '!', '<', 'a', 'r', 'c', 'h', '>', '\n'};
    return bytes.size() == signature.size() &&
           std::equal(bytes.begin(), bytes.end(), signature.begin());
}

bool is_pe64_amd64(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    IMAGE_DOS_HEADER dos{};
    input.read(reinterpret_cast<char*>(&dos), sizeof(dos));
    if (!input || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) {
        return false;
    }
    input.seekg(dos.e_lfanew, std::ios::beg);
    DWORD signature = 0;
    IMAGE_FILE_HEADER file_header{};
    WORD optional_magic = 0;
    input.read(reinterpret_cast<char*>(&signature), sizeof(signature));
    input.read(reinterpret_cast<char*>(&file_header), sizeof(file_header));
    input.read(reinterpret_cast<char*>(&optional_magic), sizeof(optional_magic));
    return input && signature == IMAGE_NT_SIGNATURE &&
           file_header.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           optional_magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
}

const IMAGE_NT_HEADERS64& running_nt_headers() {
    const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (base == nullptr) {
        throw std::runtime_error("running module base unavailable");
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        throw std::runtime_error("running module DOS header invalid");
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        throw std::runtime_error("running module PE64 header invalid");
    }
    return *nt;
}

std::uint64_t executable_preferred_image_base() {
    std::ifstream input(current_executable_path(), std::ios::binary);
    if (!input) {
        throw EvidenceUnavailable("cannot read running executable PE header");
    }
    IMAGE_DOS_HEADER dos{};
    input.read(reinterpret_cast<char*>(&dos), sizeof(dos));
    if (!input || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) {
        throw std::runtime_error("running executable DOS header invalid");
    }
    input.seekg(dos.e_lfanew, std::ios::beg);
    IMAGE_NT_HEADERS64 nt{};
    input.read(reinterpret_cast<char*>(&nt), sizeof(nt));
    if (!input || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        throw std::runtime_error("running executable file PE64 header invalid");
    }
    return nt.OptionalHeader.ImageBase;
}

std::uint64_t preferred_address(std::uintptr_t runtime_address) {
    const auto loaded_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (runtime_address < loaded_base) {
        throw std::runtime_error("function address is outside running module");
    }
    return executable_preferred_image_base() + (runtime_address - loaded_base);
}

std::uint64_t preferred_import_iat_address(const char* import_name) {
    const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    const auto& nt = running_nt_headers();
    const auto& directory =
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0) {
        throw std::runtime_error("running module import directory missing");
    }
    const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
        base + directory.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor) {
        if (descriptor->OriginalFirstThunk == 0 || descriptor->FirstThunk == 0) {
            continue;
        }
        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(
            base + descriptor->OriginalFirstThunk);
        for (std::size_t index = 0; names[index].u1.AddressOfData != 0; ++index) {
            if (IMAGE_SNAP_BY_ORDINAL64(names[index].u1.Ordinal)) {
                continue;
            }
            const auto* imported = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                base + names[index].u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(imported->Name), import_name) == 0) {
                return executable_preferred_image_base() + descriptor->FirstThunk +
                       index * sizeof(IMAGE_THUNK_DATA64);
            }
        }
    }
    throw std::runtime_error(std::string("running executable import missing: ") +
                             import_name);
}

void verify_artifact_roles(const ExpectedArtifacts& expected) {
    if (!is_pe64_amd64(expected.compiler)) {
        throw std::runtime_error("configured compiler is not an AMD64 PE image");
    }
    if (!is_amd64_coff_object(expected.object)) {
        throw std::runtime_error("baseline object is not an AMD64 COFF object");
    }
    if (!is_coff_archive(expected.library)) {
        throw std::runtime_error("baseline library is not a COFF archive");
    }
    if (!is_pe64_amd64(expected.executable)) {
        throw std::runtime_error("running executable is not an AMD64 PE image");
    }
    const std::set<std::filesystem::path> binary_roles = {
        std::filesystem::weakly_canonical(expected.object),
        std::filesystem::weakly_canonical(expected.library),
        std::filesystem::weakly_canonical(expected.executable)};
    if (binary_roles.size() != 3) {
        throw std::runtime_error("object/library/executable roles are duplicated");
    }
}

CoverageAssessment assess_bitmap(std::uint64_t expected_count,
                                 const std::vector<unsigned char>& bitmap,
                                 std::uint64_t unique_count,
                                 std::uint64_t duplicate_count,
                                 std::uint64_t out_of_range_count, bool completed) {
    CoverageAssessment result;
    result.expected_count = expected_count;
    result.observed_unique_count = unique_count;
    result.duplicate_count = duplicate_count;
    result.out_of_range_count = out_of_range_count;
    result.completed = completed;
    result.missing_count = expected_count >= unique_count ? expected_count - unique_count : 0;

    bool bitmap_complete = bitmap.size() == static_cast<std::size_t>((expected_count + 7U) / 8U);
    if (bitmap_complete) {
        for (std::uint64_t index = 0; index < expected_count; ++index) {
            const auto byte = static_cast<std::size_t>(index / 8U);
            const auto mask = static_cast<unsigned char>(1U << (index % 8U));
            if ((bitmap[byte] & mask) == 0) {
                bitmap_complete = false;
                break;
            }
        }
    }

    if (duplicate_count != 0 || out_of_range_count != 0) {
        result.status = EvidenceStatus::failed;
    } else if (!completed) {
        result.status = EvidenceStatus::blocked;
    } else if (!bitmap_complete || result.missing_count != 0 ||
               unique_count != expected_count) {
        result.status = EvidenceStatus::failed;
    } else {
        result.status = EvidenceStatus::passed;
    }
    return result;
}

class CoverageObserver final : public detail::CandidateVisitObserver {
  public:
    explicit CoverageObserver(std::uint64_t expected_count)
        : expected_count_(expected_count),
          bitmap_(static_cast<std::size_t>((expected_count + 7U) / 8U), 0) {}

    void observe(std::uint64_t index) noexcept override {
        if (index >= expected_count_) {
            ++out_of_range_count_;
            return;
        }
        const auto byte = static_cast<std::size_t>(index / 8U);
        const auto mask = static_cast<unsigned char>(1U << (index % 8U));
        if ((bitmap_[byte] & mask) != 0) {
            ++duplicate_count_;
        } else {
            bitmap_[byte] = static_cast<unsigned char>(bitmap_[byte] | mask);
            ++unique_count_;
        }
    }

    void finish(bool completed) noexcept override { completed_ = completed; }

    CoverageAssessment assessment() const {
        return assess_bitmap(expected_count_, bitmap_, unique_count_, duplicate_count_,
                             out_of_range_count_, completed_);
    }

  private:
    std::uint64_t expected_count_ = 0;
    std::vector<unsigned char> bitmap_;
    std::uint64_t unique_count_ = 0;
    std::uint64_t duplicate_count_ = 0;
    std::uint64_t out_of_range_count_ = 0;
    bool completed_ = false;
};

}  // namespace

CoverageAssessment assess_candidate_visitation(
    std::uint64_t expected_count, const std::vector<std::uint64_t>& visits,
    bool completed) {
    try {
        CoverageObserver observer(expected_count);
        for (const auto index : visits) {
            observer.observe(index);
        }
        observer.finish(completed);
        return observer.assessment();
    } catch (const std::bad_alloc&) {
        CoverageAssessment blocked;
        blocked.expected_count = expected_count;
        return blocked;
    }
}

GraphInspectionAssessment assess_graph_disassembly_for_testing(
    const std::string& object_disassembly,
    const std::string& executable_disassembly,
    const std::string& linker_map) {
    return inspect_graph(object_disassembly, executable_disassembly, linker_map);
}

CpuBuildEvidence verify_cpu_build_evidence(const std::string& manifest_path) {
    CpuBuildEvidence result;
    try {
        if (!std::filesystem::exists(manifest_path)) {
            throw EvidenceUnavailable("evidence manifest unavailable");
        }
        const auto fields = parse_manifest(read_text(manifest_path));
        if (require_field(fields, "schema") != kCpuEvidenceSchema) {
            throw std::runtime_error("unsupported evidence schema");
        }
        const auto expected = expected_artifacts();
        if (require_field(fields, "target") != expected.target) {
            throw std::runtime_error("manifest target is not the running executable");
        }
        if (std::string(build_identity::kWorkingTreeState) != "clean" ||
            require_field(fields, "working_tree_state") != "clean") {
            throw std::runtime_error("acceptance build identity is not clean");
        }
        const std::vector<std::pair<std::string, std::string>> identities = {
            {"revision", build_identity::kRevision},
            {"working_tree_state", build_identity::kWorkingTreeState},
            {"working_tree_fingerprint", build_identity::kWorkingTreeFingerprint},
            {"source_manifest_sha256", build_identity::kSourceManifestSha256},
            {"compiler_id", build_identity::kCompilerId},
            {"compiler_version", build_identity::kCompilerVersion},
            {"build_configuration", build_identity::kBuildConfiguration},
            {"required_compile_flags", build_identity::kRequiredCompileFlags},
            {"policy_sha256", build_identity::kPolicySha256},
            {"baseline_header_sha256", build_identity::kBaselineHeaderSha256},
            {"evidence_header_sha256", build_identity::kEvidenceHeaderSha256},
            {"baseline_source_sha256", build_identity::kBaselineSourceSha256},
            {"evidence_source_sha256", build_identity::kEvidenceSourceSha256},
            {"cmake_sha256", build_identity::kCmakeSha256}};
        for (const auto& identity : identities) {
            if (require_field(fields, identity.first) != identity.second) {
                throw std::runtime_error("build identity mismatch: " + identity.first);
            }
        }
        const auto runtime_source = inspect_runtime_source_identity();
        if (runtime_source.working_tree_state != "clean" ||
            runtime_source.revision != build_identity::kRevision ||
            runtime_source.working_tree_state != build_identity::kWorkingTreeState ||
            runtime_source.working_tree_fingerprint !=
                build_identity::kWorkingTreeFingerprint ||
            runtime_source.source_manifest_sha256 !=
                build_identity::kSourceManifestSha256) {
            throw std::runtime_error(
                "runtime Git/source identity disagrees with the compiled build identity");
        }

        const std::vector<std::pair<std::string, std::filesystem::path>> expected_paths = {
            {"compiler_path", expected.compiler},
            {"policy_path", expected.policy},
            {"baseline_header_path", expected.baseline_header},
            {"evidence_header_path", expected.evidence_header},
            {"baseline_source_path", expected.baseline_source},
            {"evidence_source_path", expected.evidence_source},
            {"cmake_path", expected.cmake_source},
            {"compile_commands_path", expected.compile_commands},
            {"object_path", expected.object},
            {"library_path", expected.library},
            {"executable_path", expected.executable},
            {"map_path", expected.map},
            {"object_disassembly_path", expected.object_disassembly},
            {"executable_disassembly_path", expected.executable_disassembly},
            {"symbols_path", expected.symbols},
            {"library_symbols_path", expected.library_symbols}};
        for (const auto& artifact : expected_paths) {
            if (!std::filesystem::exists(artifact.second)) {
                throw EvidenceUnavailable("expected artifact unavailable: " + artifact.first);
            }
            require_expected_path(fields, artifact.first, artifact.second);
        }
        verify_artifact_roles(expected);

        const std::vector<std::tuple<std::string, std::string, std::filesystem::path>>
            artifacts = {
                {"compiler_path", "compiler_sha256", expected.compiler},
                {"policy_path", "policy_sha256", expected.policy},
                {"baseline_header_path", "baseline_header_sha256", expected.baseline_header},
                {"evidence_header_path", "evidence_header_sha256", expected.evidence_header},
                {"baseline_source_path", "baseline_source_sha256", expected.baseline_source},
                {"evidence_source_path", "evidence_source_sha256", expected.evidence_source},
                {"cmake_path", "cmake_sha256", expected.cmake_source},
                {"compile_commands_path", "compile_commands_sha256",
                 expected.compile_commands},
                {"object_path", "object_sha256", expected.object},
                {"library_path", "library_sha256", expected.library},
                {"executable_path", "executable_sha256", expected.executable},
                {"map_path", "map_sha256", expected.map},
                {"object_disassembly_path", "object_disassembly_sha256",
                 expected.object_disassembly},
                {"executable_disassembly_path", "executable_disassembly_sha256",
                 expected.executable_disassembly},
                {"symbols_path", "symbols_sha256", expected.symbols},
                {"library_symbols_path", "library_symbols_sha256",
                 expected.library_symbols}};
        for (const auto& artifact : artifacts) {
            if (sha256_file(std::get<2>(artifact)) !=
                require_field(fields, std::get<1>(artifact))) {
                throw std::runtime_error("artifact hash mismatch: " +
                                         std::get<0>(artifact));
            }
        }

        const auto source = read_text(expected.baseline_source);
        if (!source_contains_in_order(
                source, {"float accumulator = 0.0F;",
                         "for (std::size_t j = 0; j < dimension; ++j)",
                         "candidate[j] - query[j]",
                         "std::fma(rounded_difference, rounded_difference, accumulator)"})) {
            throw std::runtime_error("production source graph mismatch");
        }
        const auto compile_commands = read_text(expected.compile_commands);
        const auto baseline_command = compile_commands.find("src\\\\cpu_baseline.cpp");
        const auto alternate_command = compile_commands.find("src/cpu_baseline.cpp");
        const auto command_position =
            baseline_command == std::string::npos ? alternate_command : baseline_command;
        if (command_position == std::string::npos) {
            throw std::runtime_error("production compile command missing");
        }
        const auto command_window = compile_commands.substr(
            command_position > 1000 ? command_position - 1000 : 0, 2200);
        std::istringstream flags(build_identity::kRequiredCompileFlags);
        std::string flag;
        while (std::getline(flags, flag, '|')) {
            if (command_window.find(flag) == std::string::npos) {
                throw std::runtime_error("required compiler flag missing: " + flag);
            }
        }
        if (command_window.find("/fp:fast") != std::string::npos ||
            command_window.find(" /GL") != std::string::npos ||
            command_window.find("/LTCG") != std::string::npos) {
            throw std::runtime_error("unsupported transformation flag present");
        }

        const auto symbols = read_text(expected.symbols);
        if (symbols.find("| ?cpu_search_float32_impl@detail@s6@@") ==
                std::string::npos ||
            symbols.find("| ?production_distance@") == std::string::npos ||
            symbols.find("| ?validator_distance@") == std::string::npos ||
            symbols.find("| ?reference_distance@") == std::string::npos ||
            symbols.find("| ?validator_reference_distance@") == std::string::npos ||
            symbols.find("| __imp_fmaf") == std::string::npos ||
            symbols.find("| __imp_fma") == std::string::npos) {
            throw std::runtime_error("baseline object symbols/relocations mismatch");
        }
        const auto library_symbols = read_text(expected.library_symbols);
        if (library_symbols.find("?cpu_search_float32_impl@detail@s6@@") ==
            std::string::npos) {
            throw std::runtime_error("baseline library does not expose production caller");
        }

        const auto object_disassembly = read_text(expected.object_disassembly);
        const auto executable_disassembly = read_text(expected.executable_disassembly);
        const auto linker_map = read_text(expected.map);
        ExpectedGraphAddresses graph_addresses;
        graph_addresses.production_float32 =
            preferred_address(detail::production_distance_function_address());
        graph_addresses.validator_float32 =
            preferred_address(detail::validator_distance_function_address());
        graph_addresses.reference_float64 =
            preferred_address(detail::reference_distance_function_address());
        graph_addresses.validator_float64 =
            preferred_address(detail::validator_reference_distance_function_address());
        graph_addresses.production_caller =
            preferred_address(detail::cpu_search_float32_impl_function_address());
        graph_addresses.fmaf_iat = preferred_import_iat_address("fmaf");
        graph_addresses.fma_iat = preferred_import_iat_address("fma");
        const auto graph = inspect_graph(object_disassembly, executable_disassembly,
                                         linker_map, graph_addresses);

        result.identity_status_ = EvidenceStatus::passed;
        result.graph_status_ = graph.status;
        result.production_float32_graph_status_ = graph.production_float32_status;
        result.validator_float32_graph_status_ = graph.validator_float32_status;
        result.reference_float64_graph_status_ = graph.reference_float64_status;
        result.validator_float64_graph_status_ = graph.validator_float64_status;
        result.graph_defect_demonstrated_ = graph.status == EvidenceStatus::failed;
        result.identity_ = require_field(fields, "revision") + "|" +
                           require_field(fields, "working_tree_state") + "|" +
                           require_field(fields, "target") + "|exe=" +
                           require_field(fields, "executable_sha256") + "|obj=" +
                           require_field(fields, "object_sha256") + "|compiler=" +
                           require_field(fields, "compiler_version");
        result.detail_ = graph.detail;
        result.revision_ = require_field(fields, "revision");
        result.working_tree_state_ = require_field(fields, "working_tree_state");
        result.working_tree_fingerprint_ =
            require_field(fields, "working_tree_fingerprint");
        result.source_manifest_sha256_ =
            require_field(fields, "source_manifest_sha256");
        result.source_identity_ = runtime_source.combined;
        result.compiler_id_ = require_field(fields, "compiler_id");
        result.compiler_version_ = require_field(fields, "compiler_version");
        result.build_configuration_ =
            require_field(fields, "build_configuration");
        result.required_compile_flags_ = require_field(fields, "required_compile_flags");
    } catch (const EvidenceUnavailable& error) {
        result.identity_status_ = EvidenceStatus::blocked;
        result.graph_status_ = EvidenceStatus::blocked;
        result.production_float32_graph_status_ = EvidenceStatus::blocked;
        result.validator_float32_graph_status_ = EvidenceStatus::blocked;
        result.reference_float64_graph_status_ = EvidenceStatus::blocked;
        result.validator_float64_graph_status_ = EvidenceStatus::blocked;
        result.graph_defect_demonstrated_ = false;
        result.detail_ = error.what();
    } catch (const std::exception& error) {
        result.identity_status_ = EvidenceStatus::failed;
        result.graph_status_ = EvidenceStatus::blocked;
        result.production_float32_graph_status_ = EvidenceStatus::blocked;
        result.validator_float32_graph_status_ = EvidenceStatus::blocked;
        result.reference_float64_graph_status_ = EvidenceStatus::blocked;
        result.validator_float64_graph_status_ = EvidenceStatus::blocked;
        result.graph_defect_demonstrated_ = false;
        result.detail_ = error.what();
    }
    return result;
}

bool CpuProductionEvidence::matches(DatasetView dataset, QueryView query,
                                    const ProductionOutcome& production) const {
    try {
        if (identity_status_ != EvidenceStatus::passed ||
            workload_hash(dataset, query) != workload_identity_ ||
            production.invocation_status != invocation_status_ ||
            production.result.has_value() != has_result_) {
            return false;
        }
        if (!has_result_) {
            return true;
        }
        return production.result->index == result_index_ &&
               float_bits(production.result->squared_distance) == result_distance_bits_;
    } catch (const std::exception&) {
        return false;
    }
}

EvidencedProductionOutcome cpu_search_float32_evidenced(
    DatasetView dataset, QueryView query, const CpuBuildEvidence& build_evidence) {
    EvidencedProductionOutcome result;
    result.evidence.identity_status_ = build_evidence.identity_status();
    result.evidence.graph_status_ = build_evidence.graph_status();
    result.evidence.production_float32_graph_status_ =
        build_evidence.production_float32_graph_status();
    result.evidence.validator_float32_graph_status_ =
        build_evidence.validator_float32_graph_status();
    result.evidence.reference_float64_graph_status_ =
        build_evidence.reference_float64_graph_status();
    result.evidence.validator_float64_graph_status_ =
        build_evidence.validator_float64_graph_status();
    result.evidence.graph_defect_demonstrated_ =
        build_evidence.graph_defect_demonstrated();
    result.evidence.identity_ = build_evidence.identity();
    result.evidence.detail_ = build_evidence.detail();
    result.evidence.source_identity_ = build_evidence.source_identity();
    try {
        result.evidence.workload_identity_ = workload_hash(dataset, query);
        CoverageObserver observer(dataset.candidate_count);
        result.production = detail::cpu_search_float32_impl(dataset, query, &observer);
        result.evidence.coverage_ = observer.assessment();
    } catch (const std::bad_alloc&) {
        result.production = cpu_search_float32(dataset, query);
        result.evidence.coverage_.expected_count = dataset.candidate_count;
        result.evidence.coverage_.status = EvidenceStatus::blocked;
        result.evidence.detail_ += "; coverage allocation failure";
    } catch (const std::exception& error) {
        result.production = cpu_search_float32(dataset, query);
        result.evidence.coverage_.expected_count = dataset.candidate_count;
        result.evidence.coverage_.status = EvidenceStatus::blocked;
        result.evidence.detail_ += "; evidence failure: ";
        result.evidence.detail_ += error.what();
    }
    result.evidence.invocation_status_ = result.production.invocation_status;
    result.evidence.has_result_ = result.production.result.has_value();
    if (result.production.result) {
        result.evidence.result_index_ = result.production.result->index;
        result.evidence.result_distance_bits_ =
            float_bits(result.production.result->squared_distance);
    }
    return result;
}

namespace {

const char* evidence_status_token(EvidenceStatus status) {
    switch (status) {
        case EvidenceStatus::passed:
            return "passed";
        case EvidenceStatus::failed:
            return "failed";
        case EvidenceStatus::blocked:
            return "blocked";
    }
    return "blocked";
}

EvidenceStatus parse_evidence_status(const std::string& value) {
    if (value == "passed") {
        return EvidenceStatus::passed;
    }
    if (value == "failed") {
        return EvidenceStatus::failed;
    }
    return EvidenceStatus::blocked;
}

const char* verdict_token(NnVerdict verdict) {
    switch (verdict) {
        case NnVerdict::accepted_unambiguous:
            return "accepted-unambiguous";
        case NnVerdict::accepted_ambiguous:
            return "accepted-ambiguous";
        case NnVerdict::failed:
            return "failed";
        case NnVerdict::blocked_unverified:
            return "blocked-unverified";
        case NnVerdict::not_applicable:
            return "not-applicable";
    }
    return "blocked-unverified";
}

const char* invocation_token(InvocationStatus status) {
    switch (status) {
        case InvocationStatus::completed:
            return "completed";
        case InvocationStatus::contract_invalid:
            return "contract-invalid";
        case InvocationStatus::api_misuse:
            return "api-misuse";
        case InvocationStatus::resource_failure:
            return "resource-failure";
        case InvocationStatus::backend_failure:
            return "backend-failure";
        case InvocationStatus::not_executed:
            return "not-executed";
    }
    return "not-executed";
}

const char* input_outcome_token(InputOutcome outcome) {
    switch (outcome) {
        case InputOutcome::valid:
            return "valid";
        case InputOutcome::invalid_nonfinite:
            return "invalid-nonfinite";
        case InputOutcome::invalid_N_zero:
            return "invalid-N-zero";
        case InputOutcome::invalid_D_zero:
            return "invalid-D-zero";
        case InputOutcome::not_applicable:
            return "not-applicable";
    }
    return "not-applicable";
}

const char* domain_status_token(DomainStatus status) {
    switch (status) {
        case DomainStatus::core_eligible:
            return "core-eligible";
        case DomainStatus::valid_outside_approved_domain:
            return "valid-outside-approved-domain";
        case DomainStatus::unknown:
            return "unknown";
        case DomainStatus::not_applicable:
            return "not-applicable";
    }
    return "unknown";
}

const char* provenance_token(EvidenceProvenance provenance) {
    switch (provenance) {
        case EvidenceProvenance::internally_observed:
            return "internally-observed";
        case EvidenceProvenance::verified_build_artifact:
            return "verified-build-artifact";
        case EvidenceProvenance::production_instrumentation:
            return "production-instrumentation";
        case EvidenceProvenance::external_attestation:
            return "external-attestation";
        case EvidenceProvenance::unavailable:
            return "unavailable";
    }
    return "unavailable";
}

const char* contender_provenance_token(ContenderProvenance provenance) {
    switch (provenance) {
        case ContenderProvenance::exact_resolved:
            return "exact";
        case ContenderProvenance::conservative_outward:
            return "conservative-outward";
        case ContenderProvenance::not_applicable:
            return "not-applicable";
    }
    return "not-applicable";
}

std::uint64_t parse_u64(const std::string& value, const std::string& field_name) {
    if (value.empty() ||
        !std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isdigit(character) != 0;
        })) {
        throw std::runtime_error("invalid unsigned integer field: " + field_name);
    }
    try {
        return std::stoull(value);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid unsigned integer field: " + field_name);
    }
}

std::vector<std::uint64_t> parse_indices(const std::string& value,
                                         const std::string& field_name,
                                         std::uint64_t candidate_count) {
    std::vector<std::uint64_t> result;
    if (value.empty()) {
        return result;
    }
    std::istringstream values(value);
    std::string token;
    while (std::getline(values, token, ',')) {
        const auto index = parse_u64(token, field_name);
        if (index >= candidate_count ||
            (!result.empty() && index <= result.back())) {
            throw std::runtime_error("invalid candidate index list: " + field_name);
        }
        result.push_back(index);
    }
    return result;
}

bool is_sha256(const std::string& value) {
    return value.size() == 64U &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isdigit(character) != 0 ||
                      (character >= 'a' && character <= 'f');
           });
}

const std::vector<std::string>& matrix_required_fields() {
    static const std::vector<std::string> fields = {
        "schema", "case_id", "revision", "working_tree_state",
        "working_tree_fingerprint", "source_manifest_sha256", "source_identity",
        "build_identity", "compiler_id", "compiler_version", "build_configuration",
        "effective_flags", "implementation_variant", "workload_kind", "generator_id",
        "seed", "N", "D", "dataset_sha256", "query_sha256", "workload_sha256",
        "input_contract_outcome", "domain_status", "production_invocation",
        "production_index", "production_distance_bits", "reference_index",
        "evidence_identity_status", "graph_status", "graph_provenance",
        "production_float32_graph", "validator_float32_graph",
        "reference_float64_graph", "validator_float64_graph", "float32_environment",
        "float64_environment", "arithmetic_status", "candidate_coverage_status",
        "candidate_coverage_provenance", "production_candidate_coverage_status",
        "production_candidate_coverage_provenance", "production_match_status",
        "reference_match_status", "float32_lexicographic_status",
        "float64_index_agrees", "ranking_provenance", "graph_policy",
        "environment_policy", "bound_policy", "ranking_policy", "verdict_policy",
        "gamma32_lower_bits", "gamma32_upper_bits", "gamma64_lower_bits",
        "gamma64_upper_bits", "e32_lower_bits", "e32_upper_bits", "e64_lower_bits",
        "e64_upper_bits", "additive_lower_bits", "additive_upper_bits",
        "relative_lower_bits", "relative_upper_bits", "ud001_status", "ud002_status",
        "contender_provenance", "boundary_status", "exact_contenders",
        "conservative_contenders", "boundary_uncertain_indices",
        "exact_membership_resolved", "exact_ambiguity", "fixture_status",
        "oracle_status", "tie_status", "reduction_status", "fixture_ids",
        "decision_trace", "nn_verdict", "candidate_encoding", "candidate_record_size",
        "candidate_count", "candidate_bytes", "candidate_sha256", "candidate_path"};
    return fields;
}

std::string join_indices(const std::vector<std::uint64_t>& values) {
    std::ostringstream output;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            output << ',';
        }
        output << values[i];
    }
    return output.str();
}

std::uint64_t double_bits(double value) noexcept {
    std::uint64_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

double double_from_bits(std::uint64_t value) noexcept {
    double result = 0.0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

float float_from_bits(std::uint32_t value) noexcept {
    float result = 0.0F;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

#pragma pack(push, 1)
struct CandidateReportHeader {
    char magic[8];
    std::uint32_t version;
    std::uint32_t record_size;
    std::uint64_t expected_count;
};

struct CandidateReportRecord {
    std::uint64_t index;
    std::uint32_t float32_bits;
    std::uint64_t float64_bits;
    std::uint64_t observed_error_lower_bits;
    std::uint64_t observed_error_upper_bits;
    std::uint64_t allowed_bound_lower_bits;
    std::uint64_t allowed_bound_upper_bits;
    std::uint64_t radius_lower_bits;
    std::uint64_t radius_upper_bits;
    std::uint64_t lower_endpoint_lower_bits;
    std::uint64_t lower_endpoint_upper_bits;
    std::uint64_t upper_endpoint_lower_bits;
    std::uint64_t upper_endpoint_upper_bits;
    std::uint64_t mathematical_numerator;
    std::int32_t mathematical_exponent;
    std::uint8_t ud001_status;
    std::uint8_t exact_zero_required;
    std::uint8_t exact_zero_satisfied;
    std::uint8_t confirmed_contender;
    std::uint8_t confirmed_non_contender;
    std::uint8_t boundary_uncertain;
    std::uint8_t conservative_member;
    std::uint8_t exact_member;
    std::uint8_t float32_tie;
    std::uint8_t float64_tie;
    std::uint8_t mathematical_tie;
    std::uint8_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(CandidateReportHeader) == 24, "candidate report header ABI");
static_assert(sizeof(CandidateReportRecord) == 124, "candidate report record ABI");

std::uint8_t status_byte(EvidenceStatus status) {
    return status == EvidenceStatus::passed
               ? 1U
               : (status == EvidenceStatus::failed ? 2U : 0U);
}

std::string hash_raw_floats(const float* data, std::size_t count) {
    Sha256 hash;
    if (data != nullptr && count != 0) {
        hash.update(data, count * sizeof(float));
    }
    return hash.finish();
}

}  // namespace

const std::vector<std::string>& cpu_matrix_required_metadata_fields() {
    return matrix_required_fields();
}

bool write_cpu_fixture_evidence(const std::string& path,
                                const CpuBuildEvidence& build_evidence,
                                const MandatoryGateEvidence& gates,
                                std::string& detail) {
    try {
        if (build_evidence.identity_status() != EvidenceStatus::passed ||
            build_evidence.graph_status() != EvidenceStatus::passed ||
            gates.source_identity != build_evidence.source_identity()) {
            detail = "fixture evidence is not bound to a verified build";
            return false;
        }
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            detail = "cannot create fixture evidence";
            return false;
        }
        output << "schema=" << kCpuFixtureEvidenceSchema << '\n'
               << "source_identity=" << gates.source_identity << '\n'
               << "build_identity=" << build_evidence.identity() << '\n'
               << "fixture_status=" << evidence_status_token(gates.fixture_status) << '\n'
               << "oracle_status=" << evidence_status_token(gates.oracle_status) << '\n'
               << "tie_status=" << evidence_status_token(gates.tie_status) << '\n'
               << "reduction_status=" << evidence_status_token(gates.reduction_status) << '\n'
               << "fixture_ids=" << gates.fixture_ids << '\n';
        output.close();
        if (!output) {
            detail = "fixture evidence write failed";
            return false;
        }
        detail = "fixture evidence written";
        return true;
    } catch (const std::exception& error) {
        detail = error.what();
        return false;
    }
}

MandatoryGateEvidence load_cpu_fixture_evidence(
    const std::string& path, const std::string& expected_source_identity,
    std::string& detail) {
    MandatoryGateEvidence result;
    try {
        const auto fields = parse_manifest(read_text(path));
        if (require_field(fields, "schema") != kCpuFixtureEvidenceSchema) {
            throw std::runtime_error("unsupported fixture evidence schema");
        }
        result.source_identity = require_field(fields, "source_identity");
        result.fixture_ids = require_field(fields, "fixture_ids");
        if (result.source_identity != expected_source_identity) {
            result.fixture_status = EvidenceStatus::failed;
            result.oracle_status = EvidenceStatus::failed;
            result.tie_status = EvidenceStatus::failed;
            result.reduction_status = EvidenceStatus::failed;
            detail = "fixture source identity mismatch";
            return result;
        }
        result.fixture_status =
            parse_evidence_status(require_field(fields, "fixture_status"));
        result.oracle_status =
            parse_evidence_status(require_field(fields, "oracle_status"));
        result.tie_status = parse_evidence_status(require_field(fields, "tie_status"));
        result.reduction_status =
            parse_evidence_status(require_field(fields, "reduction_status"));
        detail = "fixture evidence loaded";
    } catch (const std::exception& error) {
        detail = error.what();
    }
    return result;
}

MatrixReportSummary write_cpu_matrix_case_report(
    const std::string& report_root, const std::string& case_id,
    std::uint64_t seed, DatasetView dataset, QueryView query,
    const CpuBuildEvidence& build_evidence,
    const EvidencedProductionOutcome& evidenced,
    const Float64Reference& reference, const ValidationReport& report,
    const MandatoryGateEvidence& gates,
    const std::vector<std::uint64_t>& mathematical_distance_numerators,
    std::int32_t mathematical_distance_exponent) {
    MatrixReportSummary summary;
    summary.candidate_count = dataset.candidate_count;
    try {
        const auto count = static_cast<std::size_t>(dataset.candidate_count);
        if (report.independently_computed_float32_distances.size() != count ||
            report.independently_computed_float64_distances.size() != count ||
            report.bound_evidence.size() != count ||
            report.ranking.candidates.size() != count ||
            mathematical_distance_numerators.size() != count) {
            throw std::runtime_error("matrix report candidate evidence is incomplete");
        }
        const auto root = std::filesystem::path(report_root);
        std::filesystem::create_directories(root);
        const auto candidate_path = root / (case_id + ".candidates.bin");
        const auto metadata_path = root / (case_id + ".metadata.txt");
        summary.candidate_path = candidate_path.string();
        summary.metadata_path = metadata_path.string();

        float minimum_float32 = std::numeric_limits<float>::infinity();
        double minimum_float64 = std::numeric_limits<double>::infinity();
        std::uint64_t minimum_mathematical = std::numeric_limits<std::uint64_t>::max();
        for (std::size_t i = 0; i < count; ++i) {
            minimum_float32 =
                std::min(minimum_float32,
                         report.independently_computed_float32_distances[i]);
            minimum_float64 =
                std::min(minimum_float64,
                         report.independently_computed_float64_distances[i]);
            minimum_mathematical =
                std::min(minimum_mathematical, mathematical_distance_numerators[i]);
        }

        CandidateReportHeader header{{'S', '6', 'C', 'A', 'N', 'D', '0', '1'}, 1U,
                                     static_cast<std::uint32_t>(
                                         sizeof(CandidateReportRecord)),
                                     dataset.candidate_count};
        std::ofstream candidates(candidate_path, std::ios::binary | std::ios::trunc);
        if (!candidates) {
            throw std::runtime_error("cannot create candidate evidence report");
        }
        Sha256 candidate_hash;
        candidates.write(reinterpret_cast<const char*>(&header), sizeof(header));
        candidate_hash.update(&header, sizeof(header));
        const auto in_values = [](const std::vector<std::uint64_t>& values,
                                  std::uint64_t index) {
            return std::find(values.begin(), values.end(), index) != values.end();
        };
        for (std::size_t i = 0; i < count; ++i) {
            const auto index = static_cast<std::uint64_t>(i);
            const auto& bound = report.bound_evidence[i];
            const auto& ranking = report.ranking.candidates[i];
            CandidateReportRecord record{};
            record.index = index;
            record.float32_bits = float_bits(
                report.independently_computed_float32_distances[i]);
            record.float64_bits = double_bits(
                report.independently_computed_float64_distances[i]);
            record.observed_error_lower_bits = double_bits(bound.observed_error.lower);
            record.observed_error_upper_bits = double_bits(bound.observed_error.upper);
            record.allowed_bound_lower_bits = double_bits(bound.allowed_bound.lower);
            record.allowed_bound_upper_bits = double_bits(bound.allowed_bound.upper);
            record.radius_lower_bits = double_bits(ranking.radius.lower);
            record.radius_upper_bits = double_bits(ranking.radius.upper);
            record.lower_endpoint_lower_bits = double_bits(ranking.lower_endpoint.lower);
            record.lower_endpoint_upper_bits = double_bits(ranking.lower_endpoint.upper);
            record.upper_endpoint_lower_bits = double_bits(ranking.upper_endpoint.lower);
            record.upper_endpoint_upper_bits = double_bits(ranking.upper_endpoint.upper);
            record.mathematical_numerator = mathematical_distance_numerators[i];
            record.mathematical_exponent = mathematical_distance_exponent;
            record.ud001_status = status_byte(bound.status);
            record.exact_zero_required = bound.exact_zero_required ? 1U : 0U;
            record.exact_zero_satisfied = bound.exact_zero_satisfied ? 1U : 0U;
            record.confirmed_contender = ranking.confirmed_contender ? 1U : 0U;
            record.confirmed_non_contender = ranking.confirmed_non_contender ? 1U : 0U;
            record.boundary_uncertain = ranking.boundary_uncertain ? 1U : 0U;
            record.conservative_member =
                in_values(report.ranking.conservative_contenders, index) ? 1U : 0U;
            record.exact_member =
                in_values(report.ranking.exact_contenders, index) ? 1U : 0U;
            record.float32_tie =
                report.independently_computed_float32_distances[i] == minimum_float32
                    ? 1U
                    : 0U;
            record.float64_tie =
                report.independently_computed_float64_distances[i] == minimum_float64
                    ? 1U
                    : 0U;
            record.mathematical_tie =
                mathematical_distance_numerators[i] == minimum_mathematical ? 1U : 0U;
            candidates.write(reinterpret_cast<const char*>(&record), sizeof(record));
            candidate_hash.update(&record, sizeof(record));
        }
        candidates.close();
        if (!candidates) {
            throw std::runtime_error("candidate evidence report write failed");
        }
        summary.candidate_sha256 = candidate_hash.finish();
        summary.candidate_bytes = sizeof(header) +
                                  dataset.candidate_count * sizeof(CandidateReportRecord);

        std::ofstream metadata(metadata_path, std::ios::binary | std::ios::trunc);
        if (!metadata) {
            throw std::runtime_error("cannot create matrix metadata report");
        }
        const auto environment = inspect_arithmetic_environment();
        const auto exact_contenders = report.ranking.exact_membership_resolved
                                          ? join_indices(report.ranking.exact_contenders)
                                          : std::string("unknown");
        metadata << "schema=" << kCpuMatrixEvidenceSchema << '\n'
                 << "case_id=" << case_id << '\n'
                 << "revision=" << build_evidence.revision() << '\n'
                 << "working_tree_state=" << build_evidence.working_tree_state() << '\n'
                 << "working_tree_fingerprint="
                 << build_evidence.working_tree_fingerprint() << '\n'
                 << "source_manifest_sha256="
                 << build_evidence.source_manifest_sha256() << '\n'
                 << "source_identity=" << build_evidence.source_identity() << '\n'
                 << "build_identity=" << build_evidence.identity() << '\n'
                 << "compiler_id=" << build_evidence.compiler_id() << '\n'
                 << "compiler_version=" << build_evidence.compiler_version() << '\n'
                 << "build_configuration=" << build_evidence.build_configuration() << '\n'
                 << "effective_flags=" << build_evidence.required_compile_flags() << '\n'
                 << "implementation_variant=cpu-float32-baseline\n"
                 << "workload_kind=single-query-primary\n"
                 << "generator_id=s6-cpu-matrix-dyadic-v1\n"
                 << "seed=" << seed << '\n'
                 << "N=" << dataset.candidate_count << '\n'
                 << "D=" << dataset.dimension << '\n'
                 << "dataset_sha256="
                 << hash_raw_floats(dataset.data, dataset.element_count) << '\n'
                 << "query_sha256=" << hash_raw_floats(query.data, query.element_count)
                 << '\n'
                 << "workload_sha256=" << evidenced.evidence.workload_identity() << '\n'
                 << "input_contract_outcome="
                 << input_outcome_token(report.input_outcome) << '\n'
                 << "domain_status=" << domain_status_token(report.domain_status) << '\n'
                 << "production_invocation="
                 << invocation_token(evidenced.production.invocation_status) << '\n'
                 << "production_index="
                 << (evidenced.production.result
                         ? std::to_string(evidenced.production.result->index)
                         : "not-applicable")
                 << '\n'
                 << "production_distance_bits="
                 << (evidenced.production.result
                         ? std::to_string(float_bits(
                               evidenced.production.result->squared_distance))
                         : "not-applicable")
                 << '\n'
                 << "reference_index="
                 << (reference.argmin_index ? std::to_string(*reference.argmin_index)
                                            : "not-applicable")
                 << '\n'
                 << "evidence_identity_status="
                 << evidence_status_token(report.evidence_identity_status) << '\n'
                 << "graph_status=" << evidence_status_token(report.graph_status) << '\n'
                 << "graph_provenance=" << provenance_token(report.graph_provenance)
                 << '\n'
                 << "production_float32_graph="
                 << evidence_status_token(report.numerical_paths.production_float32_graph)
                 << '\n'
                 << "validator_float32_graph="
                 << evidence_status_token(report.numerical_paths.validator_float32_graph)
                 << '\n'
                 << "reference_float64_graph="
                 << evidence_status_token(report.numerical_paths.reference_float64_graph)
                 << '\n'
                 << "validator_float64_graph="
                 << evidence_status_token(report.numerical_paths.validator_float64_graph)
                 << '\n'
                 << "float32_environment="
                 << (environment.conforming_float32() ? "passed" : "failed") << '\n'
                 << "float64_environment="
                 << (environment.conforming_float64() ? "passed" : "failed") << '\n'
                 << "arithmetic_status="
                 << evidence_status_token(report.arithmetic_environment_status) << '\n'
                 << "candidate_coverage_status="
                 << evidence_status_token(report.candidate_coverage_status) << '\n'
                 << "candidate_coverage_provenance="
                 << provenance_token(report.candidate_coverage_provenance) << '\n'
                 << "production_candidate_coverage_status="
                 << evidence_status_token(report.production_candidate_coverage_status)
                 << '\n'
                 << "production_candidate_coverage_provenance="
                 << provenance_token(report.production_candidate_coverage_provenance)
                 << '\n'
                 << "production_match_status="
                 << evidence_status_token(report.production_match_status) << '\n'
                 << "reference_match_status="
                 << evidence_status_token(report.reference_match_status) << '\n'
                 << "float32_lexicographic_status="
                 << evidence_status_token(report.production_match_status) << '\n'
                 << "float64_index_agrees="
                 << (report.float64_index_agrees ? "true" : "false") << '\n'
                 << "ranking_provenance=" << provenance_token(report.ranking_provenance)
                 << '\n'
                 << "graph_policy=" << kGraphPolicy << '\n'
                 << "environment_policy=" << kEnvironmentPolicy << '\n'
                 << "bound_policy=" << kBoundPolicy << '\n'
                 << "ranking_policy=" << kRankingPolicy << '\n'
                 << "verdict_policy=" << kVerdictPolicy << '\n'
                 << "gamma32_lower_bits="
                 << double_bits(report.numerical_policy.gamma32.lower) << '\n'
                 << "gamma32_upper_bits="
                 << double_bits(report.numerical_policy.gamma32.upper) << '\n'
                 << "gamma64_lower_bits="
                 << double_bits(report.numerical_policy.gamma64.lower) << '\n'
                 << "gamma64_upper_bits="
                 << double_bits(report.numerical_policy.gamma64.upper) << '\n'
                 << "e32_lower_bits="
                 << double_bits(report.numerical_policy.e32.lower) << '\n'
                 << "e32_upper_bits="
                 << double_bits(report.numerical_policy.e32.upper) << '\n'
                 << "e64_lower_bits="
                 << double_bits(report.numerical_policy.e64.lower) << '\n'
                 << "e64_upper_bits="
                 << double_bits(report.numerical_policy.e64.upper) << '\n'
                 << "additive_lower_bits="
                 << double_bits(report.numerical_policy.additive.lower) << '\n'
                 << "additive_upper_bits="
                 << double_bits(report.numerical_policy.additive.upper) << '\n'
                 << "relative_lower_bits="
                 << double_bits(report.numerical_policy.relative.lower) << '\n'
                 << "relative_upper_bits="
                 << double_bits(report.numerical_policy.relative.upper) << '\n'
                 << "ud001_status=" << evidence_status_token(report.ud001_status) << '\n'
                 << "ud002_status=" << evidence_status_token(report.ud002_status) << '\n'
                 << "contender_provenance="
                 << contender_provenance_token(report.contender_provenance) << '\n'
                 << "boundary_status="
                 << (report.ranking.exact_membership_resolved ? "resolved"
                                                             : "boundary-uncertain")
                 << '\n'
                 << "exact_contenders=" << exact_contenders << '\n'
                 << "conservative_contenders="
                 << join_indices(report.ranking.conservative_contenders) << '\n'
                 << "boundary_uncertain_indices="
                 << join_indices(report.ranking.boundary_uncertain_indices) << '\n'
                 << "exact_membership_resolved="
                 << (report.ranking.exact_membership_resolved ? "true" : "false") << '\n'
                 << "exact_ambiguity="
                 << (report.exact_ambiguity_known
                         ? (report.exact_ambiguity ? "true" : "false")
                         : "unknown")
                 << '\n'
                 << "fixture_status=" << evidence_status_token(gates.fixture_status) << '\n'
                 << "oracle_status=" << evidence_status_token(gates.oracle_status) << '\n'
                 << "tie_status=" << evidence_status_token(gates.tie_status) << '\n'
                 << "reduction_status=" << evidence_status_token(gates.reduction_status)
                 << '\n'
                 << "fixture_ids=" << gates.fixture_ids << '\n'
                 << "decision_trace=" << report.decision_trace << '\n'
                 << "nn_verdict=" << verdict_token(report.nn_verdict) << '\n'
                 << "candidate_encoding=little-endian-packed-v1\n"
                 << "candidate_record_size=" << sizeof(CandidateReportRecord) << '\n'
                 << "candidate_count=" << dataset.candidate_count << '\n'
                 << "candidate_bytes=" << summary.candidate_bytes << '\n'
                 << "candidate_sha256=" << summary.candidate_sha256 << '\n'
                 << "candidate_path="
                 << std::filesystem::weakly_canonical(candidate_path).string() << '\n';
        metadata.close();
        if (!metadata) {
            throw std::runtime_error("matrix metadata report write failed");
        }
        MatrixReportExpectation expectation;
        expectation.case_id = case_id;
        expectation.seed = seed;
        expectation.dataset = dataset;
        expectation.query = query;
        expectation.build_evidence = &build_evidence;
        expectation.evidenced = &evidenced;
        expectation.reference = &reference;
        expectation.validation_report = &report;
        expectation.mandatory_gate_evidence = &gates;
        expectation.mathematical_distance_numerators =
            &mathematical_distance_numerators;
        expectation.mathematical_distance_exponent =
            mathematical_distance_exponent;
        summary = verify_cpu_matrix_case_report(summary.metadata_path, expectation);
    } catch (const std::exception& error) {
        summary.status = EvidenceStatus::blocked;
        summary.detail = error.what();
    }
    return summary;
}

namespace {

bool same_interval_bits(const Interval& interval, std::uint64_t lower_bits,
                        std::uint64_t upper_bits) noexcept {
    return double_bits(interval.lower) == lower_bits &&
           double_bits(interval.upper) == upper_bits;
}

MatrixReportSummary verify_cpu_matrix_case_report_impl(
    const std::string& metadata_path,
    const MatrixReportExpectation* expectation) {
    MatrixReportSummary summary;
    summary.metadata_path = metadata_path;
    try {
        const auto fields = parse_manifest(read_text(metadata_path));
        for (const auto& name : matrix_required_fields()) {
            require_present_field(fields, name);
        }
        if (require_field(fields, "schema") != kCpuMatrixEvidenceSchema) {
            throw std::runtime_error("unsupported matrix evidence schema");
        }
        for (const auto& name : matrix_required_fields()) {
            if (name != "boundary_uncertain_indices" &&
                require_present_field(fields, name).empty()) {
                throw std::runtime_error("matrix metadata field is empty: " + name);
            }
        }
        if (require_field(fields, "working_tree_state") != "clean" ||
            require_field(fields, "build_configuration") != "Release" ||
            require_field(fields, "implementation_variant") !=
                "cpu-float32-baseline" ||
            require_field(fields, "workload_kind") != "single-query-primary" ||
            require_field(fields, "generator_id") != "s6-cpu-matrix-dyadic-v1") {
            throw std::runtime_error("matrix build/workload identity is not acceptance eligible");
        }
        for (const auto* hash_field : {"working_tree_fingerprint",
                                       "source_manifest_sha256", "dataset_sha256",
                                       "query_sha256", "workload_sha256",
                                       "candidate_sha256"}) {
            if (!is_sha256(require_field(fields, hash_field))) {
                throw std::runtime_error(std::string("invalid SHA-256 field: ") +
                                         hash_field);
            }
        }
        if (require_field(fields, "graph_policy") != kGraphPolicy ||
            require_field(fields, "environment_policy") != kEnvironmentPolicy ||
            require_field(fields, "bound_policy") != kBoundPolicy ||
            require_field(fields, "ranking_policy") != kRankingPolicy ||
            require_field(fields, "verdict_policy") != kVerdictPolicy) {
            throw std::runtime_error("matrix numerical policy identity mismatch");
        }
        const std::array<std::pair<const char*, const char*>, 6> policy_intervals = {{
            {"gamma32_lower_bits", "gamma32_upper_bits"},
            {"gamma64_lower_bits", "gamma64_upper_bits"},
            {"e32_lower_bits", "e32_upper_bits"},
            {"e64_lower_bits", "e64_upper_bits"},
            {"additive_lower_bits", "additive_upper_bits"},
            {"relative_lower_bits", "relative_upper_bits"},
        }};
        for (const auto& names : policy_intervals) {
            const auto lower = double_from_bits(
                std::stoull(require_field(fields, names.first)));
            const auto upper = double_from_bits(
                std::stoull(require_field(fields, names.second)));
            if (!std::isfinite(lower) || !std::isfinite(upper) || lower > upper) {
                throw std::runtime_error("matrix numerical policy interval is invalid");
            }
        }
        const auto verdict = require_field(fields, "nn_verdict");
        const bool accepted = verdict == "accepted-unambiguous" ||
                              verdict == "accepted-ambiguous";
        if (!accepted) {
            throw std::runtime_error("matrix report is not an accepted correctness result");
        }
        if (require_field(fields, "input_contract_outcome") != "valid" ||
            require_field(fields, "domain_status") != "core-eligible" ||
            require_field(fields, "production_invocation") != "completed" ||
            require_field(fields, "evidence_identity_status") != "passed" ||
            require_field(fields, "graph_status") != "passed" ||
            require_field(fields, "graph_provenance") != "verified-build-artifact" ||
            require_field(fields, "production_float32_graph") != "passed" ||
             require_field(fields, "validator_float32_graph") != "passed" ||
             require_field(fields, "reference_float64_graph") != "passed" ||
             require_field(fields, "validator_float64_graph") != "passed" ||
             require_field(fields, "float32_environment") != "passed" ||
             require_field(fields, "float64_environment") != "passed" ||
            require_field(fields, "arithmetic_status") != "passed" ||
            require_field(fields, "candidate_coverage_status") != "passed" ||
            require_field(fields, "candidate_coverage_provenance") !=
                "internally-observed" ||
            require_field(fields, "production_candidate_coverage_status") != "passed" ||
            require_field(fields, "production_candidate_coverage_provenance") !=
                "production-instrumentation" ||
            require_field(fields, "production_match_status") != "passed" ||
            require_field(fields, "reference_match_status") != "passed" ||
            require_field(fields, "float32_lexicographic_status") != "passed" ||
            require_field(fields, "float64_index_agrees") != "true" ||
            require_field(fields, "ranking_provenance") != "internally-observed" ||
            require_field(fields, "ud001_status") != "passed" ||
            require_field(fields, "ud002_status") != "passed" ||
            require_field(fields, "fixture_status") != "passed" ||
            require_field(fields, "oracle_status") != "passed" ||
            require_field(fields, "tie_status") != "passed" ||
            require_field(fields, "reduction_status") != "passed") {
            throw std::runtime_error("accepted matrix report has unsatisfied mandatory gate");
        }
        const auto candidate_count = parse_u64(require_field(fields, "N"), "N");
        const auto dimension = parse_u64(require_field(fields, "D"), "D");
        if (candidate_count == 0 || dimension == 0 ||
            dimension > kMaximumPolicyDimension) {
            throw std::runtime_error("matrix report workload is outside the approved domain");
        }
        summary.candidate_count = parse_u64(
            require_field(fields, "candidate_count"), "candidate_count");
        summary.candidate_bytes = parse_u64(
            require_field(fields, "candidate_bytes"), "candidate_bytes");
        summary.candidate_sha256 = require_field(fields, "candidate_sha256");
        summary.candidate_path = require_field(fields, "candidate_path");
        if (summary.candidate_count != candidate_count ||
            parse_u64(require_field(fields, "candidate_record_size"),
                      "candidate_record_size") !=
                sizeof(CandidateReportRecord) ||
            summary.candidate_count >
                (std::numeric_limits<std::uint64_t>::max() -
                 sizeof(CandidateReportHeader)) /
                    sizeof(CandidateReportRecord) ||
            summary.candidate_bytes !=
                sizeof(CandidateReportHeader) +
                    summary.candidate_count * sizeof(CandidateReportRecord)) {
            throw std::runtime_error("matrix candidate report cardinality mismatch");
        }
        const auto metadata = std::filesystem::weakly_canonical(metadata_path);
        const auto expected_metadata = std::filesystem::weakly_canonical(
            metadata.parent_path() / (require_field(fields, "case_id") +
                                      ".metadata.txt"));
        const auto expected_candidate = std::filesystem::weakly_canonical(
            metadata.parent_path() / (require_field(fields, "case_id") +
                                      ".candidates.bin"));
        if (metadata != expected_metadata ||
            std::filesystem::weakly_canonical(summary.candidate_path) !=
                expected_candidate) {
            throw std::runtime_error("matrix report path/case identity mismatch");
        }
        if (!std::filesystem::exists(summary.candidate_path) ||
            std::filesystem::file_size(summary.candidate_path) !=
                summary.candidate_bytes ||
            sha256_file(summary.candidate_path) != summary.candidate_sha256) {
            throw std::runtime_error("matrix candidate report size/hash mismatch");
        }
        std::ifstream input(summary.candidate_path, std::ios::binary);
        CandidateReportHeader header{};
        input.read(reinterpret_cast<char*>(&header), sizeof(header));
        const std::array<char, 8> expected_magic = {'S', '6', 'C', 'A',
                                                     'N', 'D', '0', '1'};
        if (!input || !std::equal(std::begin(header.magic), std::end(header.magic),
                                  expected_magic.begin()) ||
            header.version != 1U ||
            header.record_size != sizeof(CandidateReportRecord) ||
            header.expected_count != summary.candidate_count) {
            throw std::runtime_error("matrix candidate report header mismatch");
        }
        std::vector<CandidateReportRecord> records;
        records.reserve(static_cast<std::size_t>(summary.candidate_count));
        std::vector<double> distances64;
        distances64.reserve(static_cast<std::size_t>(summary.candidate_count));
        const auto valid_flag = [](std::uint8_t value) { return value <= 1U; };
        const auto valid_interval = [](std::uint64_t lower_bits,
                                       std::uint64_t upper_bits) {
            const auto lower = double_from_bits(lower_bits);
            const auto upper = double_from_bits(upper_bits);
            return std::isfinite(lower) && std::isfinite(upper) && lower <= upper;
        };
        for (std::uint64_t index = 0; index < summary.candidate_count; ++index) {
            CandidateReportRecord record{};
            input.read(reinterpret_cast<char*>(&record), sizeof(record));
            if (!input || record.index != index) {
                throw std::runtime_error(
                    "matrix candidate report missing or duplicate index");
            }
            if (!std::isfinite(float_from_bits(record.float32_bits)) ||
                !std::isfinite(double_from_bits(record.float64_bits)) ||
                !valid_interval(record.observed_error_lower_bits,
                                record.observed_error_upper_bits) ||
                !valid_interval(record.allowed_bound_lower_bits,
                                record.allowed_bound_upper_bits) ||
                !valid_interval(record.radius_lower_bits, record.radius_upper_bits) ||
                !valid_interval(record.lower_endpoint_lower_bits,
                                record.lower_endpoint_upper_bits) ||
                !valid_interval(record.upper_endpoint_lower_bits,
                                record.upper_endpoint_upper_bits)) {
                throw std::runtime_error("matrix candidate numerical interval is invalid");
            }
            const std::array<std::uint8_t, 11> flags = {
                record.exact_zero_required, record.exact_zero_satisfied,
                record.confirmed_contender, record.confirmed_non_contender,
                record.boundary_uncertain, record.conservative_member,
                record.exact_member, record.float32_tie, record.float64_tie,
                record.mathematical_tie, record.reserved};
            if (record.ud001_status > 2U ||
                !std::all_of(flags.begin(), flags.end(), valid_flag) ||
                record.reserved != 0U ||
                (record.confirmed_contender != 0U &&
                 record.confirmed_non_contender != 0U) ||
                record.boundary_uncertain !=
                    static_cast<std::uint8_t>(record.confirmed_contender == 0U &&
                                              record.confirmed_non_contender == 0U) ||
                (record.boundary_uncertain != 0U && record.exact_member != 0U)) {
                throw std::runtime_error("matrix candidate status trace is invalid");
            }
            if (accepted && record.ud001_status != 1U) {
                throw std::runtime_error("accepted matrix candidate failed UD-001");
            }
            const auto recomputed_bound = evaluate_ud001(
                dimension, float_from_bits(record.float32_bits),
                double_from_bits(record.float64_bits),
                record.exact_zero_required != 0U);
            if (status_byte(recomputed_bound.status) != record.ud001_status ||
                recomputed_bound.exact_zero_satisfied !=
                    (record.exact_zero_satisfied != 0U) ||
                !same_interval_bits(recomputed_bound.observed_error,
                                    record.observed_error_lower_bits,
                                    record.observed_error_upper_bits) ||
                !same_interval_bits(recomputed_bound.allowed_bound,
                                    record.allowed_bound_lower_bits,
                                    record.allowed_bound_upper_bits)) {
                throw std::runtime_error("matrix candidate UD-001 trace is inconsistent");
            }
            records.push_back(record);
            distances64.push_back(double_from_bits(record.float64_bits));
        }
        char extra = 0;
        if (input.read(&extra, 1)) {
            throw std::runtime_error("matrix candidate report has trailing records");
        }
        if (!records.empty()) {
            const auto recomputed_ranking = evaluate_ud002(dimension, distances64);
            if (recomputed_ranking.status != EvidenceStatus::passed ||
                recomputed_ranking.candidates.size() != records.size()) {
                throw std::runtime_error("matrix candidate ranking could not be recomputed");
            }
            const auto minimum_float32 = std::min_element(
                records.begin(), records.end(), [](const auto& lhs, const auto& rhs) {
                    return float_from_bits(lhs.float32_bits) <
                           float_from_bits(rhs.float32_bits);
                });
            const auto minimum_float64 = std::min_element(
                records.begin(), records.end(), [](const auto& lhs, const auto& rhs) {
                    return double_from_bits(lhs.float64_bits) <
                           double_from_bits(rhs.float64_bits);
                });
            const auto minimum_mathematical = std::min_element(
                records.begin(), records.end(), [](const auto& lhs, const auto& rhs) {
                    return lhs.mathematical_numerator < rhs.mathematical_numerator;
                });
            for (std::size_t index = 0; index < records.size(); ++index) {
                const auto& record = records[index];
                const auto& ranking = recomputed_ranking.candidates[index];
                const auto in_values = [index](const std::vector<std::uint64_t>& values) {
                    return std::find(values.begin(), values.end(),
                                     static_cast<std::uint64_t>(index)) != values.end();
                };
                if (!same_interval_bits(ranking.radius, record.radius_lower_bits,
                                        record.radius_upper_bits) ||
                    !same_interval_bits(ranking.lower_endpoint,
                                        record.lower_endpoint_lower_bits,
                                        record.lower_endpoint_upper_bits) ||
                    !same_interval_bits(ranking.upper_endpoint,
                                        record.upper_endpoint_lower_bits,
                                        record.upper_endpoint_upper_bits) ||
                    record.confirmed_contender !=
                        static_cast<std::uint8_t>(ranking.confirmed_contender) ||
                    record.confirmed_non_contender !=
                        static_cast<std::uint8_t>(ranking.confirmed_non_contender) ||
                    record.boundary_uncertain !=
                        static_cast<std::uint8_t>(ranking.boundary_uncertain) ||
                    record.conservative_member != static_cast<std::uint8_t>(
                        in_values(recomputed_ranking.conservative_contenders)) ||
                    record.exact_member != static_cast<std::uint8_t>(
                        in_values(recomputed_ranking.exact_contenders)) ||
                    record.float32_tie != static_cast<std::uint8_t>(
                        float_from_bits(record.float32_bits) ==
                        float_from_bits(minimum_float32->float32_bits)) ||
                    record.float64_tie != static_cast<std::uint8_t>(
                        double_from_bits(record.float64_bits) ==
                        double_from_bits(minimum_float64->float64_bits)) ||
                    record.mathematical_tie != static_cast<std::uint8_t>(
                        record.mathematical_numerator ==
                        minimum_mathematical->mathematical_numerator)) {
                    throw std::runtime_error(
                        "matrix candidate contender or tie trace is inconsistent");
                }
            }

            const bool exact_resolved = recomputed_ranking.exact_membership_resolved;
            const auto exact_field = require_field(fields, "exact_contenders");
            const auto exact_contenders = exact_resolved
                                              ? parse_indices(exact_field, "exact_contenders",
                                                              candidate_count)
                                              : std::vector<std::uint64_t>{};
            const auto conservative_contenders = parse_indices(
                require_field(fields, "conservative_contenders"),
                "conservative_contenders", candidate_count);
            const auto boundary_indices = parse_indices(
                require_present_field(fields, "boundary_uncertain_indices"),
                "boundary_uncertain_indices", candidate_count);
            if ((exact_resolved && exact_field == "unknown") ||
                (!exact_resolved && exact_field != "unknown") ||
                exact_contenders != recomputed_ranking.exact_contenders ||
                conservative_contenders !=
                    recomputed_ranking.conservative_contenders ||
                boundary_indices != recomputed_ranking.boundary_uncertain_indices ||
                require_field(fields, "exact_membership_resolved") !=
                    (exact_resolved ? "true" : "false")) {
                throw std::runtime_error("matrix contender metadata is inconsistent");
            }

            const auto production_index = parse_u64(
                require_field(fields, "production_index"), "production_index");
            const auto reference_index = parse_u64(
                require_field(fields, "reference_index"), "reference_index");
            const auto production_distance_bits = parse_u64(
                require_field(fields, "production_distance_bits"),
                "production_distance_bits");
            const auto minimum_float32_index = static_cast<std::uint64_t>(
                std::distance(records.begin(), minimum_float32));
            const auto minimum_float64_index = static_cast<std::uint64_t>(
                std::distance(records.begin(), minimum_float64));
            if (production_index != minimum_float32_index ||
                reference_index != minimum_float64_index ||
                production_distance_bits > std::numeric_limits<std::uint32_t>::max() ||
                static_cast<std::uint32_t>(production_distance_bits) !=
                    records[static_cast<std::size_t>(production_index)].float32_bits) {
                throw std::runtime_error("matrix production/reference result is inconsistent");
            }

            const auto provenance = require_field(fields, "contender_provenance");
            const auto boundary_status = require_field(fields, "boundary_status");
            const auto ambiguity = require_field(fields, "exact_ambiguity");
            const auto trace = require_field(fields, "decision_trace");
            const auto contains = [production_index](
                                      const std::vector<std::uint64_t>& values) {
                return std::find(values.begin(), values.end(), production_index) !=
                       values.end();
            };
            if (exact_resolved) {
                const bool ambiguous = exact_contenders.size() > 1U;
                if (provenance != "exact" || boundary_status != "resolved" ||
                    !boundary_indices.empty() ||
                    ambiguity != (ambiguous ? "true" : "false") ||
                    !contains(exact_contenders) ||
                    (ambiguous &&
                     (verdict != "accepted-ambiguous" ||
                      trace != "precedence 8: exact multi-candidate contender set")) ||
                    (!ambiguous &&
                     (exact_contenders.size() != 1U ||
                      verdict != "accepted-unambiguous" ||
                      reference_index != production_index ||
                      trace != "precedence 7: exact singleton and Float64 agreement"))) {
                    throw std::runtime_error("matrix exact contender verdict is inconsistent");
                }
            } else if (provenance != "conservative-outward" ||
                       boundary_status != "boundary-uncertain" ||
                       ambiguity != "unknown" || boundary_indices.empty() ||
                       !contains(conservative_contenders) ||
                       verdict != "accepted-ambiguous" ||
                       trace !=
                           "precedence 9: conservative outward set; exact ambiguity unknown") {
                throw std::runtime_error(
                    "matrix conservative contender verdict is inconsistent");
            }

            if (expectation != nullptr) {
                if (expectation->build_evidence == nullptr ||
                    expectation->evidenced == nullptr ||
                    expectation->reference == nullptr ||
                    expectation->validation_report == nullptr ||
                    expectation->mandatory_gate_evidence == nullptr ||
                    expectation->mathematical_distance_numerators == nullptr) {
                    throw std::runtime_error("matrix verification expectation is incomplete");
                }
                const auto& build = *expectation->build_evidence;
                const auto& evidenced = *expectation->evidenced;
                const auto& reference = *expectation->reference;
                const auto& report = *expectation->validation_report;
                const auto& gates = *expectation->mandatory_gate_evidence;
                const auto& mathematical =
                    *expectation->mathematical_distance_numerators;
                if (build.identity_status() != EvidenceStatus::passed ||
                    build.working_tree_state() != "clean" ||
                    build.build_configuration() != "Release" ||
                    expectation->dataset.candidate_count != candidate_count ||
                    expectation->dataset.dimension != dimension ||
                    expectation->query.dimension != dimension ||
                    expectation->dataset.data == nullptr ||
                    expectation->query.data == nullptr ||
                    dimension > std::numeric_limits<std::size_t>::max() /
                                    candidate_count ||
                    expectation->dataset.element_count !=
                        static_cast<std::size_t>(candidate_count * dimension) ||
                    expectation->query.element_count !=
                        static_cast<std::size_t>(dimension) ||
                    mathematical.size() != records.size() ||
                    report.independently_observed_candidate_count != candidate_count ||
                    report.independently_computed_float32_distances.size() !=
                        records.size() ||
                    report.independently_computed_float64_distances.size() !=
                        records.size() ||
                    report.bound_evidence.size() != records.size() ||
                    report.ranking.candidates.size() != records.size() ||
                    reference.distances.size() != records.size() ||
                    reference.invocation_status != InvocationStatus::completed ||
                    reference.input_outcome != InputOutcome::valid ||
                    reference.domain_status != DomainStatus::core_eligible ||
                    gates.source_identity != build.source_identity() ||
                    evidenced.evidence.source_identity() != build.source_identity() ||
                    !evidenced.evidence.matches(expectation->dataset,
                                                 expectation->query,
                                                 evidenced.production)) {
                    throw std::runtime_error(
                        "matrix report does not match expected build/workload identity");
                }
                const auto environment = inspect_arithmetic_environment();
                const auto expected_exact_contenders =
                    report.ranking.exact_membership_resolved
                        ? join_indices(report.ranking.exact_contenders)
                        : std::string("unknown");
                const std::map<std::string, std::string> expected_fields = {
                    {"case_id", expectation->case_id},
                    {"revision", build.revision()},
                    {"working_tree_state", build.working_tree_state()},
                    {"working_tree_fingerprint", build.working_tree_fingerprint()},
                    {"source_manifest_sha256", build.source_manifest_sha256()},
                    {"source_identity", build.source_identity()},
                    {"build_identity", build.identity()},
                    {"compiler_id", build.compiler_id()},
                    {"compiler_version", build.compiler_version()},
                    {"build_configuration", build.build_configuration()},
                    {"effective_flags", build.required_compile_flags()},
                    {"implementation_variant", "cpu-float32-baseline"},
                    {"workload_kind", "single-query-primary"},
                    {"generator_id", "s6-cpu-matrix-dyadic-v1"},
                    {"seed", std::to_string(expectation->seed)},
                    {"N", std::to_string(expectation->dataset.candidate_count)},
                    {"D", std::to_string(expectation->dataset.dimension)},
                    {"dataset_sha256",
                     hash_raw_floats(expectation->dataset.data,
                                     expectation->dataset.element_count)},
                    {"query_sha256",
                     hash_raw_floats(expectation->query.data,
                                     expectation->query.element_count)},
                    {"workload_sha256", evidenced.evidence.workload_identity()},
                    {"input_contract_outcome", input_outcome_token(report.input_outcome)},
                    {"domain_status", domain_status_token(report.domain_status)},
                    {"production_invocation",
                     invocation_token(evidenced.production.invocation_status)},
                    {"production_index",
                     evidenced.production.result
                         ? std::to_string(evidenced.production.result->index)
                         : "not-applicable"},
                    {"production_distance_bits",
                     evidenced.production.result
                         ? std::to_string(float_bits(
                               evidenced.production.result->squared_distance))
                         : "not-applicable"},
                    {"reference_index",
                     reference.argmin_index ? std::to_string(*reference.argmin_index)
                                            : "not-applicable"},
                    {"evidence_identity_status",
                     evidence_status_token(report.evidence_identity_status)},
                    {"graph_status", evidence_status_token(report.graph_status)},
                    {"graph_provenance", provenance_token(report.graph_provenance)},
                    {"production_float32_graph",
                     evidence_status_token(report.numerical_paths.production_float32_graph)},
                    {"validator_float32_graph",
                     evidence_status_token(report.numerical_paths.validator_float32_graph)},
                    {"reference_float64_graph",
                     evidence_status_token(report.numerical_paths.reference_float64_graph)},
                    {"validator_float64_graph",
                     evidence_status_token(report.numerical_paths.validator_float64_graph)},
                    {"float32_environment",
                     environment.conforming_float32() ? "passed" : "failed"},
                    {"float64_environment",
                     environment.conforming_float64() ? "passed" : "failed"},
                    {"arithmetic_status",
                     evidence_status_token(report.arithmetic_environment_status)},
                    {"candidate_coverage_status",
                     evidence_status_token(report.candidate_coverage_status)},
                    {"candidate_coverage_provenance",
                     provenance_token(report.candidate_coverage_provenance)},
                    {"production_candidate_coverage_status",
                     evidence_status_token(report.production_candidate_coverage_status)},
                    {"production_candidate_coverage_provenance",
                     provenance_token(report.production_candidate_coverage_provenance)},
                    {"production_match_status",
                     evidence_status_token(report.production_match_status)},
                    {"reference_match_status",
                     evidence_status_token(report.reference_match_status)},
                    {"float32_lexicographic_status",
                     evidence_status_token(report.production_match_status)},
                    {"float64_index_agrees",
                     report.float64_index_agrees ? "true" : "false"},
                    {"ranking_provenance", provenance_token(report.ranking_provenance)},
                    {"graph_policy", kGraphPolicy},
                    {"environment_policy", kEnvironmentPolicy},
                    {"bound_policy", kBoundPolicy},
                    {"ranking_policy", kRankingPolicy},
                    {"verdict_policy", kVerdictPolicy},
                    {"gamma32_lower_bits",
                     std::to_string(double_bits(report.numerical_policy.gamma32.lower))},
                    {"gamma32_upper_bits",
                     std::to_string(double_bits(report.numerical_policy.gamma32.upper))},
                    {"gamma64_lower_bits",
                     std::to_string(double_bits(report.numerical_policy.gamma64.lower))},
                    {"gamma64_upper_bits",
                     std::to_string(double_bits(report.numerical_policy.gamma64.upper))},
                    {"e32_lower_bits",
                     std::to_string(double_bits(report.numerical_policy.e32.lower))},
                    {"e32_upper_bits",
                     std::to_string(double_bits(report.numerical_policy.e32.upper))},
                    {"e64_lower_bits",
                     std::to_string(double_bits(report.numerical_policy.e64.lower))},
                    {"e64_upper_bits",
                     std::to_string(double_bits(report.numerical_policy.e64.upper))},
                    {"additive_lower_bits",
                     std::to_string(double_bits(report.numerical_policy.additive.lower))},
                    {"additive_upper_bits",
                     std::to_string(double_bits(report.numerical_policy.additive.upper))},
                    {"relative_lower_bits",
                     std::to_string(double_bits(report.numerical_policy.relative.lower))},
                    {"relative_upper_bits",
                     std::to_string(double_bits(report.numerical_policy.relative.upper))},
                    {"ud001_status", evidence_status_token(report.ud001_status)},
                    {"ud002_status", evidence_status_token(report.ud002_status)},
                    {"contender_provenance",
                     contender_provenance_token(report.contender_provenance)},
                    {"boundary_status",
                     report.ranking.exact_membership_resolved ? "resolved"
                                                             : "boundary-uncertain"},
                    {"exact_contenders", expected_exact_contenders},
                    {"conservative_contenders",
                     join_indices(report.ranking.conservative_contenders)},
                    {"boundary_uncertain_indices",
                     join_indices(report.ranking.boundary_uncertain_indices)},
                    {"exact_membership_resolved",
                     report.ranking.exact_membership_resolved ? "true" : "false"},
                    {"exact_ambiguity",
                     report.exact_ambiguity_known
                         ? (report.exact_ambiguity ? "true" : "false")
                         : "unknown"},
                    {"fixture_status", evidence_status_token(gates.fixture_status)},
                    {"oracle_status", evidence_status_token(gates.oracle_status)},
                    {"tie_status", evidence_status_token(gates.tie_status)},
                    {"reduction_status", evidence_status_token(gates.reduction_status)},
                    {"fixture_ids", gates.fixture_ids},
                    {"decision_trace", report.decision_trace},
                    {"nn_verdict", verdict_token(report.nn_verdict)},
                    {"candidate_encoding", "little-endian-packed-v1"},
                    {"candidate_record_size",
                     std::to_string(sizeof(CandidateReportRecord))},
                    {"candidate_count",
                     std::to_string(expectation->dataset.candidate_count)},
                    {"candidate_bytes", std::to_string(summary.candidate_bytes)},
                    {"candidate_sha256", summary.candidate_sha256},
                    {"candidate_path",
                     std::filesystem::weakly_canonical(summary.candidate_path).string()}};
                for (const auto& expected_field : expected_fields) {
                    if (require_present_field(fields, expected_field.first) !=
                        expected_field.second) {
                        throw std::runtime_error(
                            "matrix report field disagrees with expected evidence: " +
                            expected_field.first);
                    }
                }
                const auto dimension_size = static_cast<std::size_t>(dimension);
                for (std::size_t index = 0; index < records.size(); ++index) {
                    const auto& bound = report.bound_evidence[index];
                    const auto& ranking = report.ranking.candidates[index];
                    const auto in_expected = [index](
                                                 const std::vector<std::uint64_t>& values) {
                        return std::find(values.begin(), values.end(),
                                         static_cast<std::uint64_t>(index)) !=
                               values.end();
                    };
                    bool exact_zero_expected = true;
                    const auto offset = index * dimension_size;
                    for (std::size_t coordinate = 0; coordinate < dimension_size;
                         ++coordinate) {
                        if (expectation->dataset.data[offset + coordinate] !=
                            expectation->query.data[coordinate]) {
                            exact_zero_expected = false;
                            break;
                        }
                    }
                    if (records[index].float32_bits !=
                            float_bits(
                                report.independently_computed_float32_distances[index]) ||
                        records[index].float64_bits !=
                            double_bits(
                                report.independently_computed_float64_distances[index]) ||
                        records[index].float64_bits !=
                            double_bits(reference.distances[index]) ||
                        !same_interval_bits(bound.observed_error,
                                            records[index].observed_error_lower_bits,
                                            records[index].observed_error_upper_bits) ||
                        !same_interval_bits(bound.allowed_bound,
                                            records[index].allowed_bound_lower_bits,
                                            records[index].allowed_bound_upper_bits) ||
                        status_byte(bound.status) != records[index].ud001_status ||
                        records[index].exact_zero_required !=
                            static_cast<std::uint8_t>(exact_zero_expected) ||
                        records[index].exact_zero_required !=
                            static_cast<std::uint8_t>(bound.exact_zero_required) ||
                        records[index].exact_zero_satisfied !=
                            static_cast<std::uint8_t>(bound.exact_zero_satisfied) ||
                        !same_interval_bits(ranking.radius,
                                            records[index].radius_lower_bits,
                                            records[index].radius_upper_bits) ||
                        !same_interval_bits(ranking.lower_endpoint,
                                            records[index].lower_endpoint_lower_bits,
                                            records[index].lower_endpoint_upper_bits) ||
                        !same_interval_bits(ranking.upper_endpoint,
                                            records[index].upper_endpoint_lower_bits,
                                            records[index].upper_endpoint_upper_bits) ||
                        records[index].confirmed_contender !=
                            static_cast<std::uint8_t>(ranking.confirmed_contender) ||
                        records[index].confirmed_non_contender !=
                            static_cast<std::uint8_t>(ranking.confirmed_non_contender) ||
                        records[index].boundary_uncertain !=
                            static_cast<std::uint8_t>(ranking.boundary_uncertain) ||
                        records[index].conservative_member !=
                            static_cast<std::uint8_t>(in_expected(
                                report.ranking.conservative_contenders)) ||
                        records[index].exact_member !=
                            static_cast<std::uint8_t>(
                                in_expected(report.ranking.exact_contenders)) ||
                        records[index].mathematical_numerator != mathematical[index] ||
                        records[index].mathematical_exponent !=
                            expectation->mathematical_distance_exponent) {
                        throw std::runtime_error(
                            "matrix candidate report does not match expected workload");
                    }
                }
            }
        }
        if (expectation == nullptr) {
            summary.status = EvidenceStatus::blocked;
            summary.detail =
                "report is internally valid but expected build/workload identity is unavailable";
        } else {
            summary.status = EvidenceStatus::passed;
            summary.detail = "complete build/workload-bound candidate report verified";
        }
    } catch (const std::exception& error) {
        summary.status = EvidenceStatus::failed;
        summary.detail = error.what();
    }
    return summary;
}

}  // namespace

MatrixReportSummary verify_cpu_matrix_case_report(
    const std::string& metadata_path) {
    return verify_cpu_matrix_case_report_impl(metadata_path, nullptr);
}

MatrixReportSummary verify_cpu_matrix_case_report(
    const std::string& metadata_path,
    const MatrixReportExpectation& expectation) {
    return verify_cpu_matrix_case_report_impl(metadata_path, &expectation);
}

}  // namespace s6
