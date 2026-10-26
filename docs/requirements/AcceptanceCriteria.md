# Acceptance Criteria

Status: Canonical acceptance mapping\
Source: [FinalProjectSpecification-v1.0.md](../specifications/FinalProjectSpecification-v1.0.md)\
Open decisions: [DecisionRegister.md](../governance/DecisionRegister.md)

## 1. Verdict vocabulary

- **Passed:** Executed evidence satisfies every approved condition for the gate.
- **Failed:** Executed evidence violates at least one approved condition for the gate.
- **Blocked:** A required decision, tool, environment, hardware capability, revision identifier, or other prerequisite is unavailable.
- **Skipped:** Explicitly outside the evaluated scope; skipped never means passed.
- **Planned:** Procedure only; no execution claim.

Unless a row says otherwise, the evidence must identify the tested revision/configuration and retain exact commands and traceable output. Current S4.2 documents are plans and contain no executed project evidence.

## 2. Functional and input gates

| Gate | Acceptance condition | Required evidence | Dependencies/status notes |
|---|---|---|---|
| AC-001 — Input-domain handling | NaN, positive/negative infinity, `N = 0`, and `D = 0` are rejected; positive and negative zero are accepted. | Executed cases for each class against every applicable implementation, recording returned status/result. | Exact API/error representation depends on UD-005, but accept/reject behavior does not. |
| AC-002 — Distance semantics | Reported distance is Float32 squared Euclidean distance over exactly `D` components, with no square root. | Hand-checkable fixtures plus Float64 diagnostic/reference output and per-implementation result. | Numerical-error verdict also uses AC-006. |
| AC-003 — Exhaustive search | Every candidate index in `[0, N)` participates in the exact search. | Fixtures whose nearest candidate occurs at the beginning, middle, and end, plus design/code review evidence once implementation exists. | Detailed implementation is UD-005. |
| AC-004 — Equal-computed-distance tie | For candidates with equal computed Float32 distances in the implementation under test, the smallest candidate index is returned. | Deliberate exact-tie and duplicate-vector cases, with computed distances and returned index retained. | Does not resolve near-tie ambiguity; see AC-007 and AC-008. |
| AC-005 — Argmin validation | The implementation argmin index is recorded and compared separately with the Float64 reference argmin and with the applicable Float32 tie contract. | Per-case implementation index, Float64 reference index, relevant distances, ambiguity classification, and independent verdict field. | Verdict for ambiguous index disagreements is blocked by UD-003. Unambiguous disagreements fail. |

## 3. Numerical gates

| Gate | Acceptance condition | Required evidence | Dependencies/status notes |
|---|---|---|---|
| AC-006 — Distance error | The Float32 reported distance is compared with the Float64 reference using the approved error metric and tolerance, with the numeric error recorded separately from index agreement. | Float32 value, Float64 value, computed error metric, approved tolerance identifier, and verdict. | **Blocked by UD-001.** Data may be collected, but no pass/fail tolerance verdict may be invented. |
| AC-007 — Numerical ambiguity classification | Each applicable result is classified using the approved Float64 near-tie diagnostic rule, and the classification remains separate from acceptance. | Best and relevant competing Float64 distances, threshold/rule identifier, classification, and diagnostic details. | **Blocked by UD-002.** No threshold may be inferred. |
| AC-008 — Ambiguous-case acceptance | A case classified as ambiguous receives the explicitly approved acceptance treatment; Float64 near-tie status alone is not acceptance. | Ambiguity classification, separate index/error observations, approved policy identifier, and final verdict rationale. | **Blocked by UD-003** and depends on AC-006/AC-007 policy. |
| AC-009 — Float32 finiteness | Every core valid test dataset produces finite squared distances in the Float32 evaluation domain. | Dataset-generation bounds/inspection and executed finite-result assertions. | Overflow-stress behavior is outside core acceptance unless separately approved. |

## 4. Variant and dataset gates

| Gate | Acceptance condition | Required evidence | Dependencies/status notes |
|---|---|---|---|
| AC-010 — V0–V3 contract preservation | V0, V1, V2, and V3 each satisfy AC-001–AC-009 under one common public behavior contract. | Per-variant test matrix and design/review evidence; no variant-specific waiver. | Hardware/detail choices remain UD-004/UD-005. AC-006–AC-008 inherit their current blocks. |
| AC-011 — Required coverage | Core evaluation covers all 12 combinations of approved `N` and `D` values using a recorded seed, plus the derived edge-case catalog. | Manifest listing seed, generator/configuration, every `(N,D)` pair, edge-case IDs, implementation variant, and result locations. | Batch cases are reported separately. Resource failure is blocked, not skipped without rationale. |
| AC-016 — Scope completion | Core completion includes V0–V3 evidence. V4 is optional and cannot compensate for missing V0–V3 evidence. | Completion matrix showing each core variant and separately labeled optional work. | No V4 result is required for a core pass. |

## 5. Performance and reproducibility gates

| Gate | Acceptance condition | Required evidence | Dependencies/status notes |
|---|---|---|---|
| AC-012 — Correctness-before-performance | Performance acceptance is evaluated only after correctness is accepted for the same relevant revision, build configuration, workload, and implementation variant. | Linked correctness report with accepted status and matching revision/configuration identifiers. | Currently dependent on unresolved AC-006–AC-008 policy. Measurements may be collected only as non-accepted exploratory data if separately authorized and clearly labeled. |
| AC-013 — Benchmark protocol | Each reported configuration has 10 warm-ups and 100 measured repetitions; kernel-only and end-to-end distributions are separate; median, p5, and p95 are reported; timing boundaries identify transfer and preprocessing treatment. | Raw/traceable samples, iteration counts, statistic calculation record, and boundary declaration. | Detailed hardware settings, if used, are recorded rather than promoted to requirements (UD-004). |
| AC-014 — Environment and revision reproducibility | Before measurement, the report captures actual revision/working-tree state, GPU, driver, CUDA Toolkit, compiler, OS/environment, build configuration, fast-math state, seed, workload, and commands. | Environment manifest and exact commands captured with the run. | Missing Git/revision evidence or required hardware makes performance acceptance blocked. |
| AC-015 — Benchmark policy | Core benchmark builds have fast math disabled; single-query results are primary; batch and any V4 results are reported separately; the hypothesis outcome is `supported`, `rejected`, or `inconclusive`. | Build configuration evidence, workload labels, comparison table, stated pre-run hypothesis/interpretation, and outcome. | No absolute speedup target is currently approved. A rejected hypothesis remains a valid experiment outcome. |

## 6. Current gate status at S4.2

| Category | Status | Reason |
|---|---|---|
| Documentation definition | Ready | Approved requirements, derived plans, and unresolved decisions are separated and traceable. |
| Executed correctness | Not executed | No CPU/CUDA implementation exists; implementation is outside S4.2. |
| AC-006 | Blocked for future verdict | UD-001 is unresolved. |
| AC-007 | Blocked for future verdict | UD-002 is unresolved. |
| AC-008 | Blocked for future verdict | UD-003 is unresolved. |
| Performance acceptance | Blocked for future verdict | Correctness is not yet accepted and revision-bound implementation evidence does not exist. |
