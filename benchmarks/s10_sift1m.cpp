#include "s10_sift1m_loader.hpp"

#include "s6/benchmark_internal.hpp"
#include "s6/cpu_build_identity.hpp"
#include "s6/cpu_evidence.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef S10_BENCHMARK_REVISION
#define S10_BENCHMARK_REVISION "unknown"
#endif
#ifndef S10_BENCHMARK_TREE_STATE
#define S10_BENCHMARK_TREE_STATE "unknown"
#endif

namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr std::array<std::uint64_t, 4> kCandidateCounts = {
    1024U, 16384U, 262144U, 1000000U};
constexpr std::uint64_t kDimension = 128U;
constexpr std::uint32_t kBlockSize = 256U;
constexpr unsigned kWarmups = 10U;
constexpr unsigned kRepetitions = 100U;

struct Options {
    std::string experiment_id;
    fs::path config;
    std::string config_sha256;
    std::string expected_revision;
    fs::path dataset_root;
    std::string base_sha256;
    std::string query_sha256;
    std::string captured_at;
    std::string timezone;
    std::string cuda_architecture;
    std::string build_flags;
    std::string benchmark_binary_sha256;
    std::string artifact_manifest_sha256;
    std::string numerical_build_identity_sha256;
    fs::path correctness_reference;
    fs::path cpu_manifest;
    fs::path fixture_evidence;
    std::array<fs::path, 4> cuda_binary;
    std::array<fs::path, 4> cuda_functional;
};

struct Gate {
    std::string status;
    std::string revision;
    std::string tree_state;
    std::string source_manifest_sha256;
    std::string config_sha256;
    std::string base_sha256;
    std::string query_sha256;
    std::string cuda_architecture;
    std::string build_flags;
    std::string correctness_ledger_sha256;
    std::string benchmark_binary_sha256;
    std::string artifact_manifest_sha256;
    std::string numerical_build_identity_sha256;
    std::uint64_t required_cases = 0U;
    std::uint64_t accepted_cases = 0U;
    std::uint64_t failed_cases = 0U;
    std::uint64_t blocked_cases = 0U;
    std::uint64_t v2_block_size = 0U;
    std::uint64_t v3_block_size = 0U;
    bool performance_authorized = false;
};

struct Sample {
    std::string variant;
    std::string boundary;
    std::string timer;
    std::uint64_t n = 0U;
    std::uint64_t elapsed_ns = 0U;
    std::uint32_t block_size = 0U;
    std::uint32_t run_order = 0U;
    std::uint32_t iteration = 0U;
};

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::string json_escape(std::string_view text) {
    std::string result;
    for (const char character : text) {
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += character; break;
        }
    }
    return result;
}

std::string read_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot open required evidence file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void require_token(const std::string& text, const std::string& token,
                   std::string_view label) {
    require(text.find(token) != std::string::npos,
            std::string(label) + " is missing required token: " + token);
}

enum class JsonKind { string, number, boolean };
struct JsonValue { JsonKind kind; std::string text; };
using JsonObject = std::map<std::string, JsonValue>;

class StrictJsonParser {
  public:
    explicit StrictJsonParser(std::string_view input) : input_(input) {}
    JsonObject parse() {
        JsonObject object;
        space(); take('{'); space();
        if (peek('}')) { take('}'); finish(); return object; }
        for (;;) {
            const auto key = string();
            space(); take(':'); space();
            JsonValue value;
            if (peek('"')) value = {JsonKind::string, string()};
            else if (starts("true")) { position_ += 4U; value = {JsonKind::boolean, "true"}; }
            else if (starts("false")) { position_ += 5U; value = {JsonKind::boolean, "false"}; }
            else value = {JsonKind::number, number()};
            require(object.emplace(key, std::move(value)).second, "duplicate JSON key");
            space();
            if (peek('}')) { take('}'); finish(); return object; }
            take(','); space();
        }
    }
  private:
    void space() { while (position_ < input_.size() &&
        std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_; }
    bool peek(char value) const { return position_ < input_.size() && input_[position_] == value; }
    bool starts(std::string_view value) const { return input_.substr(position_, value.size()) == value; }
    void take(char value) { require(peek(value), "malformed JSON"); ++position_; }
    std::string string() {
        take('"'); std::string result;
        while (position_ < input_.size()) {
            const unsigned char value = static_cast<unsigned char>(input_[position_++]);
            if (value == '"') return result;
            require(value >= 0x20U && value != '\\', "unsupported JSON string escape/control");
            result.push_back(static_cast<char>(value));
        }
        throw std::runtime_error("unterminated JSON string");
    }
    std::string number() {
        const auto start = position_;
        require(position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_])),
                "JSON unsigned integer is malformed");
        if (input_[position_] == '0') ++position_;
        else while (position_ < input_.size() &&
                    std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        require(position_ == input_.size() || input_[position_] == ',' || input_[position_] == '}' ||
                    std::isspace(static_cast<unsigned char>(input_[position_])),
                "JSON number is not an unsigned integer");
        return std::string(input_.substr(start, position_ - start));
    }
    void finish() { space(); require(position_ == input_.size(), "trailing JSON data"); }
    std::string_view input_; std::size_t position_ = 0U;
};

const JsonValue& field(const JsonObject& object, std::string_view key, JsonKind kind) {
    const auto found = object.find(std::string(key));
    require(found != object.end() && found->second.kind == kind, "missing or mistyped JSON field");
    return found->second;
}
std::string json_string(const JsonObject& object, std::string_view key) {
    return field(object, key, JsonKind::string).text;
}
std::uint64_t json_u64(const JsonObject& object, std::string_view key) {
    const auto& value = field(object, key, JsonKind::number).text;
    std::size_t consumed = 0U; const auto result = std::stoull(value, &consumed);
    require(consumed == value.size(), "invalid JSON unsigned integer"); return result;
}
bool json_bool(const JsonObject& object, std::string_view key) {
    return field(object, key, JsonKind::boolean).text == "true";
}
void require_schema(const JsonObject& object, const std::set<std::string>& expected) {
    require(object.size() == expected.size(), "JSON schema field count mismatch");
    for (const auto& key : expected) require(object.count(key) == 1U, "JSON schema field mismatch");
}

Gate read_gate(const fs::path& path) {
    const auto object = StrictJsonParser(read_text(path)).parse();
    require_schema(object, {"schema_version","status","revision","working_tree_state",
        "source_manifest_sha256","experiment_config_sha256","sift_base_sha256",
        "sift_query_sha256","cuda_architecture","build_flags","correctness_ledger_sha256",
        "benchmark_binary_sha256","artifact_manifest_sha256","numerical_build_identity_sha256",
        "required_cases","accepted_cases","failed_cases","blocked_cases","v2_block_size",
        "v3_block_size","performance_authorized"});
    require(json_string(object, "schema_version") == "S10-CORRECTNESS-GATE-v2",
            "unsupported S10 correctness gate schema");
    return {json_string(object,"status"),json_string(object,"revision"),
        json_string(object,"working_tree_state"),json_string(object,"source_manifest_sha256"),
        json_string(object,"experiment_config_sha256"),json_string(object,"sift_base_sha256"),
        json_string(object,"sift_query_sha256"),json_string(object,"cuda_architecture"),
        json_string(object,"build_flags"),json_string(object,"correctness_ledger_sha256"),
        json_string(object,"benchmark_binary_sha256"),json_string(object,"artifact_manifest_sha256"),
        json_string(object,"numerical_build_identity_sha256"),json_u64(object,"required_cases"),
        json_u64(object,"accepted_cases"),json_u64(object,"failed_cases"),
        json_u64(object,"blocked_cases"),json_u64(object,"v2_block_size"),
        json_u64(object,"v3_block_size"),json_bool(object,"performance_authorized")};
}

