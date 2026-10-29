#include "s6/cpu_baseline.hpp"

#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_MSC_VER) || defined(__SSE__)
#include <xmmintrin.h>
#endif

namespace {

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

struct ExactDyadicSquare {
    std::uint64_t significand = 0;
    int exponent = 0;
};

ExactDyadicSquare exact_square_against_zero(float value) {
    const std::uint32_t raw = bits(value) & 0x7fffffffU;
    const std::uint32_t stored_exponent = raw >> 23U;
    const std::uint32_t fraction = raw & 0x7fffffU;
    if (stored_exponent == 0U) {
        const std::uint64_t significand = fraction;
        return {significand * significand, -298};
    }
    const std::uint64_t significand = (1U << 23U) | fraction;
    const int exponent = static_cast<int>(stored_exponent) - 127 - 23;
    return {significand * significand, 2 * exponent};
}

struct OwnedCase {
    std::vector<float> dataset;
    std::vector<float> query;
    std::uint64_t n = 0;
    std::uint64_t d = 0;

    s6::DatasetView dataset_view() const {
        return {dataset.data(), n, d, dataset.size()};
    }
    s6::QueryView query_view() const { return {query.data(), d, query.size()}; }
};

OwnedCase make_case(std::vector<float> dataset, std::vector<float> query) {
    const auto d = static_cast<std::uint64_t>(query.size());
    const auto n = static_cast<std::uint64_t>(dataset.size() / query.size());
    return {std::move(dataset), std::move(query), n, d};
}

s6::ValidationReport run_validated(const OwnedCase& input) {
    const auto production = s6::cpu_search_float32(input.dataset_view(), input.query_view());
    const auto reference =
        s6::compute_float64_reference(input.dataset_view(), input.query_view());
    return s6::validate_cpu_search(input.dataset_view(), input.query_view(), production,
                                   reference);
}

void test_input_and_shape_contract() {
    float value = 0.0F;
    const s6::QueryView one_query{&value, 1, 1};
    auto n_zero = s6::cpu_search_float32({&value, 0, 1, 0}, one_query);
    require(n_zero.invocation_status == s6::InvocationStatus::contract_invalid,
            "N=0 was not contract-invalid");
    require(n_zero.input_outcome == s6::InputOutcome::invalid_N_zero,
            "N=0 outcome mismatch");

    auto d_zero = s6::cpu_search_float32({&value, 1, 0, 0}, {&value, 0, 0});
    require(d_zero.input_outcome == s6::InputOutcome::invalid_D_zero,
            "D=0 outcome mismatch");

    auto null_view = s6::cpu_search_float32({nullptr, 1, 1, 1}, one_query);
    require(null_view.invocation_status == s6::InvocationStatus::api_misuse,
            "null view was not API misuse");
    auto inconsistent = s6::cpu_search_float32({&value, 1, 1, 2}, one_query);
    require(inconsistent.invocation_status == s6::InvocationStatus::api_misuse,
            "inconsistent size was not API misuse");
    auto inconsistent_dimension =
        s6::cpu_search_float32({&value, 1, 1, 1}, {&value, 2, 1});
    require(inconsistent_dimension.invocation_status == s6::InvocationStatus::api_misuse,
            "inconsistent dimension was not API misuse");

    const auto huge = std::numeric_limits<std::uint64_t>::max();
    auto overflow = s6::cpu_search_float32({&value, huge, 2, 0}, {&value, 2, 2});
    require(overflow.invocation_status == s6::InvocationStatus::api_misuse,
            "size overflow was not API misuse");
}

void test_nonfinite_rejection() {
    for (float bad : {std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity()}) {
        float query = 0.0F;
        auto dataset_bad = s6::cpu_search_float32({&bad, 1, 1, 1}, {&query, 1, 1});
        require(dataset_bad.input_outcome == s6::InputOutcome::invalid_nonfinite,
                "nonfinite dataset input was not rejected");
        auto query_bad = s6::cpu_search_float32({&query, 1, 1, 1}, {&bad, 1, 1});
        require(query_bad.input_outcome == s6::InputOutcome::invalid_nonfinite,
                "nonfinite query input was not rejected");
    }
}

void test_arithmetic_environment_and_graph() {
    const auto environment = s6::inspect_arithmetic_environment();
    require(environment.round_to_nearest_even, "RNE is not active");
    require(environment.ftz_disabled, "FTZ is active");
    require(environment.daz_disabled, "DAZ is active");
    require(environment.fused_fma_probe, "fused Float32 FMA probe failed");
    require(environment.sequential_order_probe, "sequential-order probe failed");

    volatile float one = 1.0F;
    volatile float half_ulp = std::ldexp(1.0F, -24);
    volatile float halfway = one + half_ulp;
    require(bits(halfway) == bits(1.0F), "RNE halfway did not choose the even value");

    volatile float a = 1.0F + std::ldexp(1.0F, -23);
    volatile float b = 1.0F - std::ldexp(1.0F, -23);
    volatile float c = -1.0F;
    const float fused = std::fma(a, b, c);
    volatile float product = a * b;
    const float unfused = product + c;
    require(bits(fused) == 0xa8800000U, "fused result bit pattern mismatch");
    require(bits(unfused) == 0U, "unfused discriminator mismatch");

    const auto ordered = make_case({4096.0F, 1.0F, 1.0F}, {0.0F, 0.0F, 0.0F});
    const auto production =
        s6::cpu_search_float32(ordered.dataset_view(), ordered.query_view());
    require(production.result.has_value(), "ordered graph produced no result");
    require(bits(production.result->squared_distance) == 0x4b800000U,
            "ascending sequential accumulation changed");
}

void test_signed_zero_subnormal_and_exact_zero() {
    auto zeros = make_case({-0.0F, +0.0F, +0.0F, -0.0F}, {+0.0F, -0.0F});
    const auto production = s6::cpu_search_float32(zeros.dataset_view(), zeros.query_view());
    require(production.invocation_status == s6::InvocationStatus::completed,
            "signed zero was rejected");
    require(production.result && production.result->index == 0,
            "signed-zero tie did not select smallest index");
    require(bits(production.result->squared_distance) == 0U,
            "exact match was not positive zero");
    const auto report = run_validated(zeros);
    require(report.nn_verdict == s6::NnVerdict::blocked_unverified,
            "equal exact matches bypassed missing graph/production evidence");
    require(report.exact_ambiguity_known && report.exact_ambiguity,
            "equal exact matches lost their separate ambiguity evidence");
    for (const auto& evidence : report.bound_evidence) {
        require(evidence.exact_zero_required && evidence.exact_zero_satisfied,
                "exact-zero evidence was not retained");
    }

    const float minimum_subnormal = std::numeric_limits<float>::denorm_min();
    auto tiny = make_case({minimum_subnormal}, {0.0F});
    const auto tiny_result = s6::cpu_search_float32(tiny.dataset_view(), tiny.query_view());
    require(tiny_result.result && bits(tiny_result.result->squared_distance) == 0U,
            "minimum-subnormal square did not underflow to positive zero");
    const auto tiny_reference =
        s6::compute_float64_reference(tiny.dataset_view(), tiny.query_view());
    const auto tiny_report = s6::validate_cpu_search(
        tiny.dataset_view(), tiny.query_view(), tiny_result, tiny_reference);
    require(tiny_report.bound_evidence.size() == 1 &&
                !tiny_report.bound_evidence[0].exact_zero_required &&
                tiny_report.independently_computed_float64_distances[0] > 0.0,
            "positive exact value rounded to zero was mislabeled an exact match");
    // Exact dyadic oracle: decode the binary32 operand, then square its integer
    // significand and power-of-two exponent without floating-point arithmetic.
    const auto exact_tiny_square = exact_square_against_zero(minimum_subnormal);
    require(exact_tiny_square.significand == 1 && exact_tiny_square.exponent == -298,
            "exact dyadic subnormal-square oracle failed");
    const auto exact_positive_tie = exact_square_against_zero(1.0F);
    const auto exact_negative_tie = exact_square_against_zero(-1.0F);
    require(exact_positive_tie.significand == exact_negative_tie.significand &&
                exact_positive_tie.exponent == exact_negative_tie.exponent,
            "exact dyadic mathematical-tie oracle failed");
}

void test_ftz_daz_guard() {
#if defined(_MSC_VER) || defined(__SSE__)
    const unsigned int saved = _mm_getcsr();
    _mm_setcsr(saved | (1U << 15U) | (1U << 6U));
    float value = 0.0F;
    const auto guarded = s6::cpu_search_float32({&value, 1, 1, 1}, {&value, 1, 1});
    _mm_setcsr(saved);
    require(guarded.invocation_status == s6::InvocationStatus::backend_failure,
            "FTZ/DAZ environment was not rejected");
#endif
}

void test_finite_input_nonfinite_arithmetic() {
    const float maximum = std::numeric_limits<float>::max();
    const float negative_maximum = -maximum;
    const auto outcome =
        s6::cpu_search_float32({&maximum, 1, 1, 1}, {&negative_maximum, 1, 1});
    require(outcome.invocation_status == s6::InvocationStatus::backend_failure,
            "nonfinite arithmetic was reported successful");
    require(outcome.domain_status == s6::DomainStatus::valid_outside_approved_domain,
            "outside-domain arithmetic was mislabeled");
    require(!outcome.result, "nonfinite arithmetic returned a production result");
}

void test_nearest_neighbor_and_independence() {
    auto input = make_case({4.0F, 4.0F, 1.0F, 1.0F, 2.0F, 2.0F}, {1.0F, 1.0F});
    const auto production = s6::cpu_search_float32(input.dataset_view(), input.query_view());
    const auto reference =
        s6::compute_float64_reference(input.dataset_view(), input.query_view());
    require(production.result && production.result->index == 1,
            "multiple-candidate nearest neighbor mismatch");
    require(reference.distances.size() == 3 && reference.argmin_index == 1,
            "Float64 reference did not retain all candidates");
    auto report = s6::validate_cpu_search(input.dataset_view(), input.query_view(), production,
                                          reference);
    require(report.independently_computed_float32_distances.size() == 3,
            "validator did not cover every candidate");
    require(report.nn_verdict == s6::NnVerdict::blocked_unverified,
            "unique nearest neighbor bypassed missing graph/production evidence");
    require(report.float64_index_agrees, "Float64 agreement was not recorded separately");

    auto corrupted_distance = production;
    corrupted_distance.result->squared_distance = 7.0F;
    auto corrupt_report = s6::validate_cpu_search(
        input.dataset_view(), input.query_view(), corrupted_distance, reference);
    require(corrupt_report.nn_verdict == s6::NnVerdict::failed,
            "corrupted production distance was not detected");

    auto wrong_index = production;
    wrong_index.result->index = 2;
    wrong_index.result->squared_distance = 2.0F;
    auto index_report =
        s6::validate_cpu_search(input.dataset_view(), input.query_view(), wrong_index, reference);
    require(index_report.nn_verdict == s6::NnVerdict::failed,
            "wrong production argmin was not detected");

    auto corrupted_reference = reference;
    corrupted_reference.distances[0] = 0.0;
    auto reference_report = s6::validate_cpu_search(
        input.dataset_view(), input.query_view(), production, corrupted_reference);
    require(reference_report.nn_verdict == s6::NnVerdict::failed,
            "corrupted Float64 evidence was not detected independently");

    auto corrupted_reference_argmin = reference;
    corrupted_reference_argmin.argmin_index = 2;
    corrupted_reference_argmin.tied_argmin_indices = {2};
    auto reference_argmin_report = s6::validate_cpu_search(
        input.dataset_view(), input.query_view(), production, corrupted_reference_argmin);
    require(reference_argmin_report.nn_verdict == s6::NnVerdict::failed,
            "corrupted Float64 argmin was not detected independently");

    auto inconsistent_input_status = production;
    inconsistent_input_status.input_outcome = s6::InputOutcome::not_applicable;
    const auto status_report = s6::validate_cpu_search(
        input.dataset_view(), input.query_view(), inconsistent_input_status, reference);
    require(status_report.nn_verdict == s6::NnVerdict::failed,
            "inconsistent production input status was not detected");
}

void test_bounds_ranking_and_verdict_precedence() {
    const auto bound_pass = s6::evaluate_ud001(1, 1.0F, 1.0, false);
    require(bound_pass.status == s6::EvidenceStatus::passed,
            "equal Float32/Float64 bound did not pass");
    const auto bound_fail = s6::evaluate_ud001(1, 100.0F, 0.0, false);
    require(bound_fail.status == s6::EvidenceStatus::failed,
            "UD-001 bound violation was not failed");
    const auto zero_fail = s6::evaluate_ud001(1, 0.0F, 1.0, true);
    require(zero_fail.status == s6::EvidenceStatus::failed,
            "exact-zero violation was not failed");

    const auto exact_ranking = s6::evaluate_ud002(1, {0.0, 4.0, 9.0});
    require(exact_ranking.status == s6::EvidenceStatus::passed &&
                exact_ranking.exact_membership_resolved &&
                exact_ranking.exact_contenders == std::vector<std::uint64_t>{0},
            "exact singleton contender classification failed");

    const double u32 = std::ldexp(1.0, -24);
    const double u64 = std::ldexp(1.0, -53);
    const double gamma32 = u32 / (1.0 - u32);
    const double gamma64 = u64 / (1.0 - u64);
    const double e32 = (1.0 + u32) * (1.0 + u32) * (1.0 + gamma32) - 1.0;
    const double e64 = (1.0 + u64) * (1.0 + u64) * (1.0 + gamma64) - 1.0;
    const double relative = (e32 + e64) / (1.0 - e64);
    const double additive = std::ldexp(1.0, -150) * (1.0 + gamma32);
    double boundary = (1.0 + relative + 2.0 * additive) / (1.0 - relative);
    bool found_boundary_uncertainty = false;
    for (int step = -32; step <= 32 && !found_boundary_uncertainty; ++step) {
        double candidate = boundary;
        for (int move = 0; move < std::abs(step); ++move) {
            candidate = std::nextafter(candidate,
                                       step < 0 ? -std::numeric_limits<double>::infinity()
                                                : std::numeric_limits<double>::infinity());
        }
        const auto ranking = s6::evaluate_ud002(1, {1.0, candidate});
        found_boundary_uncertainty = !ranking.boundary_uncertain_indices.empty() &&
                                     !ranking.conservative_contenders.empty();
    }
    require(found_boundary_uncertainty,
            "outward ranking did not retain a boundary-uncertain contender");

    auto tied = make_case({-1.0F, 1.0F}, {0.0F});
    const auto production = s6::cpu_search_float32(tied.dataset_view(), tied.query_view());
    const auto reference =
        s6::compute_float64_reference(tied.dataset_view(), tied.query_view());
    require(production.result && production.result->index == 0,
            "smallest-index equal-distance tie failed");
    auto tied_report =
        s6::validate_cpu_search(tied.dataset_view(), tied.query_view(), production, reference);
    require(tied_report.nn_verdict == s6::NnVerdict::blocked_unverified &&
                tied_report.exact_ambiguity_known && tied_report.exact_ambiguity,
            "equal-distance evidence or mandatory blocking was lost");

    s6::ValidationOptions missing;
    missing.candidate_evidence_complete = false;
    auto missing_report = s6::validate_cpu_search(tied.dataset_view(), tied.query_view(),
                                                  production, reference, missing);
    require(missing_report.nn_verdict == s6::NnVerdict::blocked_unverified,
            "missing candidate evidence did not block acceptance");
    require(missing_report.candidate_coverage_status == s6::EvidenceStatus::passed &&
                missing_report.candidate_coverage_provenance ==
                    s6::EvidenceProvenance::internally_observed &&
                missing_report.production_candidate_coverage_status ==
                    s6::EvidenceStatus::blocked &&
                missing_report.production_candidate_coverage_provenance ==
                    s6::EvidenceProvenance::unavailable,
            "validator and production candidate coverage were conflated");

    s6::ValidationOptions no_graph;
    no_graph.graph_evidence_available = false;
    const auto no_graph_report = s6::validate_cpu_search(
        tied.dataset_view(), tied.query_view(), production, reference, no_graph);
    require(no_graph_report.nn_verdict == s6::NnVerdict::blocked_unverified &&
                no_graph_report.graph_status == s6::EvidenceStatus::blocked &&
                no_graph_report.graph_provenance == s6::EvidenceProvenance::unavailable,
            "missing graph evidence did not remain blocked");

    s6::ValidationOptions unsupported_graph;
    unsupported_graph.graph_evidence_available = true;
    const auto unsupported_graph_report = s6::validate_cpu_search(
        tied.dataset_view(), tied.query_view(), production, reference, unsupported_graph);
    require(unsupported_graph_report.nn_verdict == s6::NnVerdict::blocked_unverified &&
                unsupported_graph_report.graph_status == s6::EvidenceStatus::blocked &&
                unsupported_graph_report.graph_provenance ==
                    s6::EvidenceProvenance::external_attestation,
            "external graph attestation was treated as independent proof");

    s6::ValidationOptions missing_ranking;
    missing_ranking.ranking_evidence_available = false;
    const auto missing_ranking_report = s6::validate_cpu_search(
        tied.dataset_view(), tied.query_view(), production, reference, missing_ranking);
    require(missing_ranking_report.nn_verdict == s6::NnVerdict::blocked_unverified &&
                missing_ranking_report.ud002_status == s6::EvidenceStatus::blocked &&
                missing_ranking_report.ranking_provenance ==
                    s6::EvidenceProvenance::unavailable,
            "missing ranking evidence did not block acceptance");

    auto unknown_domain = production;
    unknown_domain.domain_status = s6::DomainStatus::unknown;
    const auto unknown_domain_report = s6::validate_cpu_search(
        tied.dataset_view(), tied.query_view(), unknown_domain, reference);
    require(unknown_domain_report.nn_verdict == s6::NnVerdict::blocked_unverified &&
                unknown_domain_report.domain_status == s6::DomainStatus::unknown &&
                unknown_domain_report.domain_consistency_status ==
                    s6::EvidenceStatus::blocked,
            "unknown domain eligibility was accepted or failed instead of blocked");

    auto corrupt = production;
    corrupt.result->index = 1;
    s6::ValidationOptions missing_and_failed;
    missing_and_failed.graph_evidence_available = false;
    auto precedence_report = s6::validate_cpu_search(
        tied.dataset_view(), tied.query_view(), corrupt, reference, missing_and_failed);
    require(precedence_report.nn_verdict == s6::NnVerdict::failed,
            "demonstrated failure did not override missing evidence");

    float value = 0.0F;
    const auto invalid_production =
        s6::cpu_search_float32({&value, 0, 1, 0}, {&value, 1, 1});
    const auto invalid_reference =
        s6::compute_float64_reference({&value, 0, 1, 0}, {&value, 1, 1});
    const auto invalid_report = s6::validate_cpu_search(
        {&value, 0, 1, 0}, {&value, 1, 1}, invalid_production, invalid_reference);
    require(invalid_report.nn_verdict == s6::NnVerdict::not_applicable,
            "contract-invalid input received an NN verdict");
}

void test_end_to_end_outside_domain_verdict() {
    constexpr std::uint64_t outside_dimension = std::uint64_t{1} << 24U;
    std::vector<float> dataset(static_cast<std::size_t>(outside_dimension), 0.0F);
    std::vector<float> query(static_cast<std::size_t>(outside_dimension), 0.0F);
    const s6::DatasetView dataset_view{dataset.data(), 1, outside_dimension, dataset.size()};
    const s6::QueryView query_view{query.data(), outside_dimension, query.size()};

    const auto production = s6::cpu_search_float32(dataset_view, query_view);
    const auto reference = s6::compute_float64_reference(dataset_view, query_view);
    require(production.invocation_status == s6::InvocationStatus::completed &&
                production.domain_status ==
                    s6::DomainStatus::valid_outside_approved_domain &&
                production.result && production.result->squared_distance == 0.0F,
            "end-to-end outside-domain production classification failed");
    const auto report =
        s6::validate_cpu_search(dataset_view, query_view, production, reference);
    require(report.domain_status == s6::DomainStatus::valid_outside_approved_domain &&
                report.domain_consistency_status == s6::EvidenceStatus::passed &&
                report.nn_verdict == s6::NnVerdict::blocked_unverified,
            "valid outside-domain execution was failed or accepted");
    require(report.decision_trace ==
                "precedence 6: valid outside approved numerical domain",
            "outside-domain decision trace did not identify precedence 6");

    auto corrupted = production;
    corrupted.result->squared_distance = 1.0F;
    const auto failed_report =
        s6::validate_cpu_search(dataset_view, query_view, corrupted, reference);
    require(failed_report.nn_verdict == s6::NnVerdict::failed,
            "demonstrated numerical failure did not override outside-domain status");
}

}  // namespace

int main() {
    try {
        test_input_and_shape_contract();
        test_nonfinite_rejection();
        test_arithmetic_environment_and_graph();
        test_signed_zero_subnormal_and_exact_zero();
        test_ftz_daz_guard();
        test_finite_input_nonfinite_arithmetic();
        test_nearest_neighbor_and_independence();
        test_bounds_ranking_and_verdict_precedence();
        test_end_to_end_outside_domain_verdict();
        const auto environment = s6::inspect_arithmetic_environment();
        std::cout << "arithmetic-environment RNE=" << environment.round_to_nearest_even
                  << " FTZ-disabled=" << environment.ftz_disabled
                  << " DAZ-disabled=" << environment.daz_disabled
                  << " fused-FMA=" << environment.fused_fma_probe
                  << " sequential-order=" << environment.sequential_order_probe << '\n';
        std::cout << "policies " << s6::kGraphPolicy << ' ' << s6::kEnvironmentPolicy
                  << ' ' << s6::kBoundPolicy << ' ' << s6::kRankingPolicy << ' '
                  << s6::kVerdictPolicy << '\n';
        std::cout << "S6 CPU fixture tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "S6 CPU fixture tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
