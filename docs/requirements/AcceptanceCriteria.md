# Acceptance Criteria

Status: Canonical acceptance mapping\
Sources: [FinalProjectSpecification-v1.0.md](../specifications/FinalProjectSpecification-v1.0.md) and [S6 Numerical Policy Addendum v1.0](../specifications/FinalProjectSpecification-S6-Numerical-Addendum-v1.0.md)\
Decision status: [DecisionRegister.md](../governance/DecisionRegister.md)

## 1. Verdict vocabulary

- **Passed:** Executed evidence satisfies every approved condition for the gate.
- **Failed:** Executed evidence violates at least one approved condition for the gate.
- **Blocked:** A required decision, tool, environment, hardware capability, revision identifier, or other prerequisite is unavailable.
- **Skipped:** Explicitly outside the evaluated scope; skipped never means passed.
- **Planned:** Procedure only; no execution claim.

Unless a row says otherwise, the evidence must identify the tested revision/configuration and retain exact commands and traceable output. These synchronized documents contain no executed project evidence.

The NN-policy tokens `accepted-unambiguous`, `accepted-ambiguous`, `failed`, and `blocked/unverified` are per-case numerical outcomes. They do not replace the gate-level evidence statuses above. Contract-invalid input uses a separate input outcome and `nn_verdict=not-applicable`.

## 2. Functional and input gates

| Gate | Acceptance condition | Required evidence | Dependencies/status notes |
|---|---|---|---|
| AC-001 — Input-domain handling | NaN, positive/negative infinity, `N = 0`, and `D = 0` are rejected; positive and negative zero are accepted. | Executed cases for each class against every applicable implementation, recording returned status/result. | Exact API/error representation depends on UD-005, but accept/reject behavior does not. |
| AC-002 — Distance semantics | Reported distance is Float32 squared Euclidean distance over exactly `D` components, with no square root. | Hand-checkable fixtures plus Float64 diagnostic/reference output and per-implementation result. | Numerical-error verdict also uses AC-006. |
| AC-003 — Exhaustive search | Every candidate index in `[0, N)` participates in the exact search. | Fixtures whose nearest candidate occurs at the beginning, middle, and end, plus design/code review evidence once implementation exists. | Detailed implementation is UD-005. |
| AC-004 — Equal-computed-distance tie | For candidates with equal computed Float32 distances in the implementation under test, the smallest candidate index is returned. | Deliberate exact-tie and duplicate-vector cases, with computed distances and returned index retained. | Does not resolve near-tie ambiguity; see AC-007 and AC-008. |
| AC-005 — Argmin validation | For every contract-valid case in the approved numerical domain, the returned implementation pair is independently verified as the lexicographic minimum of `(computed Float32 squared distance, candidate index)` over every candidate. The implementation index is compared separately with the Float64 reference index and classified against the approved exact contender set `C`, or against the verified conservative set `Ĉ` only when exact boundary membership remains unresolved. Float64 index agreement alone is not acceptance. A disagreement when the exact contender set is a singleton fails. | Per-case implementation index and distance; independent all-candidate Float32 graph and lexicographic verification; Float64 reference index and relevant distances; Float32, Float64, and mathematical tie fields; exact contender set or unresolved marker; conservative contender set when applicable; contender provenance; boundary-uncertain indices; UD-001 status; and independent NN verdict. | Depends on AC-004, AC-006, AC-007, and AC-008 and the approved UD-001–UD-003 policy versions. Invalid-input outcomes remain governed by AC-001 and receive no NN verdict. |

## 3. Numerical gates

