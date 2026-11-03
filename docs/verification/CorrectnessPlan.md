# Correctness Plan

Status: Reusable verification plan; executed S7 evidence is reported separately\
Acceptance gates: [AcceptanceCriteria.md](../requirements/AcceptanceCriteria.md)\
Numerical policy: [S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md](../specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md)

## 1. Purpose

This plan defines correctness evidence for the Float32 CPU production baseline and CUDA V0–V3 against the Float64 reference. It preserves separate outcomes for argmin index, distance error, and numerical ambiguity. The plan remains the reusable method and does not embed mutable results. Executed, revision-bound S7 results are reported in the [S7 final experiment report](../experiments/S7-ACCEPT-20261103-01/FinalExperimentReport.md) and its [evidence index](../experiments/S7-ACCEPT-20261103-01/EVIDENCE_INDEX.md).

## 2. Preconditions

Before execution, record:

- code revision and working-tree state;
- implementation variant and build configuration;
- compiler, CUDA Toolkit, driver, GPU, CPU, and operating environment as applicable;
- fast-math state;
- exact executed command(s);
- dataset generator/configuration and seed;
- the approved identifiers/versions for any numerical tolerance, near-tie rule, and ambiguous-case policy.
- approved design identifier and containing repository revision;
- selected block size and tuning-qualification evidence when applicable;
- reduction-record ABI evidence;
- COND-01 mask/participation evidence;
- COND-02 real-candidate nonfinite-status propagation evidence.

If a required GPU or compatible CUDA environment is unavailable, GPU checks are `blocked`. CPU-only evidence is not a substitute. Before any accepted result, verify the applicable S6 graph, environment, bound, ranking, and verdict policy versions. Missing runtime/compiler, every-candidate, interval, reduction, or production-path evidence is `blocked/unverified` unless a concrete defect has already established `failed`.

## 3. Core test matrix

For every core implementation lane (Float32 CPU, V0, V1, V2, V3), cover all 12 combinations:

| `N` | Required `D` values |
|---:|---|
| 1024 | 8, 32, 128 |
| 16384 | 8, 32, 128 |
| 262144 | 8, 32, 128 |
| 1048576 | 8, 32, 128 |

Use seeded synthetic inputs and retain a manifest sufficient to regenerate every case. Before execution, verify that the generated core dataset keeps all evaluated Float32 squared distances finite.

Single query is the primary matrix. Batch-query correctness is a separate secondary matrix and must not replace it. V4, if later authorized, is separate and optional.

The fast independent validator checks every candidate for every required matrix case. The exact-dyadic oracle supplies proof-sensitive stress and boundary fixtures and need not process every candidate in each large core case. Diagnostic buffers and validators do not replace actual production-path tests.

## 4. Derived edge-case catalog

The following cases derive coverage from approved behavior; they do not add new requirements or choose unresolved thresholds.

| Case ID | Category | Expected observation |
|---|---|---|
| EC-001 | Nearest candidate at index 0 | Exhaustive result returns index 0 when uniquely nearest. |
| EC-002 | Nearest candidate at a middle index | Result returns that unique index. |
| EC-003 | Nearest candidate at index `N-1` | Result returns the last index, demonstrating full candidate coverage. |
| EC-004 | Duplicate/equal-computed-distance candidates | Smallest tied index is returned. |
| EC-005 | Query exactly equals a dataset vector | Both approved graphs produce exact `+0`; the correct index follows the smallest-index tie rule. |
| EC-006 | Positive and negative zero components | Input is accepted and signed zero does not change the L2²/tie contract. |
| EC-007 | Invalid NaN in query or dataset | Input is rejected. |
| EC-008 | Invalid positive or negative infinity | Input is rejected. |
| EC-009 | `N = 0` | Input is rejected. |
| EC-010 | `D = 0` | Input is rejected. |
| EC-011 | Finite high-magnitude core case | Generated distances remain finite in Float32; exact values are selected later without defining overflow behavior. |
| EC-012 | Numerically close candidates | Evaluate UD-001, exact `C`, any outward `Ĉ`, provenance, exact ambiguity, and the ordered UD-003 verdict separately. |
| EC-013 | Repeated runs of identical input | Record deterministic result observations; any formal determinism requirement needs separate approval if not implied by the tie contract. |
| EC-014 | Positive tiny exact FMA result rounds to `+0` | Exercise the subnormal-or-zero underflow model and conservative `S_i <= D` allowance. |
| EC-015 | Exact and outward interval boundary | Exercise inclusive `L_i <= U_*`, confirmed inclusion/exclusion, and boundary-uncertain conservative retention. |
| EC-016 | Valid input outside numerical core domain | Record `valid-outside-approved-domain`; do not report expected invalid-input rejection. |
| EC-017 | Required finite-core nonfinite result | Record a demonstrated correctness failure. |
| EC-018 | V3 logical tail warp | All launched lanes participate; missing candidates use `valid=false`; safe shuffle sources and the final result match the independent reducer. |
| EC-019 | V3 cross-warp/cross-block equal distance | The smallest full-width index wins across every hierarchy boundary. |
| EC-020 | Real candidate with nonfinite completed arithmetic | The candidate remains logically present, contributes no value to the finite argmin, never enters the floating comparator, and propagates nonfinite/domain-failure status to the host. Finite-core execution fails; outside-domain execution is not mislabeled invalid input and receives no accepted NN result. |
| EC-021 | Reduction-record ABI | Host/device size, alignment, field widths and transfer preserve Float32 distance, flags and `uint64` index. |

Additional cases may be added as derived coverage when they do not change the valid-input or numerical-acceptance contract.

## 5. Per-case procedure

