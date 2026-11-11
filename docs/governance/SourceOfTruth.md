# Source of Truth

Status: Canonical governance record\
Milestone: S15 — Approved public SIFT1M benchmark design; S6 contracts retained\
Effective date: 2026-11-11 (Asia/Bangkok)

## 1. Canonical sources

| Priority within its responsibility | Source | Responsibility |
|---|---|---|
| 1 | [FinalProjectSpecification-v1.0.md](../specifications/FinalProjectSpecification-v1.0.md) | Approved baseline behavior, optimization scope, datasets, benchmark policy and contracts not amended by a later addendum. |
| 1, later scoped addendum | [FinalProjectSpecification-S6-Numerical-Addendum-v1.0.md](../specifications/FinalProjectSpecification-S6-Numerical-Addendum-v1.0.md) and its exact [approved policy carrier](../specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md) | Approved S6 arithmetic graphs, UD-001–UD-003 policies, acceptance amendments, evidence obligations and numerical governance boundary. Controls over the baseline only within that scope. |
| Approval evidence | [S6NumericalPolicyApprovalRecord.md](S6NumericalPolicyApprovalRecord.md) | Exact policy identifier, SHA-256, date, baseline, approving role and authorization boundary. |
| Approved design | [S6ApprovedImplementationDesign-v1.0.md](../design/S6ApprovedImplementationDesign-v1.0.md) | Owner-approved implementation choices for the UD-004 method and UD-005 V0–V3 architecture. It specializes implementation without changing the specification or numerical policy. |
| Design approval evidence | [S6DesignApprovalRecord.md](S6DesignApprovalRecord.md) | Approval ID, source manifest, COND-01–03, scope, provenance limitations and authorization boundary. |
| Approved experiment design | [S15PublicSift1mBenchmarkDesignDecision-v1.0.md](../design/S15PublicSift1mBenchmarkDesignDecision-v1.0.md) | Owner-approved formal public SIFT1M single-query experiment matrix, correctness prerequisite, timing/statistical method, performance interpretation, dataset identity and evidence-integrity design. It supplements rather than replaces required synthetic core evidence and does not authorize formal performance execution. |
| Experiment-design approval evidence | [S15PublicSift1mBenchmarkApprovalRecord.md](S15PublicSift1mBenchmarkApprovalRecord.md) | Exact approved candidate SHA-256, all 12 confirmed decisions, approval date and role, implementation/correctness-integrity authorization, absence of an assigned approval ID, and the separate Owner gate for formal performance benchmark execution. |
| 2 | [DecisionRegister.md](DecisionRegister.md) | Status and history of approved, design-resolved, measurement-dependent and unresolved decisions. It records but does not override the specification or numerical policy. |
| Operational | [DevelopmentAndMergePolicy.md](DevelopmentAndMergePolicy.md) | Approved branch, pull-request, human-review, CI, merge, and emergency process. It governs repository operation but cannot amend project semantics. |
| 3 | [AcceptanceCriteria.md](../requirements/AcceptanceCriteria.md) | Observable gates derived from the approved specification. |
| 4 | [RequirementsTraceability.md](../requirements/RequirementsTraceability.md) | Mapping among requirements, gates, plans, and evidence destinations. |
| 5 | [Architecture.md](../design/Architecture.md), [CorrectnessPlan.md](../verification/CorrectnessPlan.md), [BenchmarkProtocol.md](../performance/BenchmarkProtocol.md), and [ExperimentTemplate.md](../experiments/ExperimentTemplate.md) | Derived design and execution plans. These may specialize process details but cannot establish or weaken project requirements. |
| Operational | `.agents/skills/` | Workflow, routing, safety, and evidence discipline. It does not define project semantics. |
| Observed | Future repository configuration and implementation | Executable mechanics and current state. Observed behavior cannot amend an approved contract. |

The approval package supplied for S4.2 is the provenance for this initial canonical set: the approved Project Specification baseline, Technical Contract Addendum V1.0-A (U01–U08), and the additional approved policies stated in the S4.2 authorization.

