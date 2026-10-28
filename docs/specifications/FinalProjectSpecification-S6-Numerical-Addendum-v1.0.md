# Final Project Specification — S6 Numerical Policy Addendum v1.0

Status: Canonical numerical-policy addendum\
Approval date: 2026-10-28 (Asia/Bangkok)\
Approving role: Project Owner\
Baseline: `ad6487ef58343a78a54039fc95fd4e577f189e34`\
Approved policy: [S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md](S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md)\
Approved policy SHA-256: `503aa6b2c97567d814a9316fb0f2f9890d5472aea2bcf1f928ac505c29d1aa39`\
Approval record: [S6NumericalPolicyApprovalRecord.md](../governance/S6NumericalPolicyApprovalRecord.md)

## 1. Authority and scope

This addendum amends [FinalProjectSpecification-v1.0.md](FinalProjectSpecification-v1.0.md) for the numerical-policy scope approved at S6. The approved policy carrier is incorporated by reference in full. If this addendum conflicts with the v1.0 baseline on the status or content of UD-001, UD-002, UD-003, the production and reference arithmetic graphs, or their dependent acceptance treatment, this addendum controls.

The Owner's explicit ratification of the carrier SHA-256 supersedes statements inside the immutable carrier saying that checksum ratification is pending. It does not alter the approved mathematical, acceptance, or evidence clauses and does not claim historical byte-authentication of earlier R3 materials.

## 2. Amended requirements

- **REQ-N-003 — Approved arithmetic graphs:** Float32 CPU and CUDA V0–V3 use the approved ascending-dimension sequential Float32 fused-FMA graph. The Float64 diagnostic reference uses the approved ascending-dimension sequential Float64 fused-FMA graph after exact binary32-to-binary64 input promotion. Graph conformance and the required arithmetic environment are validated independently of numerical-bound compliance.
- **REQ-N-006 — Approved numerical policy:** Distance-error acceptance, contender classification, boundary handling and ambiguous-case treatment use `S6-UD001-DIST-BOUND-v1.0`, `S6-UD002-RANK-INTERVAL-v1.0` and `S6-UD003-QCA-v1.0` as defined by the approved carrier. No substitute local tolerance, near-tie threshold or verdict rule is permitted.
- **REQ-C-003 — Ordered evidence and verdict handling:** A demonstrated correctness defect is `failed`. Without a demonstrated defect, missing or invalid mandatory evidence is `blocked/unverified`. Accepted classifications are available only after every applicable independent correctness gate has passed. Contract-invalid input rejection is recorded separately and receives no nearest-neighbor verdict.

## 3. Normative numerical policies

The following carrier sections are normative:

1. N01–N03 arithmetic graphs and environment.
2. N04–N05 / UD-001 every-candidate combined distance bound and exact-zero rule.
3. N06–N07 / UD-002 exact and conservative ranking intervals.
4. N08–N10 / UD-003 ordered verdict semantics.
5. Verification, report and benchmark boundaries.
6. Replacement clauses for AC-005, AC-007, AC-008 and AC-012.
7. Policy version manifest and governance boundary.

Conservative membership in `Ĉ` is never proof of exact membership or exact ambiguity. Policy A permits qualified `accepted-ambiguous` only when all independent correctness gates pass and the required conservative provenance and boundary uncertainty are retained.

## 4. Input and domain preservation

NaN, positive or negative infinity, `N = 0`, and `D = 0` remain contract-invalid and must be rejected. Signed zero remains valid. A valid input outside the approved numerical domain is not converted into an invalid-input rejection. Required finite-core nonfinite arithmetic is a correctness failure; observed outside-domain overflow remains diagnostic unless separately approved.

## 5. Correctness and performance boundary

Policy approval closes the numerical-policy decisions but does not constitute executed correctness evidence. Runtime graph behavior, RNE, fused FMA, FTZ/DAZ state, compiler transformations, interval implementation, production paths and reductions remain unverified until executed against a revision-bound implementation.

Both `accepted-unambiguous` and qualified `accepted-ambiguous` may satisfy the correctness prerequisite for performance only after the complete applicable matrix passes for the same revision, working-tree state, build configuration, workload, arithmetic environment and implementation variant.

## 6. Unchanged scope

Primary single-query scope, secondary batch reporting, V0–V3 core scope, optional V4 status, required datasets, benchmark sampling/statistics, fast-math prohibition and reproducibility requirements remain unchanged. UD-004 remains open. UD-005 is resolved only for the approved arithmetic accumulation graph; its remaining API, layout, ownership, launch and reduction architecture choices remain open.

This addendum does not authorize implementation, environment changes, builds, tests, hardware probes, benchmarks or Git/GitHub operations.