bool performance_gate_qualifies(const Gate& gate, const Options& options,
                                const s10::LoadedSift1m& dataset,
                                std::string_view benchmark_revision,
                                std::string_view benchmark_tree_state,
                                std::string_view source_manifest_sha256) {
    return gate.status == "passed-clean-candidate" &&
           gate.revision == benchmark_revision &&
           gate.revision == options.expected_revision &&
           gate.tree_state == "clean" && benchmark_tree_state == "clean" &&
           gate.source_manifest_sha256 == source_manifest_sha256 &&
           gate.config_sha256 == options.config_sha256 &&
           gate.base_sha256 == dataset.base_sha256 &&
           gate.query_sha256 == dataset.query_sha256 &&
           gate.base_sha256 == options.base_sha256 &&
           gate.query_sha256 == options.query_sha256 &&
           gate.cuda_architecture == options.cuda_architecture &&
           gate.build_flags == options.build_flags &&
           gate.benchmark_binary_sha256 == options.benchmark_binary_sha256 &&
           gate.artifact_manifest_sha256 == options.artifact_manifest_sha256 &&
           gate.numerical_build_identity_sha256 == options.numerical_build_identity_sha256 &&
           gate.required_cases == 20U && gate.accepted_cases == 20U &&
           gate.failed_cases == 0U && gate.blocked_cases == 0U &&
           gate.v2_block_size == kBlockSize && gate.v3_block_size == kBlockSize &&
           gate.performance_authorized;
}

void validate_performance_gate(const Gate& gate, const Options& options,
                               const s10::LoadedSift1m& dataset) {
    require(performance_gate_qualifies(gate, options, dataset,
                                       S10_BENCHMARK_REVISION,
                                       S10_BENCHMARK_TREE_STATE,
                                       s6::build_identity::kSourceManifestSha256),
            "S10 correctness gate identity/status does not authorize performance");
}

Options parse_options(int argc, char** argv, int first) {
    Options options;
    const auto take = [&](int& index, std::string_view name) {
        require(index + 1 < argc, std::string(name) + " requires a value");
        return std::string(argv[++index]);
    };
    for (int index = first; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--experiment-id") options.experiment_id = take(index, argument);
        else if (argument == "--experiment-config") options.config = take(index, argument);
        else if (argument == "--config-sha256") options.config_sha256 = take(index, argument);
        else if (argument == "--expected-revision") options.expected_revision = take(index, argument);
        else if (argument == "--dataset-root") options.dataset_root = take(index, argument);
        else if (argument == "--base-sha256") options.base_sha256 = take(index, argument);
        else if (argument == "--query-sha256") options.query_sha256 = take(index, argument);
        else if (argument == "--captured-at") options.captured_at = take(index, argument);
        else if (argument == "--timezone") options.timezone = take(index, argument);
        else if (argument == "--cuda-architecture") options.cuda_architecture = take(index, argument);
        else if (argument == "--build-flags") options.build_flags = take(index, argument);
        else if (argument == "--benchmark-binary-sha256") options.benchmark_binary_sha256 = take(index, argument);
        else if (argument == "--artifact-manifest-sha256") options.artifact_manifest_sha256 = take(index, argument);
        else if (argument == "--numerical-build-identity-sha256") options.numerical_build_identity_sha256 = take(index, argument);
        else if (argument == "--correctness-reference") options.correctness_reference = take(index, argument);
        else if (argument == "--cpu-manifest") options.cpu_manifest = take(index, argument);
        else if (argument == "--fixture-evidence") options.fixture_evidence = take(index, argument);
        else if (argument == "--cuda-v0-binary") options.cuda_binary[0] = take(index, argument);
        else if (argument == "--cuda-v0-functional") options.cuda_functional[0] = take(index, argument);
        else if (argument == "--cuda-v1-binary") options.cuda_binary[1] = take(index, argument);
        else if (argument == "--cuda-v1-functional") options.cuda_functional[1] = take(index, argument);
        else if (argument == "--cuda-v2-binary") options.cuda_binary[2] = take(index, argument);
        else if (argument == "--cuda-v2-functional") options.cuda_functional[2] = take(index, argument);
        else if (argument == "--cuda-v3-binary") options.cuda_binary[3] = take(index, argument);
        else if (argument == "--cuda-v3-functional") options.cuda_functional[3] = take(index, argument);
        else throw std::runtime_error("unknown option: " + argument);
    }
    require(!options.experiment_id.empty() && fs::exists(options.config) &&
                options.config_sha256.size() == 64U,
            "missing experiment/configuration identity");
    require(s10::detail::sha256_file(options.config) == options.config_sha256,
            "actual experiment configuration SHA-256 does not match its identity");
    require(options.expected_revision == S10_BENCHMARK_REVISION,
            "benchmark revision does not match expected revision");
    require(!options.dataset_root.empty() && options.base_sha256.size() == 64U &&
                options.query_sha256.size() == 64U,
            "missing dataset identity");
    require(!options.captured_at.empty() && !options.timezone.empty() &&
                !options.cuda_architecture.empty() && !options.build_flags.empty(),
            "missing environment/build identity");
    require(options.benchmark_binary_sha256.size() == 64U &&
                options.artifact_manifest_sha256.size() == 64U &&
                options.numerical_build_identity_sha256.size() == 64U,
            "missing binary/numerical build identity");
    return options;
}

void verify_cuda_evidence(const Options& options) {
    for (std::size_t variant = 0U; variant < 4U; ++variant) {
        require(fs::exists(options.cuda_binary[variant]) &&
                    fs::exists(options.cuda_functional[variant]),
                "CUDA binary/functional evidence is missing");
        const auto binary = read_text(options.cuda_binary[variant]);
        const auto functional = read_text(options.cuda_functional[variant]);
        for (const auto& identity : {
                 std::string("revision=") + s6::build_identity::kRevision,
                 std::string("working_tree_state=") + s6::build_identity::kWorkingTreeState,
                 std::string("source_manifest_sha256=") +
                     s6::build_identity::kSourceManifestSha256}) {
            require_token(binary, identity, "CUDA binary evidence");
            require_token(functional, identity, "CUDA functional evidence");
        }
        for (const char* token : {"compile_policy_status=passed",
                                  "binary_graph_structure_status=passed"}) {
            require_token(binary, token, "CUDA binary evidence");
        }
        for (const char* token : {"overall=passed", "reduction=passed",
                                  "exact_dyadic_oracle=passed"}) {
            require_token(functional, token, "CUDA functional evidence");
        }
    }
}