| Gate | Acceptance condition | Required evidence | Dependencies/status notes |
|---|---|---|---|
| AC-006 — Distance error | Under `S6-UD001-DIST-BOUND-v1.0`, define `u32 = 2^-24`, `u64 = 2^-53`, `eta32 = 2^-150`, `gamma_k^(p) = k*u_p/(1-k*u_p)`, `E32(D) = (1+u32)^2*(1+gamma_D^(32))-1`, `E64(D) = (1+u64)^2*(1+gamma_D^(64))-1`, `A(D) = D*eta32*(1+gamma_D^(32))`, and `R(D) = (E32(D)+E64(D))/(1-E64(D))`. Every candidate in every required correctness case satisfies `abs(d_i^32-d_i^64) <= A(D)+R(D)*d_i^64`. For an exact input match, including signed-zero numeric equality, `Q_i = 0` implies `d_i^32 = d_i^64 = +0`. Bound compliance and arithmetic-graph conformance are separate mandatory checks. | For every candidate: `D`, `d_i^32`, `d_i^64`, observed absolute error, policy/version identifier, evaluated `gamma`, `E32`, `E64`, `A`, `R` and allowed bound; exact-dyadic or certified outward-safe comparison method and trace; exact-zero assertion where applicable; all-candidate coverage; graph-conformance status; and independent verdict. | UD-001 is policy-resolved by `S6-UD001-DIST-BOUND-v1.0`. A demonstrated bound or exact-zero violation is `failed`. An inconclusive comparison, unsafe enclosure, missing candidate, missing graph evidence or other absent mandatory evidence is `blocked/unverified` unless another demonstrated defect already establishes failure. Policy approval does not mark AC-006 passed; runtime status remains `NOT EXECUTED`. |
| AC-007 — Numerical ambiguity classification | For every applicable candidate, compute the approved ranking radius `B_i^rank`, endpoints `L_i` and `U_i`, `U_* = min_i U_i`, and the exact contender predicate `L_i ≤ U_*`. Boundary equality is included. Exact arithmetic shall be used where practical; otherwise verified outward intervals shall classify candidates as confirmed contenders, confirmed non-contenders, or boundary-uncertain and shall construct `Ĉ = {i : lower(L_i) ≤ upper(U_*)}`. Conservative inclusion or boundary uncertainty shall not be reported as proven exact membership or exact ambiguity. | Policy identifier and version; `D`; each relevant `d_i^64`, radius, endpoint, and comparison; exact or outward-evaluation method; exact contender set when resolved; conservative set; boundary-uncertain indices; `exact_ambiguity=true\|false\|unknown`; and traceable comparison results. | Uses the independently versioned UD-002 ranking policy and requires valid UD-001 per-candidate containment and safe arithmetic. An unsafe implemented interval is failed; an otherwise valid but unresolved outward comparison follows AC-008 Policy A or is blocked when its mandatory evidence is incomplete. |
| AC-008 — Ambiguous-case acceptance | Apply the approved ordered UD-003 verdict policy. `accepted-unambiguous` requires every applicable correctness gate to pass, an exact singleton contender set, agreement with the Float64 reference index, and no unresolved boundary condition. `accepted-ambiguous` requires every applicable correctness gate to pass and either (a) an exact multi-candidate contender set containing the returned index, or (b) verified outward intervals whose conservative set contains the returned index while exact boundary membership remains unresolved. Case (b) shall record conservative provenance, `exact_ambiguity=unknown`, and boundary uncertainty. Exact or conservative contender membership alone never grants acceptance. Demonstrated defects are `failed`; absent mandatory evidence without a demonstrated defect is `blocked/unverified`. | UD-003 policy identifier; input/domain outcome; returned index; Float32 lexicographic correctness; Float64 index agreement; UD-001 verdict; exact and conservative contender fields; exact ambiguity; outward-boundary uncertainty; exhaustive-candidate, tie, graph, oracle, reduction, and production-path statuses; and an ordered decision trace. | Depends on AC-003–AC-007, AC-009–AC-011, and matching revision/configuration evidence. Invalid-input rejection is reported separately under AC-001 with `nn_verdict=not-applicable`. |
| AC-009 — Float32 finiteness | Every core valid test dataset produces finite squared distances in the Float32 evaluation domain. | Dataset-generation bounds/inspection and executed finite-result assertions. | Overflow-stress behavior is outside core acceptance unless separately approved. |

## 4. Variant and dataset gates

