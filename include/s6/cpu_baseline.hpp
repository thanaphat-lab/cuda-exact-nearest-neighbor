#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace s6 {

inline constexpr const char* kGraphPolicy = "S6-NUM-GRAPH-v1.0";
inline constexpr const char* kEnvironmentPolicy = "S6-NUM-ENV-v1.0";
inline constexpr const char* kBoundPolicy = "S6-UD001-DIST-BOUND-v1.0";
inline constexpr const char* kRankingPolicy = "S6-UD002-RANK-INTERVAL-v1.0";
inline constexpr const char* kVerdictPolicy = "S6-UD003-QCA-v1.0";

enum class InvocationStatus {
    completed,
    contract_invalid,
    api_misuse,
    resource_failure,
    backend_failure,
    not_executed,
};

enum class InputOutcome {
    valid,
    invalid_nonfinite,
    invalid_N_zero,
    invalid_D_zero,
    not_applicable,
};

enum class DomainStatus {
    core_eligible,
    valid_outside_approved_domain,
    unknown,
    not_applicable,
};

enum class EvidenceStatus { passed, failed, blocked };
enum class NnVerdict {
    accepted_unambiguous,
    accepted_ambiguous,
    failed,
    blocked_unverified,
    not_applicable,
};
enum class ContenderProvenance { exact_resolved, conservative_outward, not_applicable };

struct DatasetView {
    const float* data = nullptr;
    std::uint64_t candidate_count = 0;
    std::uint64_t dimension = 0;
    std::size_t element_count = 0;
};

struct QueryView {
    const float* data = nullptr;
    std::uint64_t dimension = 0;
    std::size_t element_count = 0;
};

struct SearchResult {
    std::uint64_t index = 0;
    float squared_distance = 0.0F;
};

struct ProductionOutcome {
    InvocationStatus invocation_status = InvocationStatus::not_executed;
    InputOutcome input_outcome = InputOutcome::not_applicable;
    DomainStatus domain_status = DomainStatus::not_applicable;
    std::optional<SearchResult> result;
    std::string backend_detail;
};

struct Float64Reference {
    InvocationStatus invocation_status = InvocationStatus::not_executed;
    InputOutcome input_outcome = InputOutcome::not_applicable;
    DomainStatus domain_status = DomainStatus::not_applicable;
    std::vector<double> distances;
    std::optional<std::uint64_t> argmin_index;
    std::vector<std::uint64_t> tied_argmin_indices;
    std::string backend_detail;
};

struct ArithmeticEnvironment {
    bool round_to_nearest_even = false;
    bool ftz_disabled = false;
    bool daz_disabled = false;
    bool fused_fma_probe = false;
    bool sequential_order_probe = false;
    bool conforming() const noexcept;
};

struct Interval {
    double lower = 0.0;
    double upper = 0.0;
};

struct BoundEvaluation {
    EvidenceStatus status = EvidenceStatus::blocked;
    Interval observed_error;
    Interval allowed_bound;
    bool exact_zero_required = false;
    bool exact_zero_satisfied = false;
};

struct RankingCandidate {
    Interval radius;
    Interval lower_endpoint;
    Interval upper_endpoint;
    bool confirmed_contender = false;
    bool confirmed_non_contender = false;
    bool boundary_uncertain = false;
};

struct RankingEvaluation {
    EvidenceStatus status = EvidenceStatus::blocked;
    Interval minimum_upper_endpoint;
    std::vector<RankingCandidate> candidates;
    std::vector<std::uint64_t> exact_contenders;
    std::vector<std::uint64_t> conservative_contenders;
    std::vector<std::uint64_t> boundary_uncertain_indices;
    bool exact_membership_resolved = false;
};

struct ValidationOptions {
    bool require_core_domain = true;
    bool graph_evidence_available = true;
    bool arithmetic_environment_evidence_available = true;
    bool candidate_evidence_complete = true;
    bool ranking_evidence_available = true;
};

struct ValidationReport {
    InputOutcome input_outcome = InputOutcome::not_applicable;
    DomainStatus domain_status = DomainStatus::not_applicable;
    NnVerdict nn_verdict = NnVerdict::not_applicable;
    ContenderProvenance contender_provenance = ContenderProvenance::not_applicable;
    EvidenceStatus graph_status = EvidenceStatus::blocked;
    EvidenceStatus arithmetic_environment_status = EvidenceStatus::blocked;
    EvidenceStatus candidate_coverage_status = EvidenceStatus::blocked;
    EvidenceStatus production_match_status = EvidenceStatus::blocked;
    EvidenceStatus reference_match_status = EvidenceStatus::blocked;
    EvidenceStatus ud001_status = EvidenceStatus::blocked;
    EvidenceStatus ud002_status = EvidenceStatus::blocked;
    bool exact_ambiguity_known = false;
    bool exact_ambiguity = false;
    bool float64_index_agrees = false;
    std::vector<float> independently_computed_float32_distances;
    std::vector<double> independently_computed_float64_distances;
    std::vector<BoundEvaluation> bound_evidence;
    RankingEvaluation ranking;
    std::string decision_trace;
};

ArithmeticEnvironment inspect_arithmetic_environment() noexcept;
ProductionOutcome cpu_search_float32(DatasetView dataset, QueryView query);
Float64Reference compute_float64_reference(DatasetView dataset, QueryView query);
BoundEvaluation evaluate_ud001(std::uint64_t dimension, float distance32,
                               double distance64, bool exact_zero_expected);
RankingEvaluation evaluate_ud002(std::uint64_t dimension,
                                 const std::vector<double>& distances64);
ValidationReport validate_cpu_search(DatasetView dataset, QueryView query,
                                     const ProductionOutcome& production,
                                     const Float64Reference& reference,
                                     const ValidationOptions& options = {});

}  // namespace s6