const char* verdict_name(s6::NnVerdict verdict) {
    switch (verdict) {
        case s6::NnVerdict::accepted_unambiguous: return "accepted-unambiguous";
        case s6::NnVerdict::accepted_ambiguous: return "accepted-ambiguous";
        case s6::NnVerdict::failed: return "failed";
        case s6::NnVerdict::blocked_unverified: return "blocked-unverified";
        case s6::NnVerdict::not_applicable: return "not-applicable";
    }
    return "blocked-unverified";
}

bool accepted(s6::NnVerdict verdict) {
    return verdict == s6::NnVerdict::accepted_unambiguous ||
           verdict == s6::NnVerdict::accepted_ambiguous;
}

constexpr std::string_view kCandidateDistancePendingV0V1 =
    "mapping and poison-overwrite are production instrumentation; "
    "independent distance comparison remains required";
constexpr std::string_view kCandidateDistancePendingV2V3 =
    "diagnostic per-candidate buffers are independent of production reduction";

struct CudaCandidateEvidenceView {
    s6::InvocationStatus invocation_status = s6::InvocationStatus::not_executed;
    bool result_present = false;
    s6::EvidenceStatus mapping_status = s6::EvidenceStatus::blocked;
    s6::EvidenceStatus overwrite_status = s6::EvidenceStatus::blocked;
    s6::EvidenceStatus candidate_distance_status = s6::EvidenceStatus::blocked;
    std::string_view detail;
    std::uint64_t expected_candidate_count = 0U;
    std::uint64_t observed_mapping_count = 0U;
    std::size_t candidate_distance_count = 0U;
};

bool candidate_distance_verification_pending(const CudaCandidateEvidenceView& evidence) {
    return evidence.candidate_distance_status == s6::EvidenceStatus::blocked &&
           (evidence.detail == kCandidateDistancePendingV0V1 ||
            evidence.detail == kCandidateDistancePendingV2V3);
}

s6::EvidenceStatus production_candidate_evidence_status(
    const CudaCandidateEvidenceView& evidence, std::uint64_t expected_count) {
    if (evidence.invocation_status != s6::InvocationStatus::completed ||
        !evidence.result_present || evidence.mapping_status == s6::EvidenceStatus::blocked ||
        evidence.overwrite_status == s6::EvidenceStatus::blocked) {
        return s6::EvidenceStatus::blocked;
    }
    if (evidence.mapping_status == s6::EvidenceStatus::failed ||
        evidence.overwrite_status == s6::EvidenceStatus::failed ||
        evidence.candidate_distance_status == s6::EvidenceStatus::failed ||
        evidence.expected_candidate_count != expected_count ||
        evidence.observed_mapping_count != expected_count ||
        evidence.candidate_distance_count != static_cast<std::size_t>(expected_count)) {
        return s6::EvidenceStatus::failed;
    }
    return candidate_distance_verification_pending(evidence)
               ? s6::EvidenceStatus::passed
               : s6::EvidenceStatus::blocked;
}

s6::EvidenceStatus independently_validate_candidate_distances(
    const std::vector<float>& actual, const std::vector<float>& expected) {
    if (actual.size() != expected.size()) return s6::EvidenceStatus::failed;
    for (std::size_t index = 0U; index < actual.size(); ++index) {
        if (float_bits(actual[index]) != float_bits(expected[index])) {
            return s6::EvidenceStatus::failed;
        }
    }
    return s6::EvidenceStatus::passed;
}

bool cuda_candidate_case_authorized(s6::EvidenceStatus production_evidence,
                                    s6::EvidenceStatus production_output,
                                    s6::EvidenceStatus independent_distances) {
    return production_evidence == s6::EvidenceStatus::passed &&
           production_output == s6::EvidenceStatus::passed &&
           independent_distances == s6::EvidenceStatus::passed;
}

template <typename Evidenced>
void validate_gpu_case(const Evidenced& gpu, const s6::ValidationReport& cpu_report,
                       std::uint64_t n) {
    const auto production_evidence = production_candidate_evidence_status(
        {gpu.production.invocation_status, static_cast<bool>(gpu.production.result),
         gpu.evidence.mapping_status, gpu.evidence.overwrite_status,
         gpu.evidence.candidate_distance_status, gpu.evidence.detail,
         gpu.evidence.expected_candidate_count, gpu.evidence.observed_mapping_count,
         gpu.evidence.candidate_distances.size()},
        n);
    require(production_evidence == s6::EvidenceStatus::passed,
            "S10 CUDA production candidate evidence is incomplete or unsupported");
    require(cpu_report.independently_computed_float32_argmin_index &&
                cpu_report.independently_computed_float32_distances.size() ==
                    static_cast<std::size_t>(n),
            "S10 independent Float32 evidence is incomplete");
    const auto index = *cpu_report.independently_computed_float32_argmin_index;
    const auto production_output =
        gpu.production.result->index == index &&
                float_bits(gpu.production.result->squared_distance) ==
                    float_bits(cpu_report.independently_computed_float32_distances[
                        static_cast<std::size_t>(index)])
            ? s6::EvidenceStatus::passed
            : s6::EvidenceStatus::failed;
    require(production_output == s6::EvidenceStatus::passed,
            "S10 CUDA production result differs from independent Float32 argmin");
    const auto independent_distances = independently_validate_candidate_distances(
        gpu.evidence.candidate_distances,
        cpu_report.independently_computed_float32_distances);
    require(independent_distances == s6::EvidenceStatus::passed,
            "S10 CUDA candidate differs from independent Float32 graph");
    require(cuda_candidate_case_authorized(
                production_evidence, production_output, independent_distances),
            "S10 CUDA correctness authorization is incomplete");
}

void write_case(std::ofstream& output, std::uint64_t ordinal,
                std::string_view variant, std::uint64_t n, std::uint32_t block_size,
                const s6::ProductionOutcome& production,
                const s6::ValidationReport& report, const Options& options,
                const s10::LoadedSift1m& dataset) {
    output << "{\"schema_version\":\"S10-CORRECTNESS-CASE-v2\","
           << "\"ordinal\":" << ordinal
           << ",\"revision\":\"" << S10_BENCHMARK_REVISION
           << "\",\"working_tree_state\":\"" << S10_BENCHMARK_TREE_STATE
           << "\",\"source_manifest_sha256\":\"" << s6::build_identity::kSourceManifestSha256
           << "\",\"experiment_config_sha256\":\"" << options.config_sha256
           << "\",\"sift_base_sha256\":\"" << dataset.base_sha256
           << "\",\"sift_query_sha256\":\"" << dataset.query_sha256
           << "\",\"cuda_architecture\":\"" << json_escape(options.cuda_architecture)
           << "\",\"build_flags\":\"" << json_escape(options.build_flags)
           << "\",\"benchmark_binary_sha256\":\"" << options.benchmark_binary_sha256
           << "\",\"artifact_manifest_sha256\":\"" << options.artifact_manifest_sha256
           << "\",\"numerical_build_identity_sha256\":\""
           << options.numerical_build_identity_sha256
           << "\",\"variant\":\"" << variant << "\",\"N\":" << n
           << ",\"D\":" << kDimension
           << ",\"query_ordinal\":0,\"block_size\":" << block_size
           << ",\"implementation_index\":"
           << production.result->index
           << ",\"implementation_distance_bits\":"
           << float_bits(production.result->squared_distance)
           << ",\"f32_lexicographic_status\":\"passed\","
           << "\"production_path_status\":\"passed\","
           << "\"graph_status\":\"passed\","
           << "\"arithmetic_environment_status\":\"passed\","
           << "\"candidate_coverage_status\":\"passed\","
           << "\"production_candidate_coverage_status\":\"passed\","
           << "\"production_match_status\":\"passed\","
           << "\"reference_match_status\":\"passed\","
           << "\"all_candidate_distance_bound_status\":\"passed\","
           << "\"ranking_status\":\"passed\","
           << "\"float64_index_agreement\":"
           << (report.float64_index_agrees ? "true" : "false")
           << ",\"exact_ambiguity_status\":\""
           << (report.exact_ambiguity_known
                   ? (report.exact_ambiguity ? "known-ambiguous" : "known-unambiguous")
                   : "unknown") << "\""
           << ",\"nn_verdict\":\"" << verdict_name(report.nn_verdict)
           << "\",\"case_status\":\"passed\"}\n";
}