| Gate | Acceptance condition | Required evidence | Dependencies/status notes |
|---|---|---|---|
| AC-010 — V0–V3 contract preservation | V0, V1, V2, and V3 each satisfy AC-001–AC-009 under one common public behavior contract. | Per-variant test matrix and design/review evidence; no variant-specific waiver. | The UD-004 tuning method and UD-005 V0–V3 architecture are design-approved under `S6-DESIGN-OWNER-APPROVAL-001`. Actual hardware-selected values, ABI/compiler behavior and AC-001–AC-009 execution remain unverified. No variant-specific waiver is permitted. |
| AC-011 — Required coverage | Core evaluation covers all 12 combinations of approved `N` and `D` values using a recorded seed, plus the derived edge-case catalog. | Manifest listing seed, generator/configuration, every `(N,D)` pair, edge-case IDs, implementation variant, and result locations. | Batch cases are reported separately. Resource failure is blocked, not skipped without rationale. |
| AC-016 — Scope completion | Core completion includes V0–V3 evidence. V4 is optional and cannot compensate for missing V0–V3 evidence. | Completion matrix showing each core variant and separately labeled optional work. | No V4 result is required for a core pass. |

## 5. Performance and reproducibility gates

| Gate | Acceptance condition | Required evidence | Dependencies/status notes |
|---|---|---|---|
| AC-012 — Correctness-before-performance | Performance acceptance is evaluated only after the complete applicable correctness matrix is accepted for the same revision and working-tree state, build configuration, workload, arithmetic environment, and implementation variant. Both `accepted-unambiguous` and qualified `accepted-ambiguous` cases may satisfy this prerequisite only when issued under the approved UD-003 policy with all mandatory evidence complete and no required gate failed or blocked. One accepted case does not satisfy the matrix-level prerequisite. | Linked revision-bound correctness report identifying every applicable case and variant; per-case NN verdict and contender provenance; gate matrix with no required failed or blocked entries; exact revision, working-tree state, build configuration, workload, and arithmetic-environment identifiers. | Requires approved and synchronized UD-001–UD-003 policies and accepted AC-001–AC-011 evidence as applicable. Exploratory measurements remain non-accepted unless separately authorized and labeled. |
| AC-013 — Benchmark protocol | Each reported configuration has 10 warm-ups and 100 measured repetitions; kernel-only and end-to-end distributions are separate; median, p5, and p95 are reported; timing boundaries identify transfer and preprocessing treatment. | Raw/traceable samples, iteration counts, statistic calculation record, and boundary declaration. | Detailed hardware settings, if used, are recorded rather than promoted to requirements (UD-004). |
| AC-014 — Environment and revision reproducibility | Before measurement, the report captures actual revision/working-tree state, GPU, driver, CUDA Toolkit, compiler, OS/environment, build configuration, fast-math state, seed, workload, and commands. | Environment manifest and exact commands captured with the run. | Missing Git/revision evidence or required hardware makes performance acceptance blocked. |
| AC-015 — Benchmark policy | Core benchmark builds have fast math disabled; single-query results are primary; batch and any V4 results are reported separately; the hypothesis outcome is `supported`, `rejected`, or `inconclusive`. | Build configuration evidence, workload labels, comparison table, stated pre-run hypothesis/interpretation, and outcome. | No absolute speedup target is currently approved. A rejected hypothesis remains a valid experiment outcome. |

## 6. Current gate status after S6 numerical-policy approval

| Category | Status | Reason |
|---|---|---|
| Numerical policy definition | Approved | UD-001–UD-003 and their dependent acceptance wording are defined by the approved S6 policy. |
| Executed correctness | NOT EXECUTED | No CPU/CUDA implementation exists; implementation is outside this documentation synchronization. |
| AC-006 | NOT EXECUTED | Policy is defined; no revision-bound all-candidate runtime evidence exists. |
| AC-007 | NOT EXECUTED | Policy is defined; no exact/outward classifier evidence exists. |
| AC-008 | NOT EXECUTED | Policy is defined; no complete per-case correctness evidence exists. |
| AC-012 | BLOCKED | A matching accepted correctness matrix is unavailable. |
