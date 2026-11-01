#include "s6/cuda_v0.hpp"
#include "s6/cpu_build_identity.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kSeed = 0x6a09e667f3bcc909ULL;
constexpr const char* kGraphPolicy = "S6-NUM-GRAPH-v1.0";
constexpr const char* kEnvironmentPolicy = "S6-NUM-ENV-v1.0";
constexpr const char* kBoundPolicy = "S6-UD001-DIST-BOUND-v1.0";
constexpr const char* kRankingPolicy = "S6-UD002-RANK-INTERVAL-v1.0";
constexpr const char* kVerdictPolicy = "S6-UD003-QCA-v1.0";
constexpr const char* kCertifiedMethod =
    "binary64-outward-nextafter-interval-v1";

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

std::uint64_t bits(double value) {
    std::uint64_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

double down(double value) {
    return std::nextafter(value, -std::numeric_limits<double>::infinity());
}

double up(double value) {
    return std::nextafter(value, std::numeric_limits<double>::infinity());
}

s6::Interval add(s6::Interval lhs, s6::Interval rhs) {
    return {down(lhs.lower + rhs.lower), up(lhs.upper + rhs.upper)};
}

s6::Interval subtract(s6::Interval lhs, s6::Interval rhs) {
    return {down(lhs.lower - rhs.upper), up(lhs.upper - rhs.lower)};
}

s6::Interval multiply(s6::Interval lhs, s6::Interval rhs) {
    const double products[] = {lhs.lower * rhs.lower, lhs.lower * rhs.upper,
                               lhs.upper * rhs.lower, lhs.upper * rhs.upper};
    const auto limits = std::minmax_element(std::begin(products), std::end(products));
    return {down(*limits.first), up(*limits.second)};
}

s6::Interval divide_positive(s6::Interval numerator, s6::Interval denominator) {
    require(denominator.lower > 0.0, "invalid positive interval denominator");
    return {down(numerator.lower / denominator.upper),
            up(numerator.upper / denominator.lower)};
}

s6::NumericalPolicyEvaluation independently_evaluate_policy(std::uint64_t d) {
    const s6::Interval one{1.0, 1.0};
    const s6::Interval u32{std::ldexp(1.0, -24), std::ldexp(1.0, -24)};
    const s6::Interval u64{std::ldexp(1.0, -53), std::ldexp(1.0, -53)};
    const s6::Interval eta32{std::ldexp(1.0, -150), std::ldexp(1.0, -150)};
    const s6::Interval dimension{static_cast<double>(d), static_cast<double>(d)};
    const auto du32 = multiply(dimension, u32);
    const auto du64 = multiply(dimension, u64);
    const auto gamma32 = divide_positive(du32, subtract(one, du32));
    const auto gamma64 = divide_positive(du64, subtract(one, du64));
    const auto one_plus_u32 = add(one, u32);
    const auto one_plus_u64 = add(one, u64);
    const auto e32 = subtract(
        multiply(multiply(one_plus_u32, one_plus_u32), add(one, gamma32)), one);
    const auto e64 = subtract(
        multiply(multiply(one_plus_u64, one_plus_u64), add(one, gamma64)), one);
    auto additive = multiply(multiply(dimension, eta32), add(one, gamma32));
    auto relative = divide_positive(add(e32, e64), subtract(one, e64));
    additive.lower = std::max(0.0, additive.lower);
    relative.lower = std::max(0.0, relative.lower);
    return {gamma32, gamma64, e32, e64, additive, relative};
}

bool same_interval(s6::Interval lhs, s6::Interval rhs) {
    return bits(lhs.lower) == bits(rhs.lower) && bits(lhs.upper) == bits(rhs.upper);
}

float independent_float32_distance(const float* candidate, const float* query,
                                   std::size_t dimension) {
    float accumulator = 0.0F;
    for (std::size_t j = 0; j < dimension; ++j) {
        volatile float difference = candidate[j] - query[j];
        accumulator = std::fma(difference, difference, accumulator);
    }
    return accumulator;
}

double independent_float64_distance(const float* candidate, const float* query,
                                    std::size_t dimension) {
    double accumulator = 0.0;
    for (std::size_t j = 0; j < dimension; ++j) {
        volatile double difference = static_cast<double>(candidate[j]) -
                                     static_cast<double>(query[j]);
        accumulator = std::fma(difference, difference, accumulator);
    }
    return accumulator;
}

std::map<std::string, std::string> load_fields(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "could not open correlated evidence input");
    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto delimiter = line.find('=');
        if (delimiter != std::string::npos) {
            fields.emplace(line.substr(0, delimiter), line.substr(delimiter + 1));
        }
    }
    return fields;
}