void validate_correctness_ledger(const fs::path& ledger_path, const Gate& gate,
                                 const Options& options,
                                 const s10::LoadedSift1m& dataset) {
    const auto ledger_bytes = read_text(ledger_path);
    require(s10::detail::sha256_bytes(ledger_bytes) == gate.correctness_ledger_sha256,
            "correctness ledger SHA-256 mismatch");
    std::istringstream input(ledger_bytes);
    const std::set<std::string> schema = {"schema_version","ordinal","revision",
        "working_tree_state","source_manifest_sha256","experiment_config_sha256",
        "sift_base_sha256","sift_query_sha256","cuda_architecture","build_flags",
        "benchmark_binary_sha256","artifact_manifest_sha256",
        "numerical_build_identity_sha256","variant","N","D","query_ordinal",
        "block_size","implementation_index","implementation_distance_bits",
        "f32_lexicographic_status","production_path_status","graph_status",
        "arithmetic_environment_status","candidate_coverage_status",
        "production_candidate_coverage_status","production_match_status",
        "reference_match_status","all_candidate_distance_bound_status","ranking_status",
        "float64_index_agreement","exact_ambiguity_status","nn_verdict","case_status"};
    constexpr std::array<std::string_view, 5> variants = {"cpu-f32","v0","v1","v2","v3"};
    std::set<std::string> unique;
    std::string line; std::uint64_t ordinal = 0U;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        require(!line.empty() && ordinal < 20U, "malformed or extra correctness ledger record");
        const auto object = StrictJsonParser(line).parse(); require_schema(object, schema);
        const auto expected_n = kCandidateCounts[static_cast<std::size_t>(ordinal / 5U)];
        const auto expected_variant = variants[static_cast<std::size_t>(ordinal % 5U)];
        require(json_string(object,"schema_version") == "S10-CORRECTNESS-CASE-v2" &&
                    json_u64(object,"ordinal") == ordinal &&
                    json_string(object,"variant") == expected_variant &&
                    json_u64(object,"N") == expected_n && json_u64(object,"D") == kDimension &&
                    json_u64(object,"query_ordinal") == 0U &&
                    json_u64(object,"block_size") == (expected_variant == "cpu-f32" ? 0U : kBlockSize),
                "correctness ledger matrix/order mismatch");
        (void)json_u64(object,"implementation_index");
        (void)json_u64(object,"implementation_distance_bits");
        require(json_string(object,"revision") == gate.revision &&
                    json_string(object,"working_tree_state") == gate.tree_state &&
                    json_string(object,"source_manifest_sha256") == gate.source_manifest_sha256 &&
                    json_string(object,"experiment_config_sha256") == options.config_sha256 &&
                    json_string(object,"sift_base_sha256") == dataset.base_sha256 &&
                    json_string(object,"sift_query_sha256") == dataset.query_sha256 &&
                    json_string(object,"cuda_architecture") == options.cuda_architecture &&
                    json_string(object,"build_flags") == options.build_flags &&
                    json_string(object,"benchmark_binary_sha256") == options.benchmark_binary_sha256 &&
                    json_string(object,"artifact_manifest_sha256") == options.artifact_manifest_sha256 &&
                    json_string(object,"numerical_build_identity_sha256") == options.numerical_build_identity_sha256,
                "correctness ledger identity mismatch");
        for (const auto& status : {"f32_lexicographic_status","production_path_status",
                 "graph_status","arithmetic_environment_status","candidate_coverage_status",
                 "production_candidate_coverage_status","production_match_status",
                 "reference_match_status","all_candidate_distance_bound_status","ranking_status",
                 "case_status"}) require(json_string(object,status) == "passed",
                                          "correctness ledger contains a non-passing status");
        const auto verdict = json_string(object,"nn_verdict");
        require(verdict == "accepted-unambiguous" || verdict == "accepted-ambiguous",
                "correctness ledger verdict is not accepted");
        require(field(object,"float64_index_agreement",JsonKind::boolean).text.size() != 0U,
                "correctness ledger Boolean is invalid");
        const auto ambiguity = json_string(object,"exact_ambiguity_status");
        require(ambiguity == "known-ambiguous" || ambiguity == "known-unambiguous" ||
                    ambiguity == "unknown", "correctness ledger ambiguity status is invalid");
        const auto key = std::string(expected_variant) + ":" + std::to_string(expected_n);
        require(unique.insert(key).second, "duplicate correctness ledger case"); ++ordinal;
    }
    require(input.eof() && ordinal == 20U && unique.size() == 20U,
            "correctness ledger is incomplete");
}