The S6 numerical addendum and approval record are the provenance for the later numerical-policy amendment. The approved carrier SHA-256 is `503aa6b2c97567d814a9316fb0f2f9890d5472aea2bcf1f928ac505c29d1aa39`. The S6 approval establishes new exact-file provenance and does not claim historical byte-authentication of earlier R3 materials.

The S15 approval record is the provenance for the public SIFT1M experiment
design approved on 2026-11-11 (Asia/Bangkok). It binds the 12 decisions to
candidate SHA-256
`4d91b8a659b94100654077988491a4a1b00a1405c067bfad508324bd267e0b16`,
authorizes implementation and correctness/integrity verification, and retains a
separate explicit Owner gate for formal performance benchmark execution. It
does not amend the canonical S6 numerical policy or replace synthetic V0–V3
core evidence.

## 2. Statement classes

Every normative or planning statement in this documentation set belongs to one of these classes:

- **Approved requirement:** Explicitly present in the approved baseline, Addendum V1.0-A, or additional approved policy. Requirement IDs in the final specification identify these statements.
- **Derived plan:** A proposed architecture, verification procedure, timing boundary, evidence format, or traceability mapping that implements approved requirements without adding a new product contract.
- **Unresolved decision:** A choice explicitly left open or necessary for a future verdict. It must be recorded and cannot be silently resolved.
- **Observed evidence:** A command result, environment fact, measurement, or repository state captured when work is executed. No future observation is asserted by these planning documents.

## 3. Conflict resolution

1. Safety and applicable agent instructions govern how work is performed.
2. For project semantics, the final project specification is authoritative.
3. A decision-register entry is authoritative about decision status, but an entry cannot override the specification without an approved specification/addendum update.
4. Within the approved S6 numerical-policy scope, the S6 addendum controls over superseded REQ-N-003, REQ-N-006 and REQ-C-003 wording in the v1.0 baseline.
5. Within the approved S6 implementation-design scope, the design approval
   record and design carrier control derived implementation documents.
   COND-01–03 control over the v1.0/v1.1 reports where they clarify them.
6. The approved design may specialize API, layout, ownership, launch,
   reduction, timing and tuning method only when consistent with canonical
   requirements and the numerical policy.
7. Within S15 public-dataset experiment scope, the approved S15 design controls
   the query matrix, V2 mapping, correctness prerequisite, timing/statistical
   method, hypothesis interpretation, dataset hashes and evidence-integrity
   mechanics. It cannot override the specification, S6 numerical policy or S6
   implementation design, and it cannot replace required synthetic evidence.
8. Derived documents must be corrected if they conflict with the specification,
   an applicable approved addendum or the approved design within its scope.
9. Implementation or configuration that conflicts with the specification is
   drift, not a contract change.
10. A material conflict or missing decision that prevents a valid verdict makes
   the affected gate `blocked` until an authorized resolution is recorded.

## 4. Change control

- Requirement changes require explicit project-owner approval and versioned canonical documentation.
- Resolved open decisions must identify the approval evidence, date, affected requirement and acceptance IDs, and documents synchronized.
- A hash-bound policy carrier must be copied without content or line-ending normalization unless a newly hashed version receives separate approval.
- Design-source approval by title/version is not byte-level source authentication. No checksum may be invented.
- The repository-canonical design revision is the Owner-created squash commit that publishes the reviewed carrier and approval record.
- Design-level closure does not establish ABI, compiler, arithmetic, correctness, hardware-capacity or performance evidence.
- Policy-decision closure must remain distinct from runtime gate completion.
- Architecture and plan refinements may be made as derived documentation changes only when they do not choose a reserved unresolved policy.
- Experiment results never rewrite requirements. They may support, reject, or leave a hypothesis inconclusive.
- All experiment evidence must identify a code revision and working-tree state. Until Git exists, revision-bound experiments are blocked.
- Repository workflow policy and GitHub settings may strengthen how changes are reviewed and integrated, but they cannot establish or weaken a numerical, optimization, dataset, or acceptance contract.
