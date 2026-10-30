#include "s6/cpu_baseline.hpp"
#include "s6/cpu_evidence.hpp"

#include "cpu_evidence_internal.hpp"

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#if defined(_MSC_VER)
#include <intrin.h>
#include <xmmintrin.h>
#pragma float_control(precise, on, push)
#pragma fenv_access(on)
#endif

namespace s6 {
namespace {

constexpr std::uint64_t kMaximumPolicyDimension = (std::uint64_t{1} << 24) - 1;

struct InputCheck {
    InvocationStatus invocation = InvocationStatus::not_executed;
    InputOutcome input = InputOutcome::not_applicable;
    DomainStatus domain = DomainStatus::not_applicable;
    std::string detail;
};

InputCheck inspect_input(DatasetView dataset, QueryView query) {
    if (dataset.candidate_count == 0) {
        return {InvocationStatus::contract_invalid, InputOutcome::invalid_N_zero,
                DomainStatus::not_applicable, "N is zero"};
    }
    if (dataset.dimension == 0) {
        return {InvocationStatus::contract_invalid, InputOutcome::invalid_D_zero,
                DomainStatus::not_applicable, "D is zero"};
    }
    if (query.dimension != dataset.dimension) {
        return {InvocationStatus::api_misuse, InputOutcome::not_applicable,
                DomainStatus::not_applicable, "query and dataset dimensions differ"};
    }
    if (dataset.data == nullptr || query.data == nullptr) {
        return {InvocationStatus::api_misuse, InputOutcome::not_applicable,
                DomainStatus::not_applicable, "null data view"};
    }
    if (dataset.dimension >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return {InvocationStatus::api_misuse, InputOutcome::not_applicable,
                DomainStatus::not_applicable, "dimension is not addressable"};
    }
    const auto dimension = static_cast<std::size_t>(dataset.dimension);
    if (dataset.candidate_count >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() / dimension)) {
        return {InvocationStatus::api_misuse, InputOutcome::not_applicable,
                DomainStatus::not_applicable, "dataset element-count overflow"};
    }
    const auto expected = static_cast<std::size_t>(dataset.candidate_count) * dimension;
    if (dataset.element_count != expected || query.element_count != dimension) {
        return {InvocationStatus::api_misuse, InputOutcome::not_applicable,
                DomainStatus::not_applicable, "inconsistent view element count"};
    }
    for (std::size_t i = 0; i < dataset.element_count; ++i) {
        if (!std::isfinite(dataset.data[i])) {
            return {InvocationStatus::contract_invalid, InputOutcome::invalid_nonfinite,
                    DomainStatus::not_applicable, "dataset contains NaN or infinity"};
        }
    }
    for (std::size_t j = 0; j < query.element_count; ++j) {
        if (!std::isfinite(query.data[j])) {
            return {InvocationStatus::contract_invalid, InputOutcome::invalid_nonfinite,
                    DomainStatus::not_applicable, "query contains NaN or infinity"};
        }
    }
    const auto domain = dataset.dimension <= kMaximumPolicyDimension
                            ? DomainStatus::core_eligible
                            : DomainStatus::valid_outside_approved_domain;
    return {InvocationStatus::completed, InputOutcome::valid, domain, {}};
}

#if defined(_MSC_VER)
#define S6_NOINLINE __declspec(noinline)
#else
#define S6_NOINLINE __attribute__((noinline))
#endif

S6_NOINLINE float production_distance(const float* candidate, const float* query,
                                      std::size_t dimension) noexcept {
    float accumulator = 0.0F;
    for (std::size_t j = 0; j < dimension; ++j) {
        volatile float rounded_difference = candidate[j] - query[j];
        accumulator = std::fma(rounded_difference, rounded_difference, accumulator);
    }
    return accumulator;
}

S6_NOINLINE float validator_distance(const float* candidate, const float* query,
                                     std::size_t dimension) noexcept {
    float running_sum = 0.0F;
    for (std::size_t coordinate = 0; coordinate != dimension; ++coordinate) {
        const float left = candidate[coordinate];
        const float right = query[coordinate];
        volatile float delta32 = left - right;
        const float prior = running_sum;
        running_sum = std::fma(delta32, delta32, prior);
    }
    return running_sum;
}

S6_NOINLINE double reference_distance(const float* candidate, const float* query,
                                      std::size_t dimension) noexcept {
    double accumulator = 0.0;
    for (std::size_t j = 0; j < dimension; ++j) {
        const double x = static_cast<double>(candidate[j]);
        const double q = static_cast<double>(query[j]);
        volatile double rounded_difference = x - q;
        accumulator = std::fma(rounded_difference, rounded_difference, accumulator);
    }
    return accumulator;
}

S6_NOINLINE double validator_reference_distance(const float* candidate, const float* query,
                                                std::size_t dimension) noexcept {
    double running_sum = 0.0;
    for (std::size_t coordinate = 0; coordinate != dimension; ++coordinate) {
        const double promoted_candidate = static_cast<double>(candidate[coordinate]);
        const double promoted_query = static_cast<double>(query[coordinate]);
        volatile double delta64 = promoted_candidate - promoted_query;
        const double previous = running_sum;
        running_sum = std::fma(delta64, delta64, previous);
    }
    return running_sum;
}

std::uint32_t bits_of(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::uint64_t bits_of(double value) noexcept {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double down(double value) noexcept {
    return std::nextafter(value, -std::numeric_limits<double>::infinity());
}

double up(double value) noexcept {
    return std::nextafter(value, std::numeric_limits<double>::infinity());
}

Interval add(Interval lhs, Interval rhs) noexcept {
    return {down(lhs.lower + rhs.lower), up(lhs.upper + rhs.upper)};
}

Interval subtract(Interval lhs, Interval rhs) noexcept {
    return {down(lhs.lower - rhs.upper), up(lhs.upper - rhs.lower)};
}

Interval multiply(Interval lhs, Interval rhs) noexcept {
    const double products[] = {lhs.lower * rhs.lower, lhs.lower * rhs.upper,
                               lhs.upper * rhs.lower, lhs.upper * rhs.upper};
    const auto limits = std::minmax_element(std::begin(products), std::end(products));
    return {down(*limits.first), up(*limits.second)};
}

Interval divide_positive(Interval numerator, Interval denominator) noexcept {
    if (!(denominator.lower > 0.0)) {
        return {-std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity()};
    }
    return {down(numerator.lower / denominator.upper),
            up(numerator.upper / denominator.lower)};
}

Interval absolute(Interval value) noexcept {
    if (value.lower >= 0.0) {
        return value;
    }
    if (value.upper <= 0.0) {
        return {-value.upper, -value.lower};
    }
    return {0.0, std::max(-value.lower, value.upper)};
}

NumericalPolicyEvaluation policy_intervals(std::uint64_t dimension) noexcept {
    const Interval one{1.0, 1.0};
    const Interval u32{std::ldexp(1.0, -24), std::ldexp(1.0, -24)};
    const Interval u64{std::ldexp(1.0, -53), std::ldexp(1.0, -53)};
    const Interval eta32{std::ldexp(1.0, -150), std::ldexp(1.0, -150)};
    const double d = static_cast<double>(dimension);
    const Interval dimensions{d, d};

    const auto du32 = multiply(dimensions, u32);
    const auto du64 = multiply(dimensions, u64);
    const auto gamma32 = divide_positive(du32, subtract(one, du32));
    const auto gamma64 = divide_positive(du64, subtract(one, du64));
    const auto one_plus_u32 = add(one, u32);
    const auto one_plus_u64 = add(one, u64);
    const auto e32 = subtract(multiply(multiply(one_plus_u32, one_plus_u32),
                                         add(one, gamma32)),
                              one);
    const auto e64 = subtract(multiply(multiply(one_plus_u64, one_plus_u64),
                                         add(one, gamma64)),
                              one);
    auto relative = divide_positive(add(e32, e64), subtract(one, e64));
    auto additive = multiply(multiply(dimensions, eta32), add(one, gamma32));
    additive.lower = std::max(0.0, additive.lower);
    relative.lower = std::max(0.0, relative.lower);
    return {gamma32, gamma64, e32, e64, additive, relative};
}

bool same_float(float lhs, float rhs) noexcept { return bits_of(lhs) == bits_of(rhs); }
bool same_double(double lhs, double rhs) noexcept { return bits_of(lhs) == bits_of(rhs); }

}  // namespace

std::uintptr_t detail::production_distance_function_address() noexcept {
    return reinterpret_cast<std::uintptr_t>(&production_distance);
}

std::uintptr_t detail::validator_distance_function_address() noexcept {
    return reinterpret_cast<std::uintptr_t>(&validator_distance);
}

std::uintptr_t detail::reference_distance_function_address() noexcept {
    return reinterpret_cast<std::uintptr_t>(&reference_distance);
}

std::uintptr_t detail::validator_reference_distance_function_address() noexcept {
    return reinterpret_cast<std::uintptr_t>(&validator_reference_distance);
}

bool ArithmeticEnvironment::conforming_float32() const noexcept {
    return round_to_nearest_even && ftz_disabled && daz_disabled && fused_fma_probe &&
           sequential_order_probe;
}

bool ArithmeticEnvironment::conforming_float64() const noexcept {
    return round_to_nearest_even && ftz_disabled && daz_disabled &&
           float64_fused_fma_probe && float64_sequential_order_probe;
}

bool ArithmeticEnvironment::conforming() const noexcept {
    return conforming_float32() && conforming_float64();
}

ArithmeticEnvironment inspect_arithmetic_environment() noexcept {
    ArithmeticEnvironment result;
    result.round_to_nearest_even = std::fegetround() == FE_TONEAREST;
#if defined(_MSC_VER) || defined(__SSE__)
    const unsigned int mxcsr = _mm_getcsr();
    result.ftz_disabled = (mxcsr & (1U << 15U)) == 0;
    result.daz_disabled = (mxcsr & (1U << 6U)) == 0;
#else
    result.ftz_disabled = true;
    result.daz_disabled = true;
#endif
    volatile float a = 1.0F + std::ldexp(1.0F, -23);
    volatile float b = 1.0F - std::ldexp(1.0F, -23);
    volatile float c = -1.0F;
    const float fused = std::fma(a, b, c);
    result.fused_fma_probe = bits_of(fused) == 0xa8800000U;

    float sequential = 0.0F;
    sequential = std::fma(4096.0F, 4096.0F, sequential);
    sequential = std::fma(1.0F, 1.0F, sequential);
    sequential = std::fma(1.0F, 1.0F, sequential);
    result.sequential_order_probe = bits_of(sequential) == 0x4b800000U;

    volatile double da = 1.0 + std::ldexp(1.0, -52);
    volatile double db = 1.0 - std::ldexp(1.0, -52);
    volatile double dc = -1.0;
    const double fused64 = std::fma(da, db, dc);
    result.float64_fused_fma_probe =
        bits_of(fused64) == bits_of(-std::ldexp(1.0, -104));
    double sequential64 = 0.0;
    sequential64 = std::fma(134217728.0, 134217728.0, sequential64);
    sequential64 = std::fma(1.0, 1.0, sequential64);
    sequential64 = std::fma(1.0, 1.0, sequential64);
    result.float64_sequential_order_probe =
        bits_of(sequential64) == bits_of(std::ldexp(1.0, 54));
    return result;
}

ProductionOutcome detail::cpu_search_float32_impl(DatasetView dataset, QueryView query,
                                                  CandidateVisitObserver* observer) {
    ProductionOutcome outcome;
    const auto input = inspect_input(dataset, query);
    outcome.invocation_status = input.invocation;
    outcome.input_outcome = input.input;
    outcome.domain_status = input.domain;
    outcome.backend_detail = input.detail;
    if (input.invocation != InvocationStatus::completed) {
        return outcome;
    }
    if (!inspect_arithmetic_environment().conforming_float32()) {
        outcome.invocation_status = InvocationStatus::backend_failure;
        outcome.domain_status = DomainStatus::unknown;
        outcome.backend_detail = "nonconforming Float32 arithmetic environment";
        return outcome;
    }

    const auto dimension = static_cast<std::size_t>(dataset.dimension);
    try {
        bool have_best = false;
        SearchResult best{};
        for (std::uint64_t i = 0; i < dataset.candidate_count; ++i) {
            if (observer != nullptr) {
                observer->observe(i);
            }
            const auto offset = static_cast<std::size_t>(i) * dimension;
            const float distance = production_distance(dataset.data + offset, query.data,
                                                       dimension);
            if (!std::isfinite(distance)) {
                outcome.invocation_status = InvocationStatus::backend_failure;
                outcome.domain_status = DomainStatus::valid_outside_approved_domain;
                outcome.backend_detail = "nonfinite Float32 candidate distance";
                outcome.result.reset();
                if (observer != nullptr) {
                    observer->finish(false);
                }
                return outcome;
            }
            if (!have_best || distance < best.squared_distance ||
                (distance == best.squared_distance && i < best.index)) {
                best = {i, distance};
                have_best = true;
            }
        }
        outcome.result = best;
        outcome.invocation_status = InvocationStatus::completed;
        outcome.backend_detail.clear();
        if (observer != nullptr) {
            observer->finish(true);
        }
    } catch (const std::bad_alloc&) {
        outcome.invocation_status = InvocationStatus::resource_failure;
        outcome.domain_status = DomainStatus::unknown;
        outcome.result.reset();
        outcome.backend_detail = "allocation failure";
        if (observer != nullptr) {
            observer->finish(false);
        }
    }
    return outcome;
}

std::uintptr_t detail::cpu_search_float32_impl_function_address() noexcept {
    return reinterpret_cast<std::uintptr_t>(&cpu_search_float32_impl);
}

ProductionOutcome cpu_search_float32(DatasetView dataset, QueryView query) {
    return detail::cpu_search_float32_impl(dataset, query, nullptr);
}

Float64Reference compute_float64_reference(DatasetView dataset, QueryView query) {
    Float64Reference outcome;
    const auto input = inspect_input(dataset, query);
    outcome.invocation_status = input.invocation;
    outcome.input_outcome = input.input;
    outcome.domain_status = input.domain;
    outcome.backend_detail = input.detail;
    if (input.invocation != InvocationStatus::completed) {
        return outcome;
    }
    if (!inspect_arithmetic_environment().conforming_float64()) {
        outcome.invocation_status = InvocationStatus::backend_failure;
        outcome.domain_status = DomainStatus::unknown;
        outcome.backend_detail = "nonconforming Float64 arithmetic environment";
        return outcome;
    }

    const auto dimension = static_cast<std::size_t>(dataset.dimension);
    try {
        outcome.distances.resize(static_cast<std::size_t>(dataset.candidate_count));
        for (std::uint64_t i = 0; i < dataset.candidate_count; ++i) {
            const auto offset = static_cast<std::size_t>(i) * dimension;
            const double distance = reference_distance(dataset.data + offset, query.data,
                                                       dimension);
            outcome.distances[static_cast<std::size_t>(i)] = distance;
            if (!outcome.argmin_index ||
                distance < outcome.distances[static_cast<std::size_t>(*outcome.argmin_index)]) {
                outcome.argmin_index = i;
                outcome.tied_argmin_indices.assign(1, i);
            } else if (distance ==
                       outcome.distances[static_cast<std::size_t>(*outcome.argmin_index)]) {
                outcome.tied_argmin_indices.push_back(i);
            }
        }
        outcome.invocation_status = InvocationStatus::completed;
        outcome.backend_detail.clear();
    } catch (const std::bad_alloc&) {
        outcome.invocation_status = InvocationStatus::resource_failure;
        outcome.domain_status = DomainStatus::unknown;
        outcome.distances.clear();
        outcome.argmin_index.reset();
        outcome.tied_argmin_indices.clear();
        outcome.backend_detail = "allocation failure";
    }
    return outcome;
}

BoundEvaluation evaluate_ud001(std::uint64_t dimension, float distance32,
                               double distance64, bool exact_zero_expected) {
    BoundEvaluation evaluation;
    evaluation.exact_zero_required = exact_zero_expected;
    evaluation.exact_zero_satisfied = !exact_zero_expected ||
                                      (bits_of(distance32) == 0U && bits_of(distance64) == 0U);
    if (dimension == 0 || dimension > kMaximumPolicyDimension ||
        !std::isfinite(distance32) || !std::isfinite(distance64) || distance32 < 0.0F ||
        distance64 < 0.0) {
        evaluation.status = EvidenceStatus::blocked;
        return evaluation;
    }

    const auto policy = policy_intervals(dimension);
    const Interval d32{static_cast<double>(distance32), static_cast<double>(distance32)};
    const Interval d64{distance64, distance64};
    evaluation.observed_error = absolute(subtract(d32, d64));
    evaluation.allowed_bound = add(policy.additive, multiply(policy.relative, d64));
    evaluation.allowed_bound.lower = std::max(0.0, evaluation.allowed_bound.lower);

    if (!evaluation.exact_zero_satisfied) {
        evaluation.status = EvidenceStatus::failed;
    } else if (evaluation.observed_error.upper <= evaluation.allowed_bound.lower) {
        evaluation.status = EvidenceStatus::passed;
    } else if (evaluation.observed_error.lower > evaluation.allowed_bound.upper) {
        evaluation.status = EvidenceStatus::failed;
    } else {
        evaluation.status = EvidenceStatus::blocked;
    }
    return evaluation;
}

RankingEvaluation evaluate_ud002(std::uint64_t dimension,
                                 const std::vector<double>& distances64) {
    RankingEvaluation evaluation;
    if (dimension == 0 || dimension > kMaximumPolicyDimension || distances64.empty()) {
        return evaluation;
    }
    const auto policy = policy_intervals(dimension);
    evaluation.candidates.resize(distances64.size());
    evaluation.minimum_upper_endpoint = {std::numeric_limits<double>::infinity(),
                                         std::numeric_limits<double>::infinity()};
    for (std::size_t i = 0; i < distances64.size(); ++i) {
        const double distance = distances64[i];
        if (!std::isfinite(distance) || distance < 0.0) {
            return evaluation;
        }
        const Interval exact_distance{distance, distance};
        auto& candidate = evaluation.candidates[i];
        candidate.radius = add(policy.additive, multiply(policy.relative, exact_distance));
        candidate.radius.lower = std::max(0.0, candidate.radius.lower);
        candidate.lower_endpoint = subtract(exact_distance, candidate.radius);
        candidate.upper_endpoint = add(exact_distance, candidate.radius);
        evaluation.minimum_upper_endpoint.lower =
            std::min(evaluation.minimum_upper_endpoint.lower, candidate.upper_endpoint.lower);
        evaluation.minimum_upper_endpoint.upper =
            std::min(evaluation.minimum_upper_endpoint.upper, candidate.upper_endpoint.upper);
    }

    for (std::size_t i = 0; i < evaluation.candidates.size(); ++i) {
        auto& candidate = evaluation.candidates[i];
        candidate.confirmed_contender = candidate.lower_endpoint.upper <=
                                        evaluation.minimum_upper_endpoint.lower;
        candidate.confirmed_non_contender = candidate.lower_endpoint.lower >
                                            evaluation.minimum_upper_endpoint.upper;
        candidate.boundary_uncertain =
            !candidate.confirmed_contender && !candidate.confirmed_non_contender;
        if (candidate.confirmed_contender) {
            evaluation.exact_contenders.push_back(static_cast<std::uint64_t>(i));
        }
        if (candidate.lower_endpoint.lower <= evaluation.minimum_upper_endpoint.upper) {
            evaluation.conservative_contenders.push_back(static_cast<std::uint64_t>(i));
        }
        if (candidate.boundary_uncertain) {
            evaluation.boundary_uncertain_indices.push_back(static_cast<std::uint64_t>(i));
        }
    }
    evaluation.exact_membership_resolved = evaluation.boundary_uncertain_indices.empty();
    if (!evaluation.exact_membership_resolved) {
        evaluation.exact_contenders.clear();
    }
    evaluation.status = EvidenceStatus::passed;
    return evaluation;
}

ValidationReport validate_cpu_search(DatasetView dataset, QueryView query,
                                     const ProductionOutcome& production,
                                     const Float64Reference& reference,
                                     const ValidationOptions& options) {
    ValidationReport report;
    const auto input = inspect_input(dataset, query);
    report.input_outcome = input.input;
    report.domain_status = input.domain;

    if (input.invocation != InvocationStatus::completed) {
        const bool expected = production.invocation_status == input.invocation &&
                              production.input_outcome == input.input &&
                              !production.result.has_value();
        report.production_match_status = expected ? EvidenceStatus::passed
                                                  : EvidenceStatus::failed;
        report.reference_match_status =
            (reference.invocation_status == input.invocation &&
             reference.input_outcome == input.input)
                ? EvidenceStatus::passed
                : EvidenceStatus::failed;
        report.nn_verdict = NnVerdict::not_applicable;
        report.decision_trace = expected ? "contract-invalid: expected rejection"
                                         : "contract-invalid: rejection mismatch";
        return report;
    }

    const auto environment = inspect_arithmetic_environment();
    const DomainStatus independently_derived_domain =
        dataset.dimension <= kMaximumPolicyDimension
            ? DomainStatus::core_eligible
            : DomainStatus::valid_outside_approved_domain;
    if (options.production_evidence != nullptr &&
        options.production_evidence->identity_status() == EvidenceStatus::blocked) {
        report.evidence_identity_status = EvidenceStatus::blocked;
        report.evidence_identity = options.production_evidence->identity();
        report.graph_status = EvidenceStatus::blocked;
        report.graph_provenance = EvidenceProvenance::unavailable;
        report.production_candidate_coverage_status = EvidenceStatus::blocked;
        report.production_candidate_coverage_provenance =
            EvidenceProvenance::unavailable;
    } else if (options.production_evidence != nullptr &&
        options.production_evidence->matches(dataset, query, production)) {
        report.evidence_identity_status = EvidenceStatus::passed;
        report.evidence_identity = options.production_evidence->identity();
        report.graph_status = options.production_evidence->graph_status();
        report.numerical_paths.production_float32_graph =
            options.production_evidence->production_float32_graph_status();
        report.numerical_paths.validator_float32_graph =
            options.production_evidence->validator_float32_graph_status();
        report.numerical_paths.reference_float64_graph =
            options.production_evidence->reference_float64_graph_status();
        report.numerical_paths.validator_float64_graph =
            options.production_evidence->validator_float64_graph_status();
        report.graph_provenance = EvidenceProvenance::verified_build_artifact;
        report.production_candidate_coverage_status =
            options.production_evidence->coverage_status();
        report.production_candidate_coverage_provenance =
            EvidenceProvenance::production_instrumentation;
    } else if (options.production_evidence != nullptr) {
        report.evidence_identity_status = EvidenceStatus::failed;
        report.evidence_identity = options.production_evidence->identity();
        report.graph_status = EvidenceStatus::blocked;
        report.graph_provenance = EvidenceProvenance::unavailable;
        report.production_candidate_coverage_status = EvidenceStatus::blocked;
        report.production_candidate_coverage_provenance =
            EvidenceProvenance::unavailable;
    } else {
        report.evidence_identity_status = EvidenceStatus::blocked;
        report.graph_provenance = options.graph_evidence_available
                                      ? EvidenceProvenance::external_attestation
                                      : EvidenceProvenance::unavailable;
        // Source spelling, matching outputs, or a caller assertion cannot establish
        // independent compiler/runtime graph conformance.
        report.graph_status = EvidenceStatus::blocked;
        report.production_candidate_coverage_provenance =
            options.candidate_evidence_complete
                ? EvidenceProvenance::external_attestation
                : EvidenceProvenance::unavailable;
        report.production_candidate_coverage_status = EvidenceStatus::blocked;
    }
    if (options.arithmetic_environment_evidence_available) {
        const auto float32_environment = environment.conforming_float32()
                                             ? EvidenceStatus::passed
                                             : EvidenceStatus::failed;
        const auto float64_environment = environment.conforming_float64()
                                             ? EvidenceStatus::passed
                                             : EvidenceStatus::failed;
        report.numerical_paths.production_float32_environment = float32_environment;
        report.numerical_paths.validator_float32_environment = float32_environment;
        report.numerical_paths.reference_float64_environment = float64_environment;
        report.numerical_paths.validator_float64_environment = float64_environment;
        report.arithmetic_environment_status =
            float32_environment == EvidenceStatus::passed &&
                    float64_environment == EvidenceStatus::passed
                ? EvidenceStatus::passed
                : EvidenceStatus::failed;
    } else {
        report.arithmetic_environment_status = EvidenceStatus::blocked;
    }
    report.arithmetic_environment_provenance =
        options.arithmetic_environment_evidence_available
            ? EvidenceProvenance::internally_observed
            : EvidenceProvenance::unavailable;
    report.candidate_coverage_provenance = EvidenceProvenance::internally_observed;

    if (options.mandatory_gate_evidence != nullptr &&
        options.production_evidence != nullptr &&
        options.mandatory_gate_evidence->source_identity ==
            options.production_evidence->source_identity()) {
        report.fixture_status = options.mandatory_gate_evidence->fixture_status;
        report.oracle_status = options.mandatory_gate_evidence->oracle_status;
        report.tie_status = options.mandatory_gate_evidence->tie_status;
        report.reduction_status = options.mandatory_gate_evidence->reduction_status;
        report.fixture_ids = options.mandatory_gate_evidence->fixture_ids;
    } else if (options.mandatory_gate_evidence != nullptr) {
        report.fixture_status = EvidenceStatus::failed;
        report.oracle_status = EvidenceStatus::failed;
        report.tie_status = EvidenceStatus::failed;
        report.reduction_status = EvidenceStatus::failed;
        report.fixture_ids = options.mandatory_gate_evidence->fixture_ids;
    }

    const auto dimension = static_cast<std::size_t>(dataset.dimension);
    const auto count = static_cast<std::size_t>(dataset.candidate_count);
    report.numerical_policy = policy_intervals(dataset.dimension);
    try {
        report.independently_computed_float32_distances.resize(count);
        report.independently_computed_float64_distances.resize(count);
        report.bound_evidence.reserve(count);
    } catch (const std::bad_alloc&) {
        report.nn_verdict = NnVerdict::blocked_unverified;
        report.decision_trace = "missing evidence: validator allocation failure";
        return report;
    }

    std::uint64_t independent_index = 0;
    float independent_minimum = 0.0F;
    bool have_minimum = false;
    std::uint64_t independent_reference_index = 0;
    double independent_reference_minimum = 0.0;
    bool have_reference_minimum = false;
    std::vector<std::uint64_t> independent_reference_ties;
    bool validator_nonfinite = false;
    for (std::size_t i = 0; i < count; ++i) {
        const auto offset = i * dimension;
        const float distance32 = validator_distance(dataset.data + offset, query.data, dimension);
        const double distance64 =
            validator_reference_distance(dataset.data + offset, query.data, dimension);
        report.independently_computed_float32_distances[i] = distance32;
        report.independently_computed_float64_distances[i] = distance64;
        validator_nonfinite = validator_nonfinite || !std::isfinite(distance32) ||
                              !std::isfinite(distance64);
        if (std::isfinite(distance32) &&
            (!have_minimum || distance32 < independent_minimum ||
             (distance32 == independent_minimum && i < independent_index))) {
            independent_minimum = distance32;
            independent_index = static_cast<std::uint64_t>(i);
            have_minimum = true;
        }
        if (std::isfinite(distance64) &&
            (!have_reference_minimum || distance64 < independent_reference_minimum)) {
            independent_reference_minimum = distance64;
            independent_reference_index = static_cast<std::uint64_t>(i);
            independent_reference_ties.assign(1, independent_reference_index);
            have_reference_minimum = true;
        } else if (have_reference_minimum && distance64 == independent_reference_minimum) {
            independent_reference_ties.push_back(static_cast<std::uint64_t>(i));
        }
        bool exact_match = true;
        for (std::size_t j = 0; j < dimension; ++j) {
            if (dataset.data[offset + j] != query.data[j]) {
                exact_match = false;
                break;
            }
        }
        report.bound_evidence.push_back(
            evaluate_ud001(dataset.dimension, distance32, distance64, exact_match));
        ++report.independently_observed_candidate_count;
    }

    report.candidate_coverage_status =
        report.independently_observed_candidate_count == dataset.candidate_count &&
                report.independently_computed_float32_distances.size() == count &&
                report.independently_computed_float64_distances.size() == count &&
                report.bound_evidence.size() == count
            ? EvidenceStatus::passed
            : EvidenceStatus::failed;
    if (have_minimum) {
        report.independently_computed_float32_argmin_index = independent_index;
    }

    if (production.domain_status == DomainStatus::unknown ||
        production.domain_status == DomainStatus::not_applicable) {
        report.domain_status = DomainStatus::unknown;
        report.domain_consistency_status = EvidenceStatus::blocked;
    } else if (production.domain_status == independently_derived_domain) {
        report.domain_status = independently_derived_domain;
        report.domain_consistency_status = EvidenceStatus::passed;
    } else if (validator_nonfinite &&
               production.domain_status == DomainStatus::valid_outside_approved_domain) {
        report.domain_status = DomainStatus::valid_outside_approved_domain;
        report.domain_consistency_status = EvidenceStatus::passed;
    } else {
        report.domain_status = DomainStatus::unknown;
        report.domain_consistency_status = EvidenceStatus::failed;
    }

    report.production_match_status =
        production.invocation_status == InvocationStatus::completed &&
                production.input_outcome == InputOutcome::valid && production.result &&
                have_minimum && production.result->index == independent_index &&
                same_float(production.result->squared_distance, independent_minimum)
            ? EvidenceStatus::passed
            : EvidenceStatus::failed;

    bool reference_matches = reference.invocation_status == InvocationStatus::completed &&
                             reference.input_outcome == InputOutcome::valid &&
                             reference.domain_status == independently_derived_domain &&
                             reference.distances.size() == count && reference.argmin_index &&
                             have_reference_minimum &&
                             *reference.argmin_index == independent_reference_index &&
                             reference.tied_argmin_indices == independent_reference_ties;
    if (reference_matches) {
        for (std::size_t i = 0; i < count; ++i) {
            if (!same_double(reference.distances[i],
                             report.independently_computed_float64_distances[i])) {
                reference_matches = false;
                break;
            }
        }
    }
    report.reference_match_status = reference_matches ? EvidenceStatus::passed
                                                      : EvidenceStatus::failed;

    report.ud001_status = EvidenceStatus::passed;
    for (const auto& evidence : report.bound_evidence) {
        if (evidence.status == EvidenceStatus::failed) {
            report.ud001_status = EvidenceStatus::failed;
            break;
        }
        if (evidence.status == EvidenceStatus::blocked) {
            report.ud001_status = EvidenceStatus::blocked;
        }
    }

    if (options.ranking_evidence_available) {
        report.ranking_provenance = EvidenceProvenance::internally_observed;
        report.ranking = evaluate_ud002(dataset.dimension,
                                        report.independently_computed_float64_distances);
        report.ud002_status = report.ranking.status;
    } else {
        report.ranking_provenance = EvidenceProvenance::unavailable;
        report.ud002_status = EvidenceStatus::blocked;
    }

    if (production.result && have_reference_minimum) {
        report.float64_index_agrees =
            production.result->index == independent_reference_index;
    }

    const auto contains_index = [](const std::vector<std::uint64_t>& values,
                                   std::uint64_t index) {
        return std::find(values.begin(), values.end(), index) != values.end();
    };
    const bool returned_in_exact = production.result && report.ranking.exact_membership_resolved &&
                                   contains_index(report.ranking.exact_contenders,
                                                  production.result->index);
    const bool returned_in_conservative =
        production.result && contains_index(report.ranking.conservative_contenders,
                                            production.result->index);

    if (report.ud002_status == EvidenceStatus::passed) {
        if (report.ranking.exact_membership_resolved) {
            report.contender_provenance = ContenderProvenance::exact_resolved;
            report.exact_ambiguity_known = true;
            report.exact_ambiguity = report.ranking.exact_contenders.size() > 1;
        } else {
            report.contender_provenance = ContenderProvenance::conservative_outward;
            report.exact_ambiguity_known = false;
        }
    }

    const bool demonstrated_failure =
        validator_nonfinite ||
        report.evidence_identity_status == EvidenceStatus::failed ||
        report.graph_status == EvidenceStatus::failed ||
        report.domain_consistency_status == EvidenceStatus::failed ||
        report.arithmetic_environment_status == EvidenceStatus::failed ||
        report.candidate_coverage_status == EvidenceStatus::failed ||
        report.production_candidate_coverage_status == EvidenceStatus::failed ||
        report.production_match_status == EvidenceStatus::failed ||
        report.reference_match_status == EvidenceStatus::failed ||
        report.ud001_status == EvidenceStatus::failed ||
        report.ud002_status == EvidenceStatus::failed ||
        report.fixture_status == EvidenceStatus::failed ||
        report.oracle_status == EvidenceStatus::failed ||
        report.tie_status == EvidenceStatus::failed ||
        report.reduction_status == EvidenceStatus::failed ||
        (report.ranking.exact_membership_resolved && production.result && !returned_in_exact) ||
        (!report.ranking.exact_membership_resolved && production.result &&
         report.ud002_status == EvidenceStatus::passed && !returned_in_conservative);

    if (demonstrated_failure) {
        report.nn_verdict = NnVerdict::failed;
        report.decision_trace = "precedence 1-4: demonstrated defect";
        return report;
    }

    const bool missing_evidence =
        report.domain_consistency_status == EvidenceStatus::blocked ||
        report.graph_status == EvidenceStatus::blocked ||
        report.arithmetic_environment_status == EvidenceStatus::blocked ||
        report.candidate_coverage_status == EvidenceStatus::blocked ||
        report.evidence_identity_status != EvidenceStatus::passed ||
        report.production_candidate_coverage_status == EvidenceStatus::blocked ||
        report.ud001_status == EvidenceStatus::blocked ||
        report.ud002_status == EvidenceStatus::blocked || !production.result ||
        report.fixture_status == EvidenceStatus::blocked ||
        report.oracle_status == EvidenceStatus::blocked ||
        report.tie_status == EvidenceStatus::blocked ||
        report.reduction_status == EvidenceStatus::blocked ||
        !reference.argmin_index ||
        (options.require_core_domain &&
         report.domain_status != DomainStatus::core_eligible);
    if (missing_evidence) {
        report.nn_verdict = NnVerdict::blocked_unverified;
        if (options.require_core_domain &&
            report.domain_status == DomainStatus::valid_outside_approved_domain) {
            report.decision_trace =
                "precedence 6: valid outside approved numerical domain";
        } else if (report.domain_status == DomainStatus::unknown) {
            report.decision_trace = "precedence 5: domain eligibility unknown";
        } else if (report.graph_status == EvidenceStatus::blocked) {
            report.decision_trace =
                "precedence 5: independent graph conformance evidence unavailable";
        } else if (report.production_candidate_coverage_status ==
                   EvidenceStatus::blocked) {
            report.decision_trace =
                "precedence 5: production-path candidate coverage evidence unavailable";
        } else {
            report.decision_trace = "precedence 5: mandatory evidence missing";
        }
        return report;
    }

    if (report.ranking.exact_membership_resolved) {
        if (report.ranking.exact_contenders.size() == 1 && report.float64_index_agrees) {
            report.nn_verdict = NnVerdict::accepted_unambiguous;
            report.decision_trace = "precedence 7: exact singleton and Float64 agreement";
        } else {
            report.nn_verdict = NnVerdict::accepted_ambiguous;
            report.decision_trace = "precedence 8: exact multi-candidate contender set";
        }
    } else {
        report.nn_verdict = NnVerdict::accepted_ambiguous;
        report.decision_trace =
            "precedence 9: conservative outward set; exact ambiguity unknown";
    }
    return report;
}

}  // namespace s6

#if defined(_MSC_VER)
#pragma float_control(pop)
#endif