int run_correctness(const fs::path& output, const Options& options,
                    const s10::LoadedSift1m& dataset) {
    require(fs::exists(options.cpu_manifest) && fs::exists(options.fixture_evidence),
            "CPU build/fixture evidence is missing");
    const auto build = s6::verify_cpu_build_evidence(options.cpu_manifest.string());
    require(build.identity_status() == s6::EvidenceStatus::passed &&
                build.graph_status() == s6::EvidenceStatus::passed,
            "CPU build graph/source identity evidence did not pass");
    std::string fixture_detail;
    const auto gates = s6::load_cpu_fixture_evidence(
        options.fixture_evidence.string(), build.source_identity(), fixture_detail);
    require(gates.fixture_status == s6::EvidenceStatus::passed &&
                gates.oracle_status == s6::EvidenceStatus::passed &&
                gates.tie_status == s6::EvidenceStatus::passed &&
                gates.reduction_status == s6::EvidenceStatus::passed,
            "CPU mandatory fixture evidence did not pass");
    verify_cuda_evidence(options);

    fs::create_directories(output);
    std::ofstream cases(output / "correctness-cases.jsonl", std::ios::binary);
    require(static_cast<bool>(cases), "cannot create S10 correctness case evidence");
    std::uint64_t accepted_cases = 0U;
    for (const auto n : kCandidateCounts) {
        const auto data = dataset.prefix(n);
        const auto query = dataset.query_view();
        const auto cpu = s6::cpu_search_float32_evidenced(data, query, build);
        require(cpu.production.invocation_status == s6::InvocationStatus::completed &&
                    cpu.production.result,
                "S10 CPU production invocation failed");
        const auto reference = s6::compute_float64_reference(data, query);
        require(reference.invocation_status == s6::InvocationStatus::completed,
                "S10 Float64 reference invocation failed");
        s6::ValidationOptions validation;
        validation.production_evidence = &cpu.evidence;
        validation.mandatory_gate_evidence = &gates;
        const auto report = s6::validate_cpu_search(data, query, cpu.production,
                                                    reference, validation);
        require(report.graph_status == s6::EvidenceStatus::passed &&
                    report.arithmetic_environment_status == s6::EvidenceStatus::passed &&
                    report.candidate_coverage_status == s6::EvidenceStatus::passed &&
                    report.production_candidate_coverage_status ==
                        s6::EvidenceStatus::passed &&
                    report.production_match_status == s6::EvidenceStatus::passed &&
                    report.reference_match_status == s6::EvidenceStatus::passed &&
                    report.ud001_status == s6::EvidenceStatus::passed &&
                    report.ud002_status == s6::EvidenceStatus::passed &&
                    accepted(report.nn_verdict),
                "S10 CPU numerical correctness case was not accepted");
        write_case(cases, accepted_cases, "cpu-f32", n, 0U, cpu.production,
                   report, options, dataset);
        ++accepted_cases;

        const auto v0 = s6::cuda_v0_search_float32_evidenced(data, query);
        validate_gpu_case(v0, report, n);
        write_case(cases, accepted_cases, "v0", n, kBlockSize, v0.production,
                   report, options, dataset);
        ++accepted_cases;
        const auto v1 = s6::cuda_v1_search_float32_evidenced(data, query);
        validate_gpu_case(v1, report, n);
        write_case(cases, accepted_cases, "v1", n, kBlockSize, v1.production,
                   report, options, dataset);
        ++accepted_cases;
        const auto v2 = s6::cuda_v2_search_float32_evidenced(
            data, query, {0, nullptr, kBlockSize});
        validate_gpu_case(v2, report, n);
        write_case(cases, accepted_cases, "v2", n, kBlockSize, v2.production,
                   report, options, dataset);
        ++accepted_cases;
        const auto v3 = s6::cuda_v3_search_float32_evidenced(
            data, query, {0, nullptr, kBlockSize});
        validate_gpu_case(v3, report, n);
        write_case(cases, accepted_cases, "v3", n, kBlockSize, v3.production,
                   report, options, dataset);
        ++accepted_cases;
    }
    require(accepted_cases == 20U, "S10 correctness matrix is incomplete");
    cases.close();
    require(static_cast<bool>(cases), "S10 correctness case evidence write failed");
    const auto ledger_sha256 = s10::detail::sha256_file(output / "correctness-cases.jsonl");

    const bool clean = std::string(S10_BENCHMARK_TREE_STATE) == "clean";
    std::ofstream gate(output / "correctness-gate.json", std::ios::binary);
    gate << "{\n  \"schema_version\": \"S10-CORRECTNESS-GATE-v2\",\n"
         << "  \"status\": \""
         << (clean ? "passed-clean-candidate"
                   : "passed-local-uncommitted-not-acceptance") << "\",\n"
         << "  \"revision\": \"" << S10_BENCHMARK_REVISION << "\",\n"
         << "  \"working_tree_state\": \"" << S10_BENCHMARK_TREE_STATE << "\",\n"
         << "  \"source_manifest_sha256\": \""
         << s6::build_identity::kSourceManifestSha256 << "\",\n"
         << "  \"experiment_config_sha256\": \"" << options.config_sha256 << "\",\n"
         << "  \"sift_base_sha256\": \"" << dataset.base_sha256 << "\",\n"
         << "  \"sift_query_sha256\": \"" << dataset.query_sha256 << "\",\n"
         << "  \"cuda_architecture\": \"" << json_escape(options.cuda_architecture) << "\",\n"
         << "  \"build_flags\": \"" << json_escape(options.build_flags) << "\",\n"
         << "  \"correctness_ledger_sha256\": \"" << ledger_sha256 << "\",\n"
         << "  \"benchmark_binary_sha256\": \"" << options.benchmark_binary_sha256 << "\",\n"
         << "  \"artifact_manifest_sha256\": \"" << options.artifact_manifest_sha256 << "\",\n"
         << "  \"numerical_build_identity_sha256\": \""
         << options.numerical_build_identity_sha256 << "\",\n"
         << "  \"required_cases\": 20,\n  \"accepted_cases\": 20,\n"
         << "  \"failed_cases\": 0,\n  \"blocked_cases\": 0,\n"
         << "  \"v2_block_size\": 256,\n  \"v3_block_size\": 256,\n"
         << "  \"performance_authorized\": " << (clean ? "true" : "false")
         << "\n}\n";
    require(static_cast<bool>(gate), "cannot write S10 correctness gate");
    return 0;
}

s6::ProductionOutcome raw(std::string_view variant, s6::DatasetView data,
                          s6::QueryView query) {
    if (variant == "v0") return s6::cuda_v0_search_float32(data, query);
    if (variant == "v1") return s6::cuda_v1_search_float32(data, query);
    if (variant == "v2") return s6::cuda_v2_search_float32(
        data, query, {0, nullptr, kBlockSize});
    if (variant == "v3") return s6::cuda_v3_search_float32(
        data, query, {0, nullptr, kBlockSize});
    throw std::runtime_error("unknown CUDA variant");
}

s6::detail::S7StagedOutcome staged(std::string_view variant,
                                    s6::DatasetView data,
                                    s6::QueryView query) {
    if (variant == "v0") return s6::detail::cuda_v0_search_float32_staged_for_benchmark(data, query);
    if (variant == "v1") return s6::detail::cuda_v1_search_float32_staged_for_benchmark(data, query);
    if (variant == "v2") return s6::detail::cuda_v2_search_float32_staged_for_benchmark(
        data, query, {0, nullptr, kBlockSize});
    if (variant == "v3") return s6::detail::cuda_v3_search_float32_staged_for_benchmark(
        data, query, {0, nullptr, kBlockSize});
    throw std::runtime_error("unknown CUDA variant");
}

void check_result(const s6::ProductionOutcome& outcome,
                  const s6::SearchResult& expected) {
    require(outcome.invocation_status == s6::InvocationStatus::completed &&
                outcome.result && outcome.result->index == expected.index &&
                float_bits(outcome.result->squared_distance) ==
                    float_bits(expected.squared_distance),
            "timed production result differs from accepted Float32 result");
}

void add_sample(std::vector<Sample>& samples, std::string_view variant,
                std::string_view boundary, std::string_view timer,
                std::uint64_t n, std::uint32_t block_size,
                std::uint32_t order, std::uint32_t iteration,
                std::uint64_t elapsed_ns) {
    samples.push_back({std::string(variant), std::string(boundary),
                       std::string(timer), n, elapsed_ns, block_size,
                       order, iteration});
}

