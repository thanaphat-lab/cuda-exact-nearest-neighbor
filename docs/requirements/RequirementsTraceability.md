# Requirements Traceability

Status: Canonical traceability index\
Specifications: [FinalProjectSpecification-v1.0.md](../specifications/FinalProjectSpecification-v1.0.md) and [S6 Numerical Policy Addendum v1.0](../specifications/FinalProjectSpecification-S6-Numerical-Addendum-v1.0.md)\
Acceptance gates: [AcceptanceCriteria.md](AcceptanceCriteria.md)

## 1. Workload, search, and numerical requirements

| Requirement | Source decision | Acceptance gates | Design/plan destinations |
|---|---|---|---|
| REQ-W-001 — Primary single query | DR-001 | AC-011, AC-013, AC-015 | Architecture workload boundary; correctness matrix; benchmark primary track |
| REQ-W-002 — Secondary batch query | DR-001 | AC-011, AC-015, AC-016 | Separate batch track; optional V4 track |
| REQ-S-001 — L2² | DR-002 | AC-002, AC-006 | Architecture common contract; correctness oracle |
| REQ-S-002 — Exhaustive exact search | DR-002 | AC-003, AC-005 | Candidate evaluation and argmin stages |
| REQ-S-003 — Smallest-index equal-distance tie | DR-002 | AC-004, AC-005 | Common result-selection contract |
| REQ-N-001 — Float32 CPU/GPU | DR-003 | AC-002, AC-004–AC-010 | Float32 implementation lanes |
| REQ-N-002 — Float64 reference | DR-003 | AC-002, AC-005–AC-008 | Reference/diagnostic lane |
| REQ-N-003 — Approved arithmetic graphs | DR-004, DR-020 | AC-002, AC-005, AC-006, AC-010 | Shared ascending-dimension sequential FMA graphs; independent graph-conformance validator |
| REQ-N-004 — Separate index/error/ambiguity | DR-005 | AC-005–AC-008 | Validator evidence schema |
| REQ-N-005 — Near ties diagnostic only | DR-006 | AC-007, AC-008 | Diagnostic classifier, no automatic verdict |
| REQ-N-006 — Approved numerical policy | DR-021–DR-023 | AC-005–AC-008, AC-012 | Every-candidate bound; exact/conservative contender classification; ordered verdict and provenance fields |

## 2. Input, optimization, and dataset requirements

| Requirement | Source decision | Acceptance gates | Design/plan destinations |
|---|---|---|---|
| REQ-I-001 — Reject invalid inputs | DR-007 | AC-001 | Input validation boundary and invalid-case suite |
| REQ-I-002 — Accept signed zero | DR-007 | AC-001 | Signed-zero cases |
| REQ-I-003 — Finite Float32 core distances | DR-008 | AC-009 | Dataset constraints and finite assertions |
| REQ-O-001 — V0 naive CUDA | DR-010, DR-027 | AC-010, AC-016 | Approved design: AoS candidate kernel plus serial device reducer |
| REQ-O-002 — V1 coalescing | DR-010, DR-027 | AC-010, AC-016 | Approved design: host-prepared SoA plus serial device reducer |
| REQ-O-003 — V2 block reduction | DR-010, DR-027 | AC-010, AC-016 | Approved shared-memory and pair-hierarchy design |
| REQ-O-004 — V3 optimized reduction | DR-010, DR-027; COND-01, COND-02 | AC-010, AC-016 | Approved full-warp safe-source hierarchy and independent nonfinite status propagation |
| REQ-O-005 — Common V0–V3 contracts | DR-009, DR-027 | AC-010 | Approved typed API, common reduction semantics and verification matrix |
| REQ-O-006 — Optional V4 | DR-010 | AC-015, AC-016 | Separate optional batch/data-reuse path |
| REQ-D-001 — Seeded synthetic data | DR-011 | AC-011, AC-014 | Dataset manifest and experiment metadata |
| REQ-D-002 — Required `N × D` matrix | DR-011 | AC-011, AC-013 | Correctness/benchmark matrices |
| REQ-D-003 — Extended edge cases | DR-011 | AC-001–AC-009, AC-011 | Correctness edge-case catalog |

## 3. Correctness, benchmark, and reproducibility requirements