const std::string& field(const std::map<std::string, std::string>& fields,
                         const std::string& name) {
    const auto found = fields.find(name);
    if (found == fields.end()) {
        throw std::runtime_error("missing correlated evidence field: " + name);
    }
    return found->second;
}

void verify_binary_correlation(const std::filesystem::path& path) {
    const auto fields = load_fields(path);
    require(field(fields, "schema") == "S7-CUDA-V0-BINARY-EVIDENCE-v1.0",
            "unsupported CUDA binary evidence schema");
    require(field(fields, "revision") == s6::build_identity::kRevision &&
                field(fields, "working_tree_state") ==
                    s6::build_identity::kWorkingTreeState &&
                field(fields, "working_tree_fingerprint") ==
                    s6::build_identity::kWorkingTreeFingerprint &&
                field(fields, "source_manifest_sha256") ==
                    s6::build_identity::kSourceManifestSha256,
            "binary/source identity correlation failed");
    for (const char* name : {"compile_policy_status", "distance_kernel_symbol_status",
                             "distance_kernel_instruction_status",
                             "distance_kernel_dependency_status",
                             "distance_kernel_default_rn_no_ftz_status",
                             "binary_graph_structure_status"}) {
        require(field(fields, name) == "passed",
                "binary/SASS arithmetic graph evidence did not pass");
    }
}

void verify_functional_correlation(const std::filesystem::path& path) {
    const auto fields = load_fields(path);
    require(field(fields, "schema") == "S7-CUDA-V0-FUNCTIONAL-EVIDENCE-v1.0",
            "unsupported CUDA functional evidence schema");
    require(field(fields, "revision") == s6::build_identity::kRevision &&
                field(fields, "working_tree_state") ==
                    s6::build_identity::kWorkingTreeState &&
                field(fields, "working_tree_fingerprint") ==
                    s6::build_identity::kWorkingTreeFingerprint &&
                field(fields, "source_manifest_sha256") ==
                    s6::build_identity::kSourceManifestSha256,
            "functional/source identity correlation failed");
    require(field(fields, "overall") == "passed",
            "functional evidence did not pass");
    std::ifstream input(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    for (const char* token : {"reduction=passed", "exact_dyadic_oracle=passed",
                              "interval_boundary_fixture=passed",
                              "valid_outside_domain=passed"}) {
        require(text.find(token) != std::string::npos,
                "mandatory CUDA functional fixture evidence is incomplete");
    }
}

std::string join_indices(const std::vector<std::uint64_t>& values) {
    std::ostringstream output;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0U) {
            output << ',';
        }
        output << values[i];
    }
    return output.str();
}