int run_performance(const fs::path& output, const Options& options,
                    const s10::LoadedSift1m& dataset) {
    require(fs::exists(options.correctness_reference),
            "matching S10 correctness reference is required");
    const auto gate = read_gate(options.correctness_reference);
    validate_performance_gate(gate, options, dataset);
    validate_correctness_ledger(options.correctness_reference.parent_path() /
                                    "correctness-cases.jsonl",
                                gate, options, dataset);
    std::vector<Sample> samples;
    constexpr std::array<std::string_view, 4> variants = {"v0", "v1", "v2", "v3"};
    for (const auto n : kCandidateCounts) {
        const auto data = dataset.prefix(n);
        const auto query = dataset.query_view();
        const auto accepted_cpu = s6::cpu_search_float32(data, query);
        require(accepted_cpu.invocation_status == s6::InvocationStatus::completed &&
                    accepted_cpu.result,
                "CPU preflight failed before timing");
        for (unsigned iteration = 0U; iteration < kWarmups + kRepetitions; ++iteration) {
            const auto start = Clock::now();
            const auto result = s6::cpu_search_float32(data, query);
            const auto stop = Clock::now();
            check_result(result, *accepted_cpu.result);
            if (iteration >= kWarmups) {
                add_sample(samples, "cpu-f32", "CPU-E2E-RAW", "steady_clock",
                           n, 0U, 0U, iteration - kWarmups,
                           static_cast<std::uint64_t>(
                               std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   stop - start).count()));
            }
        }
        for (unsigned iteration = 0U; iteration < kWarmups + kRepetitions; ++iteration) {
            const auto offset = iteration % variants.size();
            for (std::size_t order = 0U; order < variants.size(); ++order) {
                const auto variant = variants[(order + offset) % variants.size()];
                const auto start = Clock::now();
                const auto raw_outcome = raw(variant, data, query);
                const auto stop = Clock::now();
                check_result(raw_outcome, *accepted_cpu.result);
                const auto staged_outcome = staged(variant, data, query);
                check_result(staged_outcome.production, *accepted_cpu.result);
                require(staged_outcome.stages.device_timing_available,
                        "CUDA event timing is unavailable");
                if (iteration >= kWarmups) {
                    const auto retained = iteration - kWarmups;
                    add_sample(samples, variant, "BND-E2E-RAW", "steady_clock", n,
                               variant == "v0" || variant == "v1" ? 256U : kBlockSize,
                               static_cast<std::uint32_t>(order), retained,
                               static_cast<std::uint64_t>(
                                   std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       stop - start).count()));
                    add_sample(samples, variant, "BND-KERNEL", "cuda_event", n,
                               variant == "v0" || variant == "v1" ? 256U : kBlockSize,
                               static_cast<std::uint32_t>(order), retained,
                               staged_outcome.stages.kernel_ns);
                }
            }
        }
    }

    fs::create_directories(output);
    fs::copy_file(options.config, output / "experiment-config.json",
                  fs::copy_options::overwrite_existing);
    std::ofstream raw_output(output / "raw_samples.csv", std::ios::binary);
    raw_output << "schema_version,revision,tree_state,sample_role,boundary,variant,"
                  "N,D,query_ordinal,block_size,run_order,iteration,warmup,"
                  "elapsed_ns,timer,status\n";
    for (const auto& sample : samples) {
        raw_output << "S10-RAW-SAMPLE-v1," << S10_BENCHMARK_REVISION << ','
                   << S10_BENCHMARK_TREE_STATE << ",final," << sample.boundary << ','
                   << sample.variant << ',' << sample.n << ",128,0,"
                   << sample.block_size << ',' << sample.run_order << ','
                   << sample.iteration << ",false," << sample.elapsed_ns << ','
                   << sample.timer << ",completed\n";
    }
    require(static_cast<bool>(raw_output), "cannot write S10 raw samples");
    const auto device = s6::inspect_cuda_v3_device();
    require(device.status == s6::EvidenceStatus::passed,
            "CUDA device identity is unavailable");
    std::ofstream manifest(output / "manifest.json", std::ios::binary);
    manifest << "{\n  \"schema_version\": \"S10-BENCHMARK-MANIFEST-v1\",\n"
             << "  \"experiment_id\": \"" << json_escape(options.experiment_id) << "\",\n"
             << "  \"revision\": \"" << S10_BENCHMARK_REVISION << "\",\n"
             << "  \"working_tree_state\": \"" << S10_BENCHMARK_TREE_STATE << "\",\n"
             << "  \"sample_role\": \"final\",\n"
             << "  \"config_sha256\": \"" << options.config_sha256 << "\",\n"
             << "  \"source_manifest_sha256\": \""
             << s6::build_identity::kSourceManifestSha256 << "\",\n"
             << "  \"sift_base_sha256\": \"" << dataset.base_sha256 << "\",\n"
             << "  \"sift_query_sha256\": \"" << dataset.query_sha256 << "\",\n"
             << "  \"correctness_reference\": \""
             << json_escape(options.correctness_reference.generic_string()) << "\",\n"
             << "  \"correctness_ledger_sha256\": \"" << gate.correctness_ledger_sha256 << "\",\n"
             << "  \"benchmark_binary_sha256\": \"" << options.benchmark_binary_sha256 << "\",\n"
             << "  \"artifact_manifest_sha256\": \"" << options.artifact_manifest_sha256 << "\",\n"
             << "  \"numerical_build_identity_sha256\": \""
             << options.numerical_build_identity_sha256 << "\",\n"
             << "  \"measurement_status\": \"valid-unreviewed\",\n"
             << "  \"warmups\": 10,\n  \"repetitions\": 100,\n"
             << "  \"percentile_method\": \"linear_n_minus_1\",\n"
             << "  \"v2_block_size\": 256,\n  \"v3_block_size\": 256,\n"
             << "  \"fast_math\": \"disabled\",\n"
             << "  \"gpu_name\": \"" << json_escape(device.name) << "\",\n"
             << "  \"gpu_uuid\": \"" << json_escape(device.uuid) << "\",\n"
             << "  \"cuda_architecture\": \"" << json_escape(options.cuda_architecture) << "\",\n"
             << "  \"build_flags\": \"" << json_escape(options.build_flags) << "\",\n"
             << "  \"captured_at\": \"" << json_escape(options.captured_at) << "\",\n"
             << "  \"timezone\": \"" << json_escape(options.timezone) << "\"\n}\n";
    require(static_cast<bool>(manifest), "cannot write S10 benchmark manifest");
    return 0;
}

