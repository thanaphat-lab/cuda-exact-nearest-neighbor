# Final Project Specification v1.0

Status: Canonical approved baseline\
Milestone: S4.2 — Canonical Project Documentation\
Addendum incorporated: Technical Contract Addendum V1.0-A\
Canonical date: 2026-10-26 (Asia/Bangkok)

## 1. Purpose and authority

This document is the canonical behavioral specification for the GPU-Accelerated Exact Nearest Neighbor Search Using CUDA project. It incorporates the approved Project Specification baseline and all eight decisions in Technical Contract Addendum V1.0-A. It defines required behavior and acceptance policy, but does not approve implementation details that remain open.

Source authority and change rules are defined in [SourceOfTruth.md](../governance/SourceOfTruth.md). Individual decisions and open questions are recorded in [DecisionRegister.md](../governance/DecisionRegister.md). Observable gates are defined in [AcceptanceCriteria.md](../requirements/AcceptanceCriteria.md).

## 2. Scope

### 2.1 Workloads

- **REQ-W-001 — Primary workload:** The primary workload is exact nearest-neighbor search for one query vector against a dataset of `N` vectors, each with dimension `D`.
- **REQ-W-002 — Secondary workload:** Batch-query evaluation is secondary. It must be reported separately from the primary workload and does not replace primary-workload acceptance.

### 2.2 Search semantics

- **REQ-S-001 — Distance:** The distance is squared Euclidean distance (L2²): the sum, over all dimensions, of the squared difference between the query and candidate vector. No square root is required.
- **REQ-S-002 — Search:** Search is exhaustive and exact over every candidate index in `[0, N)`; approximate search and pruning are outside the approved scope.
- **REQ-S-003 — Tie-breaking:** When equal distances are produced by the computation being evaluated, the result is the smallest candidate index among those equal computed distances.

### 2.3 Precision and numerical evaluation

- **REQ-N-001 — Production precision:** CPU and GPU computations use Float32 inputs and Float32 distance computation/results.
- **REQ-N-002 — Numerical reference:** The numerical reference computes in Float64 and is used for validation and diagnostics; it is not a production implementation.
- **REQ-N-003 — Evaluation order (U01):** Float32 CPU and GPU evaluation-order differences are allowed only when assessed through the approved numerical-validation process.
- **REQ-N-004 — Separate observations (U02):** Validation records and evaluates the returned argmin index, distance error, and numerical ambiguity as separate observations.
- **REQ-N-005 — Near-tie role (U03):** Float64 near-tie analysis is diagnostic and never, by itself, accepts a result.
- **REQ-N-006 — Unresolved numerical policy:** The exact distance-error tolerance, near-tie classification threshold, and acceptance treatment for ambiguous cases are not approved. They must not be inferred from Float32/Float64 types, machine epsilon, test data, or implementation behavior.

### 2.4 Input domain

- **REQ-I-001 — Rejected inputs (U04):** Reject any input containing NaN or positive/negative infinity, and reject `N = 0` or `D = 0`.
- **REQ-I-002 — Signed zero (U04):** Positive and negative zero are valid inputs.
- **REQ-I-003 — Finite core distances (U05):** Core evaluation datasets must keep squared distances finite in Float32. Overflow behavior is outside the core acceptance domain unless separately approved.

The precise API, error type, and error-message text are implementation decisions and are not specified here.

## 3. Optimization scope

- **REQ-O-001 — V0:** Naive CUDA establishes the GPU baseline.
- **REQ-O-002 — V1:** Coalesced memory access is the first core optimization focus.
- **REQ-O-003 — V2:** Block-level reduction is the second core optimization focus.
- **REQ-O-004 — V3:** Warp-level or otherwise optimized reduction is the third core optimization focus.
- **REQ-O-005 — Common contracts (U06):** V0 through V3 preserve the same distance, valid-input, exhaustive-search, output, and tie-breaking contracts.
- **REQ-O-006 — V4:** Batch optimization and data reuse are optional stretch work. V4 is not required for core completion and cannot substitute for V0–V3 evidence.

These labels define optimization intent, not launch parameters, data layout, reduction algorithm, memory ownership, or other detailed implementation choices.

