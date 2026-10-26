# Correctness Plan

Status: Planned; not executed\
Acceptance gates: [AcceptanceCriteria.md](../requirements/AcceptanceCriteria.md)\
Open numerical decisions: [DecisionRegister.md](../governance/DecisionRegister.md)

## 1. Purpose

This plan defines future correctness evidence for the Float32 CPU comparator and CUDA V0–V3 against the Float64 reference. It preserves separate outcomes for argmin index, distance error, and numerical ambiguity. It contains no test results and specifies no executable command because no implementation or repository build configuration exists at S4.2.

## 2. Preconditions

Before execution, record:

- code revision and working-tree state;
- implementation variant and build configuration;
- compiler, CUDA Toolkit, driver, GPU, CPU, and operating environment as applicable;
- fast-math state;
- exact executed command(s);
- dataset generator/configuration and seed;
- the approved identifiers/versions for any numerical tolerance, near-tie rule, and ambiguous-case policy.

If a required GPU or compatible CUDA environment is unavailable, GPU checks are `blocked`. CPU-only evidence is not a substitute. If UD-001–UD-003 remain unresolved, collect diagnostics but mark dependent gates blocked.

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

## 4. Derived edge-case catalog

The following cases derive coverage from approved behavior; they do not add new requirements or choose unresolved thresholds.

| Case ID | Category | Expected observation |
|---|---|---|
| EC-001 | Nearest candidate at index 0 | Exhaustive result returns index 0 when uniquely nearest. |
| EC-002 | Nearest candidate at a middle index | Result returns that unique index. |
| EC-003 | Nearest candidate at index `N-1` | Result returns the last index, demonstrating full candidate coverage. |
| EC-004 | Duplicate/equal-computed-distance candidates | Smallest tied index is returned. |
| EC-005 | Query exactly equals a dataset vector | L2² result is positive or negative zero as produced, numerically zero; correct index follows the tie rule. |
| EC-006 | Positive and negative zero components | Input is accepted and signed zero does not change the L2²/tie contract. |
| EC-007 | Invalid NaN in query or dataset | Input is rejected. |
| EC-008 | Invalid positive or negative infinity | Input is rejected. |
| EC-009 | `N = 0` | Input is rejected. |
| EC-010 | `D = 0` | Input is rejected. |
| EC-011 | Finite high-magnitude core case | Generated distances remain finite in Float32; exact values are selected later without defining overflow behavior. |
| EC-012 | Numerically close candidates | Record Float64 best/competitor diagnostics and separate Float32 index/error observations; classification/verdict remains blocked until UD-001–UD-003 are resolved. |
| EC-013 | Repeated runs of identical input | Record deterministic result observations; any formal determinism requirement needs separate approval if not implied by the tie contract. |

Additional cases may be added as derived coverage when they do not change the valid-input or numerical-acceptance contract.

## 5. Per-case procedure

1. Validate or reject the input according to AC-001.
2. For valid input, compute the Float64 reference distances and reference argmin; retain the best and relevant competing distances for diagnostics.
3. Execute the Float32 CPU lane and each in-scope CUDA variant with the identical logical input.
4. Record, separately for each lane:
   - returned argmin index;
   - returned Float32 L2² distance;
   - whether the distance is finite;
   - comparison to the Float64 reference index;
   - numeric distance-error value(s), without a verdict if UD-001 is open;
   - ambiguity diagnostic inputs and classification, without classification if UD-002 is open.
5. Evaluate the equal-computed-distance smallest-index rule independently of Float64 near-tie diagnostics.
6. Apply only approved numerical and ambiguity policies. Never let a Float64 near tie automatically accept the implementation result.
7. Assign each gate a passed, failed, blocked, skipped, or planned status and retain the evidence location.

## 6. Verdict composition

The report must not compress the three numerical observations into one opaque result.

| Field | Meaning | Current policy status |
|---|---|---|
| `argmin_observation` | Implementation index and comparison to reference/common tie contract. | Unambiguous mismatches fail; ambiguous treatment is blocked by UD-003. |
| `distance_error_observation` | Numeric difference between Float32 result and Float64 reference under the selected metric. | Verdict blocked by UD-001. |
| `ambiguity_observation` | Float64 diagnostic describing closeness of best competitors. | Classification blocked by UD-002; never automatic acceptance. |
| `input_contract_verdict` | Accept/reject and finite-core checks. | Can pass/fail independently where implementation detail does not prevent execution. |
| `overall_correctness_status` | Gate-level synthesis under approved policy. | Cannot be accepted while a required dependent gate is blocked. |

## 7. Required report fields

```text
case_id:
workload: single-query | batch-query
variant: cpu-f32 | v0 | v1 | v2 | v3 | v4-optional
N:
D:
seed:
revision:
working_tree_state:
build_configuration:
environment_reference:
command:
input_domain_status:
implementation_index:
implementation_distance_f32:
reference_index_f64:
reference_distance_f64:
relevant_competitor_distance_f64:
argmin_observation:
distance_error_metric:
distance_error_value:
distance_tolerance_policy:
ambiguity_rule:
ambiguity_observation:
gate_statuses:
evidence_location:
notes:
```

Unknown or unresolved fields must say `unresolved`, `not captured`, or `not applicable`; they must not be omitted in a way that implies success.

## 8. Exit criteria

Correctness is accepted only when all required functional, input, numerical, variant, and dataset gates have executable evidence and no required gate is failed or blocked. At S4.2, AC-006–AC-008 remain blocked for future verdicts, so this plan is ready as documentation but cannot yet produce final correctness acceptance.
