# Source of Truth

Status: Canonical governance record\
Milestone: S4.2\
Effective date: 2026-10-26 (Asia/Bangkok)

## 1. Canonical sources

| Priority within its responsibility | Source | Responsibility |
|---|---|---|
| 1 | [FinalProjectSpecification-v1.0.md](../specifications/FinalProjectSpecification-v1.0.md) | Approved project behavior, numerical contracts, optimization scope, datasets, and acceptance policy. It incorporates Technical Contract Addendum V1.0-A. |
| 2 | [DecisionRegister.md](DecisionRegister.md) | Status and history of approved and unresolved decisions. It records but does not override the specification. |
| 3 | [AcceptanceCriteria.md](../requirements/AcceptanceCriteria.md) | Observable gates derived from the approved specification. |
| 4 | [RequirementsTraceability.md](../requirements/RequirementsTraceability.md) | Mapping among requirements, gates, plans, and evidence destinations. |
| 5 | [Architecture.md](../design/Architecture.md), [CorrectnessPlan.md](../verification/CorrectnessPlan.md), [BenchmarkProtocol.md](../performance/BenchmarkProtocol.md), and [ExperimentTemplate.md](../experiments/ExperimentTemplate.md) | Derived design and execution plans. These may specialize process details but cannot establish or weaken project requirements. |
| Operational | `.agents/skills/` | Workflow, routing, safety, and evidence discipline. It does not define project semantics. |
| Observed | Future repository configuration and implementation | Executable mechanics and current state. Observed behavior cannot amend an approved contract. |

The approval package supplied for S4.2 is the provenance for this initial canonical set: the approved Project Specification baseline, Technical Contract Addendum V1.0-A (U01–U08), and the additional approved policies stated in the S4.2 authorization.

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
4. Derived documents must be corrected if they conflict with the specification.
5. Implementation or configuration that conflicts with the specification is drift, not a contract change.
6. A material conflict or missing decision that prevents a valid verdict makes the affected gate `blocked` until an authorized resolution is recorded.

## 4. Change control

- Requirement changes require explicit project-owner approval and versioned canonical documentation.
- Resolved open decisions must identify the approval evidence, date, affected requirement and acceptance IDs, and documents synchronized.
- Architecture and plan refinements may be made as derived documentation changes only when they do not choose a reserved unresolved policy.
- Experiment results never rewrite requirements. They may support, reject, or leave a hypothesis inconclusive.
- All experiment evidence must identify a code revision and working-tree state. Until Git exists, revision-bound experiments are blocked.