## 4. Dataset contract

- **REQ-D-001 — Generation:** Core datasets are synthetic and reproducible from a recorded seed.
- **REQ-D-002 — Required sizes:** Core coverage includes every combination of:
  - `N ∈ {1024, 16384, 262144, 1048576}`
  - `D ∈ {8, 32, 128}`
- **REQ-D-003 — Edge cases:** Correctness evaluation includes extended edge-case tests, including the approved invalid-input and signed-zero behavior. The detailed edge-case catalog is a derived verification plan, not an expansion of the valid-input contract.

## 5. Correctness and acceptance policy

- **REQ-C-001 — Correctness first:** Correctness acceptance for the relevant revision and configuration is required before performance can be accepted.
- **REQ-C-002 — Independent reporting:** Argmin agreement, distance error, and ambiguity classification must remain separately visible; none may be silently collapsed into a single pass flag.
- **REQ-C-003 — Unresolved gates:** A check whose verdict depends on an unresolved tolerance, near-tie threshold, or ambiguous-case policy is `blocked`, not passed or failed.
- **REQ-C-004 — Evidence statuses:** Evidence is reported as planned, passed, failed, skipped, or blocked. Planned work and unavailable evidence do not count as passed.

The detailed planned verification procedure is in [CorrectnessPlan.md](../verification/CorrectnessPlan.md).

## 6. Benchmark contract

- **REQ-B-001 — Measurement classes:** Record kernel-only and end-to-end timing separately.
- **REQ-B-002 — Sampling:** Perform 10 warm-up iterations followed by 100 measured repetitions for each reported configuration.
- **REQ-B-003 — Statistics:** Report the median, p5, and p95 of measured repetitions. Do not substitute a mean for a required statistic.
- **REQ-B-004 — Timing boundaries (U07):** Explicitly document timing boundaries and identify whether host/device transfers and preprocessing costs are included or excluded.
- **REQ-B-005 — Environment (U08):** Before benchmarking, capture the real GPU, driver, CUDA Toolkit, compiler, operating environment, build configuration, dataset seed, and relevant timing setup. Unknown values remain unknown; they are not guessed.
- **REQ-B-006 — Fast math:** Core benchmarks disable fast math.
- **REQ-B-007 — Workload separation:** Report primary single-query measurements separately from secondary batch-query measurements. V4 results, if any, are separately labeled optional.
- **REQ-B-008 — No threshold implied:** No absolute performance target or required speedup has been approved. Performance hypotheses must state their interpretation before execution.

The derived measurement procedure is in [BenchmarkProtocol.md](../performance/BenchmarkProtocol.md).

## 7. Reproducibility and experiment policy

- **REQ-R-001 — Revision binding:** Every correctness, benchmark, and profiling result must be tied to a code revision and working-tree state.
- **REQ-R-002 — Reproduction evidence:** Experiments record the executed commands, configuration, environment, dataset parameters and seed, timing boundaries, and raw or traceable output.
- **REQ-R-003 — Honest outcomes:** A performance hypothesis outcome is `supported`, `rejected`, or `inconclusive`. A rejected hypothesis is a valid experiment outcome when backed by complete evidence.
- **REQ-R-004 — No fabrication:** Missing commands, environment values, measurements, revision identifiers, and results remain explicitly missing or blocked.

Use [ExperimentTemplate.md](../experiments/ExperimentTemplate.md) for future experiment reports.

## 8. Exclusions

This specification does not approve or require:

- approximate nearest-neighbor search;
- a square-root distance output;
- a precision other than Float32 for CPU/GPU production computation;
- V4 for core completion;
- fast math in core benchmarks;
- any numerical tolerance, near-tie threshold, or ambiguous-case acceptance rule not yet approved;
- hardware-specific launch, occupancy, vectorization, tiling, or memory settings;
- a particular build system, API shape, source layout, or detailed implementation.

## 9. Change control

Changes to an approved requirement require explicit project-owner approval and a new specification or addendum version. Derived plans may be refined without changing this document only when they remain consistent with every approved requirement. Unresolved decisions are tracked in [DecisionRegister.md](../governance/DecisionRegister.md) and must be resolved before their dependent acceptance gates can pass.
