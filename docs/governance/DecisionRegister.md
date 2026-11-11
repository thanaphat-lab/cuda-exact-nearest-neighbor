# Decision Register

Status: Canonical decision-status record\
Milestone: S6 — Approved numerical policy and implementation design\
Last updated: 2026-11-11 (Asia/Bangkok)

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
| DR-018 | Normal changes require a focused branch, a pull request into `main`, explicit human review, passing applicable CI, and squash-only merge. Normal direct pushes to `main` are prohibited. Because protection and rulesets are unavailable for this private repository under the current plan, human compliance is the approved mitigation. | Project-owner S5.5-B authorization | Repository governance only; no project requirement changed |
| DR-019 | The exact carrier `S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0`, SHA-256 `503aa6b2c97567d814a9316fb0f2f9890d5472aea2bcf1f928ac505c29d1aa39`, is the approved S6 numerical-policy source. | Project Owner, 2026-10-28 | REQ-N-003–REQ-N-006; REQ-C-001–REQ-C-003 |
| DR-020 | Float32 CPU and CUDA V0–V3 use the approved ascending-dimension sequential fused-FMA graph; the Float64 diagnostic reference uses its approved corresponding graph and arithmetic environment. | S6 N01–N03; `S6-NUM-GRAPH-v1.0`; `S6-NUM-ENV-v1.0` | REQ-N-001–REQ-N-003; AC-002, AC-005, AC-006, AC-010 |
| DR-021 | UD-001 is resolved by the approved every-candidate combined absolute/relative bound, exact-zero rule and exact/outward-safe comparison policy. | `S6-UD001-DIST-BOUND-v1.0` | REQ-N-004, REQ-N-006; AC-006, AC-012 |
| DR-022 | UD-002 is resolved by the approved tight ranking intervals, inclusive boundary, exact contender set `C`, conservative outward set `Ĉ` and explicit boundary-uncertainty provenance. | `S6-UD002-RANK-INTERVAL-v1.0` | REQ-N-004–REQ-N-006; AC-005, AC-007, AC-008 |
| DR-023 | UD-003 is resolved by the approved ordered verdict policy and Conservative Qualified Acceptance Policy A. Invalid-input outcomes remain separate from nearest-neighbor verdicts. | `S6-UD003-QCA-v1.0` | REQ-N-004–REQ-N-006; REQ-C-002, REQ-C-003; AC-005, AC-008, AC-012 |
| DR-024 | Correctness evidence requires independent graph validation, every-candidate bound and argmin coverage, exact-dyadic stress cases, direct reduction fixtures and production-path verification. Diagnostic, oracle and report-generation costs remain outside production benchmark timings. | `S6-NUM-EVIDENCE-v1.0` | REQ-C-001–REQ-C-004; REQ-R-001–REQ-R-004; AC-005–AC-012 |
| DR-025 | `S6-DESIGN-OWNER-APPROVAL-001` approves the S6 implementation design assembled from the v1.0 report, the v1.1 refinement and controlling COND-01–03. No source checksum is claimed for the conversational reports. | Project Owner; [S6DesignApprovalRecord.md](S6DesignApprovalRecord.md) | UD-004, UD-005; requirements and gates mapped by the approved design record |
| DR-026 | UD-004's design method is approved: candidates `{128,256,512}`, correctness-gated independent tuning, kernel-only median selection, smaller block for exact measured ties, configuration freeze, correctness rerun and separation from final samples. Actual hardware winners and environment/resource facts remain unverified. | `S6-DESIGN-OWNER-APPROVAL-001`, COND-03 | REQ-O-001–REQ-O-005, REQ-B-001–REQ-B-008, REQ-R-001–REQ-R-004; AC-010, AC-012–AC-015 |
| DR-027 | UD-005's V0–V3 API, ownership, CPU/Float64, layout, reduction, timing and verification architecture is approved. COND-01 governs full-warp participation, masks, safe shuffle sources and hierarchy ties. COND-02 separates associative finite lexicographic argmin from associative nonfinite/domain-failure aggregation: real nonfinite candidates remain logically present, never enter the ordinary floating comparator and propagate failure status through the complete hierarchy. No mandatory failing-index selection rule is established. The proposed 16-byte transport record remains subject to ABI verification. V4 is deferred. | `S6-DESIGN-OWNER-APPROVAL-001`, COND-01, COND-02 | REQ-S-001–REQ-S-003, REQ-N-001–REQ-N-006, REQ-I-001–REQ-I-003, REQ-O-001–REQ-O-006; AC-001–AC-016 |
| DR-028 | S6 design documentation and future implementation integration use the approved focused-branch/PR process, and the Project Owner alone performs Squash & Merge into `main`. Codex may not merge or enable auto-merge. Branch deletion remains an Owner decision. | `S6-DESIGN-OWNER-APPROVAL-001`; DR-018 | Repository governance only |
| DR-029 | `S14-EVIDENCE-REMEDIATION-OWNER-APPROVAL-001` prospectively approves the S14 batch evidence-integrity design bound to SHA-256 `ff75b8f7462ff6177536ccc24b014300b66d31e97485c475dc0515e76b518664`: 1,000 ms telemetry cadence, 3,000 ms maximum gap, 900 ms bounded query timeout, independent lifecycle/continuity validation, fail-safe inconclusive treatment for incomplete telemetry, blocking treatment for malformed or identity-mismatched evidence, structured command/CTest provenance, and preservation of the prior run as historical-audit-limited. It does not retrospectively approve the historical experiment or authorize a replacement run. | Project Owner, 2026-11-11; [S14BatchBenchmarkApprovalRecord.md](S14BatchBenchmarkApprovalRecord.md) | REQ-B-005, REQ-R-001–REQ-R-004; optional V4 evidence only |