1. Validate or reject the input according to AC-001.
2. For contract-invalid input, record `expected-rejection-pass|expected-rejection-fail|expected-rejection-blocked` and `nn_verdict=not-applicable`.
3. For contract-valid input, establish numerical-domain eligibility and finite-core status separately.
4. Compute every Float64 reference distance using the approved graph and retain the reference argmin and tie set.
5. Execute the Float32 CPU lane and each in-scope CUDA variant with the identical logical input.
6. Independently reproduce every candidate's approved Float32 graph and verify graph/environment conformance.
7. For every candidate, evaluate the UD-001 bound and any applicable exact-zero assertion using exact or certified outward-safe arithmetic.
8. Verify exhaustive coverage and the lexicographic minimum of `(computed Float32 squared distance, index)`.
9. Compute exact `C`, or certified outward `Ĉ` with boundary-uncertain indices and explicit provenance; retain mathematical, Float64, and Float32 tie labels separately.
10. Apply the ordered UD-003 policy, recording the first decisive condition and final NN verdict.
11. Verify direct reduction/tie fixtures, COND-01 participation masks and safe sources, logical tail records and full-width index transport. For COND-02, verify independently that finite lexicographic reduction excludes nonfinite values, failure-status aggregation includes every real candidate, both operations remain associative through the hierarchy, and applicable failure prevents a successful accepted NN result. Verify the actual production path independently of diagnostic-only modes.
12. Assign gate-level evidence statuses and retain revision-, build-, environment-, and configuration-bound evidence.

## 6. Verdict composition

The report must not compress the three numerical observations into one opaque result.

| Field | Meaning | Current policy status |
|---|---|---|
| `input_outcome` | Expected invalid-input rejection or expected valid-input admission. | Independent of NN verdict. |
| `domain_status` | Supported finite core, valid outside approved domain, failed finite-core execution, or unknown eligibility. | Outside-domain is not invalid-input rejection. |
| `graph_conformance` | Approved Float32/Float64 operation graph and arithmetic environment. | Required independently of the distance bound. |
| `distance_bound_status` | Every-candidate UD-001 comparison and exact-zero checks. | Passed, failed, or blocked/unverified from executed evidence. |
| `f32_lexicographic_correctness` | Exhaustive minimum of computed Float32 distance and smallest index. | Required for every accepted NN verdict. |
| `exact_contender_indices` | Exact UD-002 contender set when resolved. | `unknown` when exact boundary membership remains unresolved. |
| `conservative_contender_indices` | Verified outward `Ĉ`. | Never represented as exact membership. |
| `contender_provenance` | `exact` or `conservative-outward`. | Mandatory for accepted ambiguous results. |
| `exact_ambiguity` | Exact contender-set singleton/multiple state. | `unknown` for unresolved conservative boundary cases. |
| `outward_boundary_uncertainty` | Whether outward evaluation leaves membership undecided. | Separate from exact ambiguity. |
| `nn_verdict` | Ordered UD-003 outcome. | `accepted-unambiguous`, `accepted-ambiguous`, `failed`, `blocked/unverified`, or `not-applicable` for invalid input. |
| `overall_correctness_status` | Matrix-level synthesis. | Accepted only when all applicable cases and gates are complete with no required failure or block. |

## 7. Required report fields

```text
case_id:
workload: single-query | batch-query
variant: cpu-f32 | v0 | v1 | v2 | v3 | v4-optional
design_id:
design_revision:
N:
D:
seed:
revision:
working_tree_state:
build_configuration:
environment_reference:
command:
input_outcome:
domain_status:
arithmetic_environment_status:
graph_policy_id:
graph_conformance:
implementation_index:
implementation_distance_f32:
reference_index_f64:
reference_distance_f64:
relevant_competitor_distance_f64:
mathematical_minimizer_indices:
mathematical_exact_tie:
f64_computed_minimizer_indices:
f64_computed_tie:
f32_computed_minimizer_indices:
f32_computed_tie:
f32_lexicographic_correctness:
distance_bound_policy_id:
all_candidate_distance_bound_status:
exact_zero_status:
ranking_policy_id:
exact_contender_indices:
conservative_contender_indices:
boundary_uncertain_indices:
contender_provenance:
exact_ambiguity:
outward_boundary_uncertainty:
float64_index_agreement:
reduction_fixture_status:
block_size:
tuning_qualification_status:
reduction_record_abi_status:
logical_candidate_coverage_status:
production_nonfinite_status:
finite_argmin_excludes_nonfinite_status:
failure_status_aggregation_status:
combined_reduction_associativity_status:
v3_participation_mask_status:
v3_safe_source_status:
production_status_propagation_status:
production_path_status:
verdict_policy_id:
ordered_decision_trace:
nn_verdict:
gate_statuses:
evidence_location:
notes:
```

Unknown or unresolved fields must say `unresolved`, `not captured`, or `not applicable`; they must not be omitted in a way that implies success.

## 8. Exit criteria

Correctness is accepted only when the complete applicable functional, input, numerical, variant, and dataset matrix has executable revision-bound evidence and no required gate is failed or blocked. The S6 policy defines AC-006–AC-008 but does not itself supply runtime results. Runtime status therefore belongs to a revision-bound experiment report rather than this reusable plan; for accepted experiment `S7-ACCEPT-20261103-01`, consult the linked report and evidence instead of inferring acceptance from this plan.

Design approval is not executed evidence. A missing candidate, an unsafe V3
mask/source, loss of full-width index information, conversion of a real
nonfinite candidate to a logical-invalid tail, passage of NaN/infinity through
the ordinary finite comparator, loss of failure status through the production
hierarchy, or reporting success despite applicable failure is failed when
demonstrated and otherwise blocked/unverified when required evidence is absent.
