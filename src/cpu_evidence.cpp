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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace s6 {
namespace {

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

const std::string& require_field(const std::map<std::string, std::string>& fields,
                                 const std::string& name) {
    const auto found = fields.find(name);
    if (found == fields.end() || found->second.empty()) {
        throw std::runtime_error("manifest field missing: " + name);
    }
    return found->second;
}

bool ordered_substrings(std::string_view text,
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

std::string address_from_map(const std::string& linker_map,
                             const std::string& symbol_fragment) {
    std::istringstream lines(linker_map);
    std::string line;
    const std::regex absolute_address("(^|[[:space:]])([0-9A-Fa-f]{16})([[:space:]]|$)");
    while (std::getline(lines, line)) {
        if (line.find(symbol_fragment) == std::string::npos) {
            continue;
        }
        std::smatch match;
        if (std::regex_search(line, match, absolute_address)) {
            std::string address = match[2].str();
            std::transform(address.begin(), address.end(), address.begin(),
                           [](unsigned char value) {
                               return static_cast<char>(std::toupper(value));
                           });
            return address;
        }
    }
    return {};
}

GraphInspectionAssessment inspect_graph(const std::string& object_disassembly,
                                        const std::string& executable_disassembly,
                                        const std::string& linker_map) {
    GraphInspectionAssessment assessment;
    const auto object_body = object_function_body(object_disassembly,
                                                  "?production_distance@");
    if (object_body.empty()) {
        assessment.status = EvidenceStatus::failed;
        assessment.detail = "production_distance symbol body missing from object";
        return assessment;
    }
    const std::vector<std::string_view> object_chain = {
        "xorps       xmm2,xmm2", "movss       xmm0,dword ptr [rbp+rbx*4]",
        "subss       xmm0,dword ptr [rsi+rbx*4]",
        "movss       dword ptr [rsp+60h],xmm0",
        "movss       xmm1,dword ptr [rsp+60h]",
        "movss       xmm0,dword ptr [rsp+60h]",
        "call        qword ptr [__imp_fmaf]", "inc         rbx",
        "movaps      xmm2,xmm0", "cmp         rbx,rdi", "jb          "};
    if (!ordered_substrings(object_body, object_chain) ||
        object_body.find("mulss") != std::string::npos ||
        object_body.find("addss") != std::string::npos) {
        assessment.status = EvidenceStatus::failed;
        assessment.detail = "object graph/control/data dependency mismatch";
        return assessment;
    }

    const auto address = address_from_map(linker_map, "?production_distance@?A0");
    if (address.empty()) {
        assessment.status = EvidenceStatus::failed;
        assessment.detail = "production_distance address missing from linker map";
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
    const std::vector<std::string_view> executable_chain = {
        "xorps       xmm2,xmm2", "movss       xmm0,dword ptr [rbp+rbx*4]",
        "subss       xmm0,dword ptr [rsi+rbx*4]",
        "movss       dword ptr [rsp+60h],xmm0",
        "movss       xmm1,dword ptr [rsp+60h]",
        "movss       xmm0,dword ptr [rsp+60h]", "call        qword ptr [",
        "inc         rbx", "movaps      xmm2,xmm0", "cmp         rbx,rdi",
        "jb          "};
    if (!ordered_substrings(executable_body, executable_chain) ||
        executable_body.find("mulss") != std::string::npos ||
        executable_body.find("addss") != std::string::npos) {
        assessment.status = EvidenceStatus::failed;
        assessment.detail = "executable graph/control/data dependency mismatch";
        return assessment;
    }

    const auto caller_object_body = object_function_body(
        object_disassembly, "?cpu_search_float32_impl@detail@s6@@");
    const std::vector<std::string_view> caller_object_chain = {
        "cmp         rsi,qword ptr [r13+8]", "call        qword ptr [rax+8]",
        "imul        rcx,rdx", "call        ?production_distance@",
        "inc         rsi", "jmp         00000000000001C0"};
    if (caller_object_body.empty() ||
        !ordered_substrings(caller_object_body, caller_object_chain)) {
        assessment.status = EvidenceStatus::failed;
        assessment.detail = "production candidate-loop call/coverage chain mismatch";
        return assessment;
    }

    const auto caller_address = address_from_map(
        linker_map, "?cpu_search_float32_impl@detail@s6@@");
    const auto caller_position = executable_disassembly.find(caller_address + ":");
    if (caller_address.empty() || caller_position == std::string::npos) {
        assessment.status = EvidenceStatus::failed;
        assessment.detail = "mapped production candidate loop missing";
        return assessment;
    }
    const auto caller_return = executable_disassembly.find("ret", caller_position);
    const auto caller_executable_body = executable_disassembly.substr(
        caller_position,
        caller_return == std::string::npos ? 8000
                                           : caller_return - caller_position + 3);
    const std::vector<std::string_view> caller_executable_chain = {
        "cmp         rsi,qword ptr [r13+8]", "call        qword ptr [rax+8]",
        "imul        rcx,rdx", "call        ", "inc         rsi", "jmp         "};
    if (!ordered_substrings(caller_executable_body, caller_executable_chain)) {
        assessment.status = EvidenceStatus::failed;
        assessment.detail = "executable candidate-loop call/coverage chain mismatch";
        return assessment;
    }

    assessment.status = EvidenceStatus::passed;
    assessment.detail =
        "source object and mapped executable preserve candidate loop and subss/fmaf chain";
    return assessment;
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
        const auto fields = parse_manifest(read_text(manifest_path));
        if (require_field(fields, "schema") != kCpuEvidenceSchema) {
            throw std::runtime_error("unsupported evidence schema");
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

        const std::vector<std::pair<std::string, std::string>> artifacts = {
            {"compiler_path", "compiler_sha256"},
            {"policy_path", "policy_sha256"},
            {"baseline_header_path", "baseline_header_sha256"},
            {"evidence_header_path", "evidence_header_sha256"},
            {"baseline_source_path", "baseline_source_sha256"},
            {"evidence_source_path", "evidence_source_sha256"},
            {"cmake_path", "cmake_sha256"},
            {"compile_commands_path", "compile_commands_sha256"},
            {"object_path", "object_sha256"},
            {"library_path", "library_sha256"},
            {"executable_path", "executable_sha256"},
            {"map_path", "map_sha256"},
            {"object_disassembly_path", "object_disassembly_sha256"},
            {"executable_disassembly_path", "executable_disassembly_sha256"},
            {"symbols_path", "symbols_sha256"}};
        for (const auto& artifact : artifacts) {
            const auto& path = require_field(fields, artifact.first);
            if (sha256_file(path) != require_field(fields, artifact.second)) {
                throw std::runtime_error("artifact hash mismatch: " + artifact.first);
            }
        }
        const auto manifest_executable = std::filesystem::weakly_canonical(
            std::filesystem::path(require_field(fields, "executable_path")));
        if (manifest_executable != current_executable_path()) {
            throw std::runtime_error("manifest executable is not the consuming process");
        }

        const auto source = read_text(require_field(fields, "baseline_source_path"));
        if (!ordered_substrings(source,
                                {"float accumulator = 0.0F;",
                                 "for (std::size_t j = 0; j < dimension; ++j)",
                                 "candidate[j] - query[j]",
                                 "std::fma(rounded_difference, rounded_difference, accumulator)"})) {
            throw std::runtime_error("production source graph mismatch");
        }
        const auto compile_commands =
            read_text(require_field(fields, "compile_commands_path"));
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

        const auto object_disassembly =
            read_text(require_field(fields, "object_disassembly_path"));
        const auto executable_disassembly =
            read_text(require_field(fields, "executable_disassembly_path"));
        const auto linker_map = read_text(require_field(fields, "map_path"));
        const auto graph = inspect_graph(object_disassembly, executable_disassembly,
                                         linker_map);

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