## 2. Resolved open decisions

| Former ID | Resolution | Approval evidence | Runtime status |
|---|---|---|---|
| UD-001 | Policy resolved by `S6-UD001-DIST-BOUND-v1.0`. | DR-019, DR-021; approved policy SHA-256 | Not executed; dependent runtime gates require implementation evidence. |
| UD-002 | Policy resolved by `S6-UD002-RANK-INTERVAL-v1.0`. | DR-019, DR-022; approved policy SHA-256 | Not executed. |
| UD-003 | Policy resolved by `S6-UD003-QCA-v1.0` and Policy A. | DR-019, DR-023; approved policy SHA-256 | Not executed. |
| UD-005 | Design-resolved for the approved V0–V3 API, ownership, data layouts, CPU/Float64 paths, V0/V1/V2/V3 reductions, timing boundaries and verification architecture. V4 is deferred. | DR-025, DR-027; `S6-DESIGN-OWNER-APPROVAL-001` | No ABI, compiler, correctness, hardware or performance evidence has executed. |

## 3. Unresolved and partially resolved decisions

These items are intentionally unresolved. Proposed values, defaults, library behavior, or implementation observations must not be treated as approval.

| ID | Unresolved decision | Why it is open | Affected acceptance gates | Required resolution evidence |
|---|---|---|---|---|
| UD-004 | **DESIGN METHOD APPROVED; HARDWARE VALUES UNVERIFIED.** Candidates, correctness qualification, independent tuning, selection rule, configuration freeze, correctness rerun and resource-evidence obligations are approved. Winning block size, occupancy, registers, spills, vector widths, memory capacity, compiler/toolchain compatibility and arithmetic-environment conformance remain measurement-dependent and unverified. | No runtime evidence exists; no hardware-specific value is Owner-approved as a constant. | AC-010, AC-012–AC-015 | Separately authorized preflight, correctness and experiment evidence bound to the selected revision/configuration. |

## 4. Gate and runtime-evidence rules

- Closing UD-001–UD-003 defines the policy; it does not provide executed correctness evidence.
- A demonstrated correctness defect is `failed`.
- Without a demonstrated defect, absent, invalid or unresolved mandatory evidence is `blocked/unverified`.
- Exact or conservative contender membership alone is never acceptance.
- Qualified `accepted-ambiguous` requires every applicable independent correctness gate to pass.
- Invalid-input outcomes are reported separately with no nearest-neighbor verdict.
- Unambiguous contract checks that do not depend on unresolved implementation decisions may still pass or fail independently.
- Performance measurements may not receive performance acceptance until the complete applicable correctness matrix is accepted for the matching revision, working-tree state, build, workload, arithmetic environment and variant.
- Design approval closes implementation-choice status only. It does not pass AC-001–AC-016 or establish actual ABI, compiler, device, correctness or performance behavior.
- Design approval authorizes no repository or runtime action. Documentation editing and each Git/GitHub stage require their applicable separate authorization. Only the Project Owner performs Squash & Merge into `main`; Codex may not merge or enable auto-merge.

## 5. Change history

| Date | Change |
|---|---|
| 2026-10-26 | Initial S4.2 register created from the approved baseline, Addendum V1.0-A, and additional approved policies. No unresolved decision was resolved. |
| 2026-10-27 | Recorded DR-018 and synchronized repository workflow governance for S5.5-B. UD-001 through UD-005 remain unresolved. |
| 2026-10-28 | Recorded the approved S6 policy carrier and resolved UD-001–UD-003 at the policy-decision level. Kept UD-004 open and UD-005 partially resolved. No runtime gate was marked passed. |
| 2026-10-29 | Recorded `S6-DESIGN-OWNER-APPROVAL-001`, design-level closure of UD-005, approved UD-004 methodology, controlling COND-01–03, deferred V4 and retained all runtime gates as not executed or blocked. |
| 2026-11-11 | Recorded prospective S14 evidence-integrity approval DR-029, including telemetry continuity/lifecycle controls and historical-audit-limited treatment. No replacement performance run was authorized or executed. |
