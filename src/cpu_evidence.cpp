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
        fields.emplace(line.substr(0, equals), line.substr(equals + 1));
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

std::string object_function_body(const std::string& disassembly,
                                 const std::string& symbol_prefix) {
    const auto heading = disassembly.find("\n" + symbol_prefix);
    if (heading == std::string::npos) {
        return {};
    }
    const auto next = disassembly.find("\n\n?", heading + 2);
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
            compare.operands != "rbx,rdi" || back_edge.mnemonic != "jb" ||
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

GraphInspectionAssessment inspect_graph(const std::string& object_disassembly,
                                        const std::string& executable_disassembly,
                                         const std::string& linker_map,
                                         std::uint64_t expected_production_address = 0,
                                         std::uint64_t expected_caller_address = 0,
                                         std::uint64_t expected_fmaf_iat = 0) {
    GraphInspectionAssessment assessment;
    const auto object_body = object_function_body(object_disassembly,
                                                  "?production_distance@");
    if (object_body.empty()) {
        assessment.status = EvidenceStatus::failed;
        assessment.detail = "production_distance symbol body missing from object";
        return assessment;
    }
    if (!validate_distance_loop(parse_instructions(object_body), true, 0,
                                assessment.detail)) {
        assessment.status = EvidenceStatus::failed;
        return assessment;
    }

    const auto address = address_from_map(linker_map, "?production_distance@?A0",
                                          "s6_cpu_baseline:cpu_baseline.cpp.obj");
    if (address.empty()) {
        assessment.status = EvidenceStatus::failed;
        assessment.detail = "production_distance address missing from linker map";
        return assessment;
    }
    const auto production_address = std::stoull(address, nullptr, 16);
    if (expected_production_address != 0 && production_address != expected_production_address) {
        assessment.status = EvidenceStatus::failed;
        std::ostringstream detail;
        detail << "linker map production address disagrees with running function: map=0x"
               << std::hex << production_address << " runtime=0x"
               << expected_production_address;
        assessment.detail = detail.str();
        return assessment;
    }
    const auto executable_position = executable_disassembly.find(address + ":");
    if (executable_position == std::string::npos) {
        assessment.status = EvidenceStatus::failed;
        assessment.detail = "mapped production function missing from executable disassembly";
        return assessment;
    }
    const auto return_position = executable_disassembly.find("ret", executable_position);
    const auto executable_body = executable_disassembly.substr(
        executable_position,
        return_position == std::string::npos ? 1800
                                             : return_position - executable_position + 3);
    if (!validate_distance_loop(parse_instructions(executable_body), false,
                                expected_fmaf_iat, assessment.detail)) {
        assessment.status = EvidenceStatus::failed;
        return assessment;
    }

    const auto caller_object_body = object_function_body(
        object_disassembly, "?cpu_search_float32_impl@detail@s6@@");
    if (caller_object_body.empty() ||
        !validate_candidate_loop(parse_instructions(caller_object_body), true,
                                 production_address, assessment.detail)) {
        assessment.status = EvidenceStatus::failed;
        return assessment;
    }

    const auto caller_address = address_from_map(
        linker_map, "?cpu_search_float32_impl@detail@s6@@",
        "s6_cpu_baseline:cpu_baseline.cpp.obj");
    const auto caller_position = executable_disassembly.find(caller_address + ":");
    if (caller_address.empty() || caller_position == std::string::npos) {
        assessment.status = EvidenceStatus::failed;
        assessment.detail = "mapped production candidate loop missing";
        return assessment;
    }
    const auto caller_numeric_address = std::stoull(caller_address, nullptr, 16);
    if (expected_caller_address != 0 && caller_numeric_address != expected_caller_address) {
        assessment.status = EvidenceStatus::failed;
        std::ostringstream detail;
        detail << "linker map caller address disagrees with running function: map=0x"
               << std::hex << caller_numeric_address << " runtime=0x"
               << expected_caller_address;
        assessment.detail = detail.str();
        return assessment;
    }
    const auto caller_return = executable_disassembly.find("ret", caller_position);
    const auto caller_executable_body = executable_disassembly.substr(
        caller_position,
        caller_return == std::string::npos ? 8000
                                           : caller_return - caller_position + 3);
    if (!validate_candidate_loop(parse_instructions(caller_executable_body), false,
                                 production_address, assessment.detail)) {
        assessment.status = EvidenceStatus::failed;
        return assessment;
    }

    assessment.status = EvidenceStatus::passed;
    assessment.detail =
        "trusted object and running mapped executable preserve exact candidate and subss/fmaf loops";
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

std::uint64_t preferred_fmaf_iat_address() {
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
            if (std::strcmp(reinterpret_cast<const char*>(imported->Name), "fmaf") == 0) {
                return executable_preferred_image_base() + descriptor->FirstThunk +
                       index * sizeof(IMAGE_THUNK_DATA64);
            }
        }
    }
    throw std::runtime_error("running executable fmaf import missing");
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
        const std::vector<std::pair<std::string, std::string>> identities = {
            {"revision", build_identity::kRevision},
            {"working_tree_state", build_identity::kWorkingTreeState},
            {"compiler_id", build_identity::kCompilerId},
            {"compiler_version", build_identity::kCompilerVersion},
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
            symbols.find("| __imp_fmaf") == std::string::npos) {
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
        const auto production_address =
            preferred_address(detail::production_distance_function_address());
        const auto caller_address =
            preferred_address(detail::cpu_search_float32_impl_function_address());
        const auto graph = inspect_graph(object_disassembly, executable_disassembly,
                                         linker_map, production_address, caller_address,
                                         preferred_fmaf_iat_address());

        result.identity_status_ = EvidenceStatus::passed;
        result.graph_status_ = graph.status;
        result.graph_defect_demonstrated_ = graph.status == EvidenceStatus::failed;
        result.identity_ = require_field(fields, "revision") + "|" +
                           require_field(fields, "working_tree_state") + "|" +
                           require_field(fields, "target") + "|exe=" +
                           require_field(fields, "executable_sha256") + "|obj=" +
                           require_field(fields, "object_sha256") + "|compiler=" +
                           require_field(fields, "compiler_version");
        result.detail_ = graph.detail;
    } catch (const EvidenceUnavailable& error) {
        result.identity_status_ = EvidenceStatus::blocked;
        result.graph_status_ = EvidenceStatus::blocked;
        result.graph_defect_demonstrated_ = false;
        result.detail_ = error.what();
    } catch (const std::exception& error) {
        result.identity_status_ = EvidenceStatus::failed;
        result.graph_status_ = EvidenceStatus::blocked;
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
    result.evidence.graph_defect_demonstrated_ =
        build_evidence.graph_defect_demonstrated();
    result.evidence.identity_ = build_evidence.identity();
    result.evidence.detail_ = build_evidence.detail();
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

}  // namespace s6