int self_test(const fs::path& output) {
    fs::create_directories(output);
    const auto parsed = StrictJsonParser("{\"a\":\"x\",\"b\":20,\"c\":true}").parse();
    require(json_string(parsed,"a") == "x" && json_u64(parsed,"b") == 20U &&
                json_bool(parsed,"c"), "strict JSON parser self-test failed");
    const auto reject_json = [](std::string_view text) {
        try { (void)StrictJsonParser(text).parse(); } catch (...) { return true; }
        return false;
    };
    require(reject_json("{\"a\":1,\"a\":2}") && reject_json("{\"a\":-1}") &&
                reject_json("{\"a\":1.0}") && reject_json("{\"a\":null}") &&
                reject_json("{\"a\":1} trailing"),
            "strict JSON negative tests failed");
    const auto rejected_schema = []() {
        try { const auto value = StrictJsonParser("{\"a\":1}").parse();
              require_schema(value,{"a","b"}); } catch (...) { return true; }
        return false;
    };
    const auto rejected_type = []() {
        try { const auto value = StrictJsonParser("{\"a\":1}").parse();
              (void)json_string(value,"a"); } catch (...) { return true; }
        return false;
    };
    require(rejected_schema() && rejected_type(), "strict JSON schema/type negative tests failed");

    CudaCandidateEvidenceView candidate_evidence{
        s6::InvocationStatus::completed, true, s6::EvidenceStatus::passed,
        s6::EvidenceStatus::passed, s6::EvidenceStatus::blocked,
        kCandidateDistancePendingV0V1, 2U, 2U, 2U};
    require(production_candidate_evidence_status(candidate_evidence, 2U) ==
                s6::EvidenceStatus::passed,
            "documented V0/V1 pending candidate verification was rejected");
    candidate_evidence.detail = kCandidateDistancePendingV2V3;
    require(production_candidate_evidence_status(candidate_evidence, 2U) ==
                s6::EvidenceStatus::passed,
            "documented V2/V3 pending candidate verification was rejected");

    const std::vector<float> expected_distances = {1.0F, 2.0F};
    const std::vector<float> matching_distances = {1.0F, 2.0F};
    const std::vector<float> mismatched_distances = {1.0F, 3.0F};
    const std::vector<float> incomplete_distances = {1.0F};
    const auto matching_status = independently_validate_candidate_distances(
        matching_distances, expected_distances);
    require(cuda_candidate_case_authorized(s6::EvidenceStatus::passed,
                                            s6::EvidenceStatus::passed,
                                            matching_status),
            "successful independent candidate verification did not authorize correctness");
    const auto mismatched_status = independently_validate_candidate_distances(
        mismatched_distances, expected_distances);
    require(mismatched_status == s6::EvidenceStatus::failed &&
                !cuda_candidate_case_authorized(s6::EvidenceStatus::passed,
                                                s6::EvidenceStatus::passed,
                                                mismatched_status),
            "failed independent candidate verification authorized correctness");
    require(independently_validate_candidate_distances(
                incomplete_distances, expected_distances) == s6::EvidenceStatus::failed,
            "incomplete independent candidate evidence authorized correctness");

    candidate_evidence.detail = "unrecognized blocked candidate evidence";
    require(production_candidate_evidence_status(candidate_evidence, 2U) ==
                s6::EvidenceStatus::blocked,
            "unsupported blocked candidate evidence bypassed the gate");
    candidate_evidence.detail = kCandidateDistancePendingV0V1;
    candidate_evidence.mapping_status = s6::EvidenceStatus::blocked;
    require(production_candidate_evidence_status(candidate_evidence, 2U) ==
                s6::EvidenceStatus::blocked,
            "genuinely blocked production mapping evidence bypassed the gate");
    candidate_evidence.mapping_status = s6::EvidenceStatus::failed;
    require(production_candidate_evidence_status(candidate_evidence, 2U) ==
                s6::EvidenceStatus::failed,
            "failed production mapping evidence bypassed the gate");
    candidate_evidence.mapping_status = s6::EvidenceStatus::passed;
    candidate_evidence.overwrite_status = s6::EvidenceStatus::blocked;
    require(production_candidate_evidence_status(candidate_evidence, 2U) ==
                s6::EvidenceStatus::blocked,
            "genuinely blocked overwrite evidence bypassed the gate");
    candidate_evidence.overwrite_status = s6::EvidenceStatus::failed;
    require(production_candidate_evidence_status(candidate_evidence, 2U) ==
                s6::EvidenceStatus::failed,
            "failed overwrite evidence bypassed the gate");
    candidate_evidence.overwrite_status = s6::EvidenceStatus::passed;
    candidate_evidence.invocation_status = s6::InvocationStatus::backend_failure;
    require(production_candidate_evidence_status(candidate_evidence, 2U) ==
                s6::EvidenceStatus::blocked,
            "failed production invocation bypassed the gate");
    candidate_evidence.invocation_status = s6::InvocationStatus::completed;
    candidate_evidence.candidate_distance_status = s6::EvidenceStatus::failed;
    require(production_candidate_evidence_status(candidate_evidence, 2U) ==
                s6::EvidenceStatus::failed,
            "failed candidate evidence bypassed the gate");
    candidate_evidence.candidate_distance_status = s6::EvidenceStatus::passed;
    require(production_candidate_evidence_status(candidate_evidence, 2U) ==
                s6::EvidenceStatus::blocked,
            "unsupported candidate evidence status bypassed the gate");
    candidate_evidence.candidate_distance_status = s6::EvidenceStatus::blocked;
    candidate_evidence.candidate_distance_count = 1U;
    require(production_candidate_evidence_status(candidate_evidence, 2U) ==
                s6::EvidenceStatus::failed,
            "incomplete candidate coverage bypassed the gate");
    candidate_evidence.candidate_distance_count = 2U;
    candidate_evidence.observed_mapping_count = 1U;
    require(production_candidate_evidence_status(candidate_evidence, 2U) ==
                s6::EvidenceStatus::failed,
            "incomplete observed mapping coverage bypassed the gate");
    candidate_evidence.observed_mapping_count = 2U;
    candidate_evidence.expected_candidate_count = 1U;
    require(production_candidate_evidence_status(candidate_evidence, 2U) ==
                s6::EvidenceStatus::failed,
            "incorrect expected candidate coverage bypassed the gate");
    candidate_evidence.expected_candidate_count = 2U;
    candidate_evidence.result_present = false;
    require(production_candidate_evidence_status(candidate_evidence, 2U) ==
                s6::EvidenceStatus::blocked,
            "missing production result bypassed the gate");
    require(!cuda_candidate_case_authorized(s6::EvidenceStatus::passed,
                                             s6::EvidenceStatus::failed,
                                             s6::EvidenceStatus::passed),
            "failed production reduction/output evidence authorized correctness");

    Options options;
    options.expected_revision = "r";
    options.config_sha256 = "c";
    options.base_sha256 = "b";
    options.query_sha256 = "q";
    options.cuda_architecture = "86";
    options.build_flags = "Release-no-fast-math";
    options.benchmark_binary_sha256 = "binary";
    options.artifact_manifest_sha256 = "artifacts";
    options.numerical_build_identity_sha256 = "numerical";
    s10::LoadedSift1m dataset;
    dataset.base_sha256 = "b";
    dataset.query_sha256 = "q";
    Gate gate{"passed-clean-candidate", "r", "clean", "s", "c", "b", "q",
              "86", "Release-no-fast-math", "ledger", "binary", "artifacts",
              "numerical", 20U, 20U, 0U, 0U, 256U, 256U, true};
    require(performance_gate_qualifies(gate, options, dataset, "r", "clean", "s"),
            "qualifying correctness gate was rejected");
    gate.accepted_cases = 19U;
    require(!performance_gate_qualifies(gate, options, dataset, "r", "clean", "s"),
            "incomplete correctness matrix authorized performance");
    gate.accepted_cases = 20U;
    gate.base_sha256 = "different";
    require(!performance_gate_qualifies(gate, options, dataset, "r", "clean", "s"),
            "dataset hash mismatch authorized performance");
    gate.base_sha256 = "b";
    gate.config_sha256 = "different";
    require(!performance_gate_qualifies(gate, options, dataset, "r", "clean", "s"),
            "configuration hash mismatch authorized performance");
    gate.config_sha256 = "c";
    gate.performance_authorized = false;
    require(!performance_gate_qualifies(gate, options, dataset, "r", "clean", "s"),
            "non-authorizing correctness gate authorized performance");
    gate.performance_authorized = true;
    gate.benchmark_binary_sha256 = "different";
    require(!performance_gate_qualifies(gate, options, dataset, "r", "clean", "s"),
            "binary mismatch authorized performance");
    gate.benchmark_binary_sha256 = "binary";
    gate.artifact_manifest_sha256 = "different";
    require(!performance_gate_qualifies(gate, options, dataset, "r", "clean", "s"),
            "artifact manifest mismatch authorized performance");
    gate.artifact_manifest_sha256 = "artifacts";
    gate.numerical_build_identity_sha256 = "different";
    require(!performance_gate_qualifies(gate, options, dataset, "r", "clean", "s"),
            "numerical build mismatch authorized performance");
    gate.numerical_build_identity_sha256 = "numerical";
    require(!performance_gate_qualifies(gate, options, dataset, "other", "clean", "s") &&
                !performance_gate_qualifies(gate, options, dataset, "r", "dirty", "s"),
            "revision or tree-state mismatch authorized performance");
    const auto ledger_path = output / "correctness-cases.jsonl";
    const auto case_line = [&](std::uint64_t ordinal) {
        constexpr std::array<std::string_view,5> variants = {"cpu-f32","v0","v1","v2","v3"};
        const auto variant = variants[static_cast<std::size_t>(ordinal % 5U)];
        const auto n = kCandidateCounts[static_cast<std::size_t>(ordinal / 5U)];
        std::ostringstream line;
        line << "{\"schema_version\":\"S10-CORRECTNESS-CASE-v2\",\"ordinal\":" << ordinal
             << ",\"revision\":\"r\",\"working_tree_state\":\"clean\","
                "\"source_manifest_sha256\":\"s\",\"experiment_config_sha256\":\"c\","
                "\"sift_base_sha256\":\"b\",\"sift_query_sha256\":\"q\","
                "\"cuda_architecture\":\"86\",\"build_flags\":\"Release-no-fast-math\","
                "\"benchmark_binary_sha256\":\"binary\",\"artifact_manifest_sha256\":\"artifacts\","
                "\"numerical_build_identity_sha256\":\"numerical\",\"variant\":\"" << variant
             << "\",\"N\":" << n << ",\"D\":128,\"query_ordinal\":0,\"block_size\":"
             << (variant == "cpu-f32" ? 0U : 256U)
             << ",\"implementation_index\":0,\"implementation_distance_bits\":0,"
                "\"f32_lexicographic_status\":\"passed\",\"production_path_status\":\"passed\","
                "\"graph_status\":\"passed\",\"arithmetic_environment_status\":\"passed\","
                "\"candidate_coverage_status\":\"passed\","
                "\"production_candidate_coverage_status\":\"passed\","
                "\"production_match_status\":\"passed\",\"reference_match_status\":\"passed\","
                "\"all_candidate_distance_bound_status\":\"passed\",\"ranking_status\":\"passed\","
                "\"float64_index_agreement\":true,\"exact_ambiguity_status\":\"known-unambiguous\","
                "\"nn_verdict\":\"accepted-unambiguous\",\"case_status\":\"passed\"}";
        return line.str();
    };
    const auto write_ledger = [&](const std::vector<std::uint64_t>& order, bool malformed) {
        std::ofstream ledger(ledger_path, std::ios::binary | std::ios::trunc);
        for (const auto ordinal : order) ledger << (malformed ? "{}" : case_line(ordinal)) << '\n';
        ledger.close(); gate.correctness_ledger_sha256 = s10::detail::sha256_file(ledger_path);
    };
    std::vector<std::uint64_t> order(20U); for (std::uint64_t i=0;i<20U;++i) order[i]=i;
    write_ledger(order, false); validate_correctness_ledger(ledger_path, gate, options, dataset);
    const auto rejected_ledger = [&]() {
        try { validate_correctness_ledger(ledger_path, gate, options, dataset); }
        catch (...) { return true; } return false;
    };
    order.pop_back(); write_ledger(order,false); require(rejected_ledger(),"missing ledger case accepted");
    order.push_back(18U); write_ledger(order,false); require(rejected_ledger(),"duplicate ledger case accepted");
    order[19]=19U; std::swap(order[0],order[1]); write_ledger(order,false);
    require(rejected_ledger(),"reordered ledger accepted");
    std::swap(order[0],order[1]); write_ledger(order,true);
    require(rejected_ledger(),"malformed ledger accepted");
    write_ledger(order,false); gate.correctness_ledger_sha256 = "modified";
    require(rejected_ledger(),"modified ledger accepted");
    std::ofstream result(output / "self-test.json", std::ios::binary);
    result << "{\"schema_version\":\"S10-BENCHMARK-SELF-TEST-v1\","
              "\"status\":\"passed\",\"revision\":\""
           << S10_BENCHMARK_REVISION << "\"}\n";
    return 0;
}