| Requirement | Source decision | Acceptance gates | Design/plan destinations |
|---|---|---|---|
| REQ-C-001 — Correctness first | DR-016 | AC-012 | Workflow gate before performance acceptance |
| REQ-C-002 — Independent observations | DR-005 | AC-005–AC-008 | Correctness report schema |
| REQ-C-003 — Ordered evidence and verdict handling | DR-023 | AC-005–AC-008, AC-012 | Demonstrated failure precedence; missing mandatory evidence blocks; qualified acceptance only after all gates pass |
| REQ-C-004 — Evidence statuses | Final Project Specification v1.0 (REQ-C-004) | All | Verification and experiment status tables |
| REQ-B-001 — Two timing classes | DR-012, DR-027 | AC-013 | `BND-KERNEL`, primary `BND-E2E-RAW`, supplementary `BND-E2E-PREP` |
| REQ-B-002 — 10/100 iterations | DR-013 | AC-013 | Run procedure and raw-sample checks |
| REQ-B-003 — Median/p5/p95 | DR-013 | AC-013 | Statistics output schema |
| REQ-B-004 — Explicit timing costs | DR-012, DR-027 | AC-013 | Approved named boundary inclusion/exclusion rules |
| REQ-B-005 — Real environment metadata | DR-014 | AC-014 | Pre-run manifest |
| REQ-B-006 — Fast math disabled | DR-015 | AC-014, AC-015 | Build evidence |
| REQ-B-007 — Workload separation | DR-001, DR-010 | AC-013, AC-015, AC-016 | Separate result tables |
| REQ-B-008 — No implied target | Final Project Specification v1.0 (REQ-B-008) | AC-015 | Pre-run hypothesis section |
| REQ-R-001 — Revision binding | DR-017 | AC-014 | Experiment identity |
| REQ-R-002 — Reproduction evidence | DR-017, DR-026 | AC-013–AC-015 | Commands, metadata, independent tuning/final samples, freeze manifest and raw-output references |
| REQ-R-003 — Three hypothesis outcomes | DR-017 | AC-015 | Experiment conclusion |
| REQ-R-004 — No fabrication | DR-017 | All evidence gates | Missing/blocked fields retained explicitly |

## 4. Addendum coverage

| Addendum item | Canonical requirements | Acceptance gates |
|---|---|---|
| U01 | REQ-N-003 | AC-005–AC-008 |
| U02 | REQ-N-004, REQ-C-002 | AC-005–AC-008 |
| U03 | REQ-N-005 | AC-007, AC-008 |
| U04 | REQ-I-001, REQ-I-002 | AC-001 |
| U05 | REQ-I-003 | AC-009 |
| U06 | REQ-O-005 | AC-010 |
| U07 | REQ-B-004 | AC-013 |
| U08 | REQ-B-005 | AC-014 |

## 5. S6 numerical-policy coverage

| Policy | Canonical requirements | Acceptance gates | Primary evidence destination |
|---|---|---|---|
| `S6-NUM-GRAPH-v1.0` | REQ-S-001–REQ-S-003; REQ-N-001–REQ-N-003 | AC-002–AC-006, AC-010 | Architecture graph boundary; every-candidate graph validator |
| `S6-NUM-ENV-v1.0` | REQ-N-003; REQ-R-001–REQ-R-004 | AC-005, AC-006, AC-009, AC-014 | Runtime/compiler arithmetic-environment evidence |
| `S6-UD001-DIST-BOUND-v1.0` | REQ-N-004, REQ-N-006; REQ-C-002, REQ-C-003 | AC-005, AC-006, AC-008, AC-012 | Every-candidate bound report and exact-zero fixtures |
| `S6-UD002-RANK-INTERVAL-v1.0` | REQ-N-004–REQ-N-006 | AC-005, AC-007, AC-008 | Exact/outward endpoints, `C`, `Ĉ`, uncertainty and provenance |
| `S6-NUM-TIE-LABELS-v1.0` | REQ-S-003; REQ-N-004 | AC-004, AC-005, AC-008 | Mathematical, Float64 and Float32 tie fields |
| `S6-UD003-QCA-v1.0` | REQ-N-004–REQ-N-006; REQ-C-001–REQ-C-003 | AC-005, AC-008, AC-012 | Ordered decision trace and final NN verdict |
| `S6-NUM-EVIDENCE-v1.0` | REQ-C-001–REQ-C-004; REQ-R-001–REQ-R-004 | AC-003–AC-012, AC-014 | Independent validator, stress oracle, direct reduction and production-path evidence |

## 6. Evidence destinations

- Approved implementation design: [S6ApprovedImplementationDesign-v1.0.md](../design/S6ApprovedImplementationDesign-v1.0.md)
- S6 design approval provenance: [S6DesignApprovalRecord.md](../governance/S6DesignApprovalRecord.md)
- Architecture and component ownership: [Architecture.md](../design/Architecture.md)
- Correctness cases and report schema: [CorrectnessPlan.md](../verification/CorrectnessPlan.md)
- Measurement execution and timing definitions: [BenchmarkProtocol.md](../performance/BenchmarkProtocol.md)
- Revision-bound report shell: [ExperimentTemplate.md](../experiments/ExperimentTemplate.md)
- Open decisions and affected gates: [DecisionRegister.md](../governance/DecisionRegister.md)
- Approved S6 numerical policy: [S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md](../specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md)
- Exact approval binding: [S6NumericalPolicyApprovalRecord.md](../governance/S6NumericalPolicyApprovalRecord.md)

No row in this matrix is execution evidence. It is a coverage map for future implementation, verification, benchmarking, and reporting.
