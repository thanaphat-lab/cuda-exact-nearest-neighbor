# Decision Register

Status: Canonical decision-status record\
Milestone: S4.2\
Last updated: 2026-10-26 (Asia/Bangkok)

## 1. Approved decisions

| ID | Decision | Authority | Affected requirements |
|---|---|---|---|
| DR-001 | Single query is the primary workload; batch query is secondary. | Approved baseline | REQ-W-001, REQ-W-002 |
| DR-002 | Distance is squared Euclidean (L2²), search is exhaustive and exact, and equal computed distances choose the smallest index. | Approved baseline | REQ-S-001–REQ-S-003 |
| DR-003 | CPU/GPU computation is Float32; the numerical reference is Float64. | Approved baseline | REQ-N-001, REQ-N-002 |
| DR-004 | Float32 evaluation-order differences are permitted only with numerical validation. | Addendum U01 | REQ-N-003 |
| DR-005 | Argmin index, distance error, and numerical ambiguity are validated separately. | Addendum U02 | REQ-N-004, REQ-C-002 |
| DR-006 | Float64 near-tie diagnostics do not automatically accept a result. | Addendum U03 | REQ-N-005 |
| DR-007 | NaN, infinity, `N = 0`, and `D = 0` are rejected; signed zero is accepted. | Addendum U04 | REQ-I-001, REQ-I-002 |
| DR-008 | Core datasets keep squared distances finite in Float32. | Addendum U05 | REQ-I-003 |
| DR-009 | V0–V3 preserve common distance, input, exhaustive-search, output, and tie-breaking contracts. | Addendum U06 | REQ-O-001–REQ-O-005 |
| DR-010 | V0 is naive CUDA, V1 targets coalescing, V2 block reduction, V3 warp-level/optimized reduction; V4 batch reuse is optional. | Approved baseline and policy | REQ-O-001–REQ-O-006 |
| DR-011 | Synthetic seeded datasets use the approved `N` and `D` matrix and extended edge cases. | Approved baseline | REQ-D-001–REQ-D-003 |
| DR-012 | Benchmarking reports kernel-only and end-to-end timing separately, with explicit transfer/preprocessing boundaries. | Baseline and Addendum U07 | REQ-B-001, REQ-B-004 |
| DR-013 | Each benchmark configuration uses 10 warm-ups and 100 measured repetitions and reports median, p5, and p95. | Approved baseline | REQ-B-002, REQ-B-003 |
| DR-014 | Real GPU, driver, CUDA Toolkit, compiler, environment, seed, and timing metadata are captured before benchmarking. | Baseline and Addendum U08 | REQ-B-005 |
| DR-015 | Fast math is disabled in core benchmarks. | Approved policy | REQ-B-006 |
| DR-016 | Correctness precedes performance acceptance. | Approved policy | REQ-C-001 |
| DR-017 | Experiments are reproducible and revision-bound; rejected hypotheses are valid outcomes. | Approved policy | REQ-R-001–REQ-R-004 |

## 2. Unresolved decisions

These items are intentionally unresolved. Proposed values, defaults, library behavior, or implementation observations must not be treated as approval.

| ID | Unresolved decision | Why it is open | Affected acceptance gates | Required resolution evidence |
|---|---|---|---|---|
| UD-001 | Exact numerical distance-error tolerance, including whether it is absolute, relative, combined, or dimension-scaled. | No value or formula was approved. | AC-006; AC-012 performance prerequisite | Explicit project-owner approval, then synchronized specification/addendum and test policy. |
| UD-002 | Numerical near-tie classification threshold and formula. | Float64 is approved for diagnostics, but no classification boundary exists. | AC-007; AC-008; AC-012 performance prerequisite | Explicit project-owner approval defining the classification rule and units. |
| UD-003 | Acceptance treatment for cases classified as numerically ambiguous. | U02 requires separate reporting and U03 forbids automatic acceptance; neither defines the verdict. | AC-005; AC-008; AC-012 performance prerequisite | Explicit project-owner approval of pass/fail/conditional handling and required evidence. |
| UD-004 | Hardware-specific optimization settings. | Launch shapes, occupancy targets, vector widths, tiling, and similar settings depend on future design and hardware evidence. | AC-010; AC-013; AC-014 when such settings are used | Approved implementation design plus recorded target environment; no single setting is required by S4.2. |
| UD-005 | Detailed implementation choices, including API, data layout, accumulation/reduction structure, error representation, and memory ownership. | The baseline approves behavior and optimization stages, not these mechanics. | AC-001–AC-005; AC-009; AC-010 | Approved design record consistent with the canonical requirements before implementation. |

## 3. Gate rule for unresolved decisions

- A test may collect and report index, error, and diagnostic data while UD-001–UD-003 remain open.
- A gate whose verdict depends on an unresolved item is `blocked`; it is never converted to passed by convention or by a locally selected threshold.
- Unambiguous contract checks that do not depend on those items may still pass or fail independently.
- Performance measurements may not receive performance acceptance until correctness is accepted for the same revision and configuration.

## 4. Change history

| Date | Change |
|---|---|
| 2026-10-26 | Initial S4.2 register created from the approved baseline, Addendum V1.0-A, and additional approved policies. No unresolved decision was resolved. |