void write_command(const fs::path& output, int argc, char** argv) {
    fs::create_directories(output);
    std::ofstream command(output / "commands.txt", std::ios::binary);
    for (int index = 0; index < argc; ++index) {
        if (index != 0) command << ' ';
        command << std::quoted(argv[index]);
    }
    command << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc >= 3,
                "usage: s10_sift1m --self-test OUTPUT or --correctness|--run OUTPUT OPTIONS");
        const std::string mode = argv[1];
        const fs::path output = argv[2];
        if (mode == "--self-test") {
            require(argc == 3, "--self-test accepts only an output directory");
            return self_test(output);
        }
        require(mode == "--correctness" || mode == "--run", "unknown S10 mode");
        const auto options = parse_options(argc, argv, 3);
        const auto loaded = s10::load_sift1m(options.dataset_root);
        require(loaded.status == s10::Sift1mLoadStatus::loaded && loaded.dataset,
                std::string("SIFT1M load failed: ") + loaded.detail);
        require(loaded.dataset->base_sha256 == options.base_sha256 &&
                    loaded.dataset->query_sha256 == options.query_sha256,
                "actual SIFT1M SHA-256 values do not match orchestration identity");
        write_command(output, argc, argv);
        if (mode == "--correctness") {
            return run_correctness(output, options, *loaded.dataset);
        }
        return run_performance(output, options, *loaded.dataset);
    } catch (const std::exception& error) {
        std::cerr << "S10 SIFT1M: FAIL: " << error.what() << '\n';
        return 1;
    }
}