bool contains(const std::vector<std::uint64_t>& values, std::uint64_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
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

static_assert(sizeof(CandidateReportHeader) == 24U, "candidate header ABI");
static_assert(sizeof(CandidateReportRecord) == 124U, "candidate record ABI");

void write_case_report(const std::filesystem::path& report_root, std::uint64_t n,
                       std::uint64_t d, const s6::CudaV0EvidencedOutcome& gpu,
                       const std::vector<double>& distances64,
                       const std::vector<std::uint64_t>& mathematical_numerators,
                       const s6::RankingEvaluation& ranking,
                       const s6::NumericalPolicyEvaluation& policy,
                       std::uint64_t independent_index, std::uint64_t reference_index,
                       std::uint64_t mathematical_index) {
    const std::string case_id = "cuda-v0-N" + std::to_string(n) + "-D" +
                                std::to_string(d);
    std::filesystem::create_directories(report_root);
    const auto candidate_path = report_root / (case_id + ".candidates.bin");
    const auto metadata_path = report_root / (case_id + ".metadata.txt");
    std::ofstream candidates(candidate_path, std::ios::binary | std::ios::trunc);
    require(candidates.good(), "could not create CUDA candidate evidence report");
    const CandidateReportHeader header{{'S', '7', 'C', 'A', 'N', 'D', '0', '1'}, 1U,
                                       sizeof(CandidateReportRecord), n};
    candidates.write(reinterpret_cast<const char*>(&header), sizeof(header));

    const float minimum32 = gpu.evidence.candidate_distances[
        static_cast<std::size_t>(independent_index)];
    const double minimum64 = distances64[static_cast<std::size_t>(reference_index)];
    const auto minimum_mathematical = mathematical_numerators[
        static_cast<std::size_t>(mathematical_index)];
    for (std::uint64_t i = 0; i < n; ++i) {
        const auto position = static_cast<std::size_t>(i);
        const float distance32 = gpu.evidence.candidate_distances[position];
        const double distance64 = distances64[position];
        const bool exact_zero = mathematical_numerators[position] == 0U;
        const auto bound = s6::evaluate_ud001(d, distance32, distance64, exact_zero);
        require(bound.status == s6::EvidenceStatus::passed,
                "candidate report encountered non-passing UD-001 evidence");
        const auto& ranked = ranking.candidates[position];
        CandidateReportRecord record{};
        record.index = i;
        record.float32_bits = bits(distance32);
        record.float64_bits = bits(distance64);
        record.observed_error_lower_bits = bits(bound.observed_error.lower);
        record.observed_error_upper_bits = bits(bound.observed_error.upper);
        record.allowed_bound_lower_bits = bits(bound.allowed_bound.lower);
        record.allowed_bound_upper_bits = bits(bound.allowed_bound.upper);
        record.radius_lower_bits = bits(ranked.radius.lower);
        record.radius_upper_bits = bits(ranked.radius.upper);
        record.lower_endpoint_lower_bits = bits(ranked.lower_endpoint.lower);
        record.lower_endpoint_upper_bits = bits(ranked.lower_endpoint.upper);
        record.upper_endpoint_lower_bits = bits(ranked.upper_endpoint.lower);
        record.upper_endpoint_upper_bits = bits(ranked.upper_endpoint.upper);
        record.mathematical_numerator = mathematical_numerators[position];
        record.mathematical_exponent = -12;
        record.ud001_status = 1U;
        record.exact_zero_required = bound.exact_zero_required ? 1U : 0U;
        record.exact_zero_satisfied = bound.exact_zero_satisfied ? 1U : 0U;
        record.confirmed_contender = ranked.confirmed_contender ? 1U : 0U;
        record.confirmed_non_contender = ranked.confirmed_non_contender ? 1U : 0U;
        record.boundary_uncertain = ranked.boundary_uncertain ? 1U : 0U;
        record.conservative_member =
            contains(ranking.conservative_contenders, i) ? 1U : 0U;
        record.exact_member = contains(ranking.exact_contenders, i) ? 1U : 0U;
        record.float32_tie = distance32 == minimum32 ? 1U : 0U;
        record.float64_tie = distance64 == minimum64 ? 1U : 0U;
        record.mathematical_tie =
            mathematical_numerators[position] == minimum_mathematical ? 1U : 0U;
        candidates.write(reinterpret_cast<const char*>(&record), sizeof(record));
    }
    candidates.close();
    require(candidates.good(), "CUDA candidate evidence report write failed");
    const auto expected_bytes = sizeof(CandidateReportHeader) +
                                n * sizeof(CandidateReportRecord);
    require(std::filesystem::file_size(candidate_path) == expected_bytes,
            "CUDA candidate evidence report size mismatch");

    std::ofstream metadata(metadata_path, std::ios::binary | std::ios::trunc);
    require(metadata.good(), "could not create CUDA matrix metadata report");
    metadata
        << "schema=S7-CUDA-V0-CASE-EVIDENCE-v1.0\n"
        << "case_id=" << case_id << '\n'
        << "revision=" << s6::build_identity::kRevision << '\n'
        << "working_tree_state=" << s6::build_identity::kWorkingTreeState << '\n'
        << "working_tree_fingerprint="
        << s6::build_identity::kWorkingTreeFingerprint << '\n'
        << "source_manifest_sha256=" << s6::build_identity::kSourceManifestSha256
        << '\n'
        << "implementation_variant=cuda-v0\n"
        << "workload_kind=single-query-primary\n"
        << "generator_id=s7-cuda-v0-matrix-dyadic-v1\n"
        << "seed=" << kSeed << '\n'
        << "N=" << n << '\n'
        << "D=" << d << '\n'
        << "input_contract_outcome=valid\n"
        << "domain_status=core-eligible\n"
        << "production_invocation=completed\n"
        << "production_index=" << gpu.production.result->index << '\n'
        << "production_distance_bits="
        << bits(gpu.production.result->squared_distance) << '\n'
        << "independent_float32_index=" << independent_index << '\n'
        << "reference_float64_index=" << reference_index << '\n'
        << "mathematical_index=" << mathematical_index << '\n'
        << "graph_policy=" << kGraphPolicy << '\n'
        << "environment_policy=" << kEnvironmentPolicy << '\n'
        << "bound_policy=" << kBoundPolicy << '\n'
        << "ranking_policy=" << kRankingPolicy << '\n'
        << "verdict_policy=" << kVerdictPolicy << '\n'
        << "certified_comparison_method=" << kCertifiedMethod << '\n'
        << "gamma32_lower_bits=" << bits(policy.gamma32.lower) << '\n'
        << "gamma32_upper_bits=" << bits(policy.gamma32.upper) << '\n'
        << "gamma64_lower_bits=" << bits(policy.gamma64.lower) << '\n'
        << "gamma64_upper_bits=" << bits(policy.gamma64.upper) << '\n'
        << "e32_lower_bits=" << bits(policy.e32.lower) << '\n'
        << "e32_upper_bits=" << bits(policy.e32.upper) << '\n'
        << "e64_lower_bits=" << bits(policy.e64.lower) << '\n'
        << "e64_upper_bits=" << bits(policy.e64.upper) << '\n'
        << "additive_lower_bits=" << bits(policy.additive.lower) << '\n'
        << "additive_upper_bits=" << bits(policy.additive.upper) << '\n'
        << "relative_lower_bits=" << bits(policy.relative.lower) << '\n'
        << "relative_upper_bits=" << bits(policy.relative.upper) << '\n'
        << "mapping_status=passed\n"
        << "overwrite_status=passed\n"
        << "candidate_coverage_status=passed\n"
        << "production_path_status=passed\n"
        << "graph_status=passed\n"
        << "arithmetic_environment_status=passed\n"
        << "float32_lexicographic_status=passed\n"
        << "float64_index_agrees=true\n"
        << "ud001_status=passed\n"
        << "ud002_status=passed\n"
        << "contender_provenance=exact\n"
        << "exact_contenders=" << join_indices(ranking.exact_contenders) << '\n'
        << "conservative_contenders="
        << join_indices(ranking.conservative_contenders) << '\n'
        << "boundary_uncertain_indices="
        << join_indices(ranking.boundary_uncertain_indices) << '\n'
        << "exact_membership_resolved=true\n"
        << "exact_ambiguity=false\n"
        << "boundary_status=resolved\n"
        << "fixture_status=passed\n"
        << "oracle_status=passed\n"
        << "tie_status=passed\n"
        << "reduction_status=passed\n"
        << "decision_trace=1:no-demonstrated-numerical-defect|"
           "2:no-candidate-argmin-tie-reduction-production-defect|"
           "3:returned-index-in-exact-C|4:not-applicable-exact-path|"
           "5:mandatory-evidence-complete|6:core-eligible|"
           "7:selected-exact-singleton-and-float64-agreement|"
           "8:not-reached|9:not-reached\n"
        << "nn_verdict=accepted-unambiguous\n"
        << "candidate_encoding=little-endian-packed-v1\n"
        << "candidate_record_size=" << sizeof(CandidateReportRecord) << '\n'
        << "candidate_count=" << n << '\n'
        << "candidate_bytes=" << expected_bytes << '\n'
        << "candidate_path=" << std::filesystem::absolute(candidate_path).string()
        << '\n';
    metadata.close();
    require(metadata.good(), "CUDA matrix metadata report write failed");
}

void run_case(std::uint64_t n, std::uint64_t d, std::ofstream& evidence,
              const std::filesystem::path& report_root) {
    std::vector<float> query(static_cast<std::size_t>(d));
    for (std::uint64_t j = 0; j < d; ++j) {
        const auto centered = static_cast<int>((j + (kSeed & 15U)) % 17U) - 8;
        query[static_cast<std::size_t>(j)] = static_cast<float>(centered) / 16.0F;
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
        dataset[offset] = query[0] + static_cast<float>((i % 31U) + 1U) / 64.0F;
    }
    const std::uint64_t expected_index = n / 2U;
    const auto expected_offset = static_cast<std::size_t>(expected_index * d);
    for (std::uint64_t j = 0; j < d; ++j) {
        dataset[expected_offset + static_cast<std::size_t>(j)] =
            query[static_cast<std::size_t>(j)];
    }

    const s6::DatasetView dataset_view{dataset.data(), n, d, dataset.size()};
    const s6::QueryView query_view{query.data(), d, query.size()};
    const auto gpu = s6::cuda_v0_search_float32_evidenced(dataset_view, query_view);
    require(gpu.production.invocation_status == s6::InvocationStatus::completed &&
                gpu.production.result,
            "matrix CUDA invocation failed");
    require(gpu.evidence.mapping_status == s6::EvidenceStatus::passed &&
                gpu.evidence.overwrite_status == s6::EvidenceStatus::passed &&
                gpu.evidence.observed_mapping_count == n,
            "matrix production mapping/overwrite evidence failed");
    require(gpu.evidence.candidate_distances.size() == static_cast<std::size_t>(n),
            "matrix CUDA candidate evidence count mismatch");

    const auto environment = s6::inspect_arithmetic_environment();
    require(environment.conforming_float32() && environment.conforming_float64(),
            "matrix validator arithmetic environment is nonconforming");
    const auto policy = independently_evaluate_policy(d);
    std::vector<double> distances64(static_cast<std::size_t>(n));
    std::vector<std::uint64_t> mathematical_numerators(static_cast<std::size_t>(n));
    std::uint64_t independent_index = 0U;
    std::uint64_t reference_index = 0U;
    std::uint64_t mathematical_index = 0U;
    float independent_minimum = 0.0F;
    double reference_minimum = 0.0;
    std::uint64_t mathematical_minimum = 0U;
    bool have_minimum = false;
    for (std::uint64_t i = 0; i < n; ++i) {
        const auto offset = static_cast<std::size_t>(i * d);
        const float distance32 = independent_float32_distance(
            dataset.data() + offset, query.data(), static_cast<std::size_t>(d));
        const double distance64 = independent_float64_distance(
            dataset.data() + offset, query.data(), static_cast<std::size_t>(d));
        std::uint64_t numerator = 0U;
        for (std::uint64_t j = 0; j < d; ++j) {
            const auto dataset_scaled = static_cast<std::int64_t>(std::llround(
                static_cast<double>(dataset[offset + static_cast<std::size_t>(j)]) *
                64.0));
            const auto query_scaled = static_cast<std::int64_t>(std::llround(
                static_cast<double>(query[static_cast<std::size_t>(j)]) * 64.0));
            const auto difference = dataset_scaled - query_scaled;
            numerator += static_cast<std::uint64_t>(difference * difference);
        }
        distances64[static_cast<std::size_t>(i)] = distance64;
        mathematical_numerators[static_cast<std::size_t>(i)] = numerator;
        require(bits(gpu.evidence.candidate_distances[static_cast<std::size_t>(i)]) ==
                    bits(distance32),
                "matrix GPU distance differs from independent Float32 graph");
        const auto bound = s6::evaluate_ud001(d, distance32, distance64, numerator == 0U);
        require(bound.status == s6::EvidenceStatus::passed,
                "matrix UD-001 candidate bound failed or blocked");
        const auto expected_bound = add(
            policy.additive, multiply(policy.relative, {distance64, distance64}));
        require(same_interval(bound.allowed_bound,
                              {std::max(0.0, expected_bound.lower),
                               expected_bound.upper}),
                "matrix UD-001 policy terms do not match certified bound");
        if (!have_minimum || distance32 < independent_minimum) {
            independent_minimum = distance32;
            independent_index = i;
        }
        if (!have_minimum || distance64 < reference_minimum) {
            reference_minimum = distance64;
            reference_index = i;
        }
        if (!have_minimum || numerator < mathematical_minimum) {
            mathematical_minimum = numerator;
            mathematical_index = i;
        }
        have_minimum = true;
    }
    const auto ranking = s6::evaluate_ud002(d, distances64);
    require(ranking.status == s6::EvidenceStatus::passed &&
                ranking.exact_membership_resolved &&
                ranking.exact_contenders == std::vector<std::uint64_t>{expected_index} &&
                ranking.conservative_contenders ==
                    std::vector<std::uint64_t>{expected_index} &&
                ranking.boundary_uncertain_indices.empty(),
            "matrix UD-002 classification was not an exact singleton");
    require(independent_index == expected_index && reference_index == expected_index &&
                mathematical_index == expected_index &&
                gpu.production.result->index == expected_index &&
                bits(gpu.production.result->squared_distance) == bits(independent_minimum),
            "matrix independent argmin correlation failed");

    write_case_report(report_root, n, d, gpu, distances64,
                      mathematical_numerators, ranking, policy, independent_index,
                      reference_index, mathematical_index);
    evidence << "case=N" << n << "-D" << d << " seed=" << kSeed
             << " mapping=passed overwrite=passed independent_f32=passed"
             << " independent_f64=passed mathematical_oracle=passed"
             << " ud001=passed ud002=passed candidate_retention=passed"
             << " numerical_pair_classification=exact-singleton"
             << " graph_environment_status=passed"
             << " correlation=passed nn_verdict=accepted-unambiguous"
             << " result_index=" << gpu.production.result->index
             << " result_distance_bits="
             << bits(gpu.production.result->squared_distance) << '\n';
    std::cout << "CUDA V0 matrix N=" << n << " D=" << d << ": PASS\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 5,
                "matrix output, report root, binary evidence, and functional evidence are required");
        verify_binary_correlation(argv[3]);
        verify_functional_correlation(argv[4]);
        const std::filesystem::path evidence_path = argv[1];
        const std::filesystem::path report_root = argv[2];
        std::filesystem::create_directories(evidence_path.parent_path());
        std::ofstream evidence(evidence_path, std::ios::binary | std::ios::trunc);
        require(evidence.good(), "could not open matrix evidence output");
        evidence << "schema=S7-CUDA-V0-MATRIX-EVIDENCE-v1.0\n"
                 << "evidence_class="
                 << (std::strcmp(s6::build_identity::kWorkingTreeState, "clean") == 0
                         ? "clean-candidate"
                         : "local-uncommitted-not-acceptance")
                 << '\n'
                 << "revision=" << s6::build_identity::kRevision << '\n'
                 << "working_tree_state=" << s6::build_identity::kWorkingTreeState
                 << '\n'
                 << "working_tree_fingerprint="
                 << s6::build_identity::kWorkingTreeFingerprint << '\n'
                 << "source_manifest_sha256="
                 << s6::build_identity::kSourceManifestSha256 << '\n'
                 << "binary_source_sass_functional_correlation=passed\n";
        constexpr std::array<std::uint64_t, 4> candidate_counts = {
            1024U, 16384U, 262144U, 1048576U};
        constexpr std::array<std::uint64_t, 3> dimensions = {8U, 32U, 128U};
        for (const auto n : candidate_counts) {
            for (const auto d : dimensions) {
                run_case(n, d, evidence, report_root);
            }
        }
        evidence << "matrix_functional_numerical_comparisons=passed cases=12\n"
                 << "matrix_candidate_evidence=passed reports=12\n"
                 << "matrix_nn_acceptance=passed accepted_unambiguous=12"
                    " accepted_ambiguous=0 failed=0 blocked=0\n";
        std::cout << "S7 CUDA V0 required 12-case matrix: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "S7 CUDA V0 required matrix: FAIL: " << error.what() << '\n';
        return 1;
    }
}
