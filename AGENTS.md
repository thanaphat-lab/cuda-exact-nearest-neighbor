# Repository Agent Instructions

Scope: the repository root and all descendants unless a more specific `AGENTS.md` provides additional compatible instructions.

## Project Mission and Scope

Build and evaluate GPU-accelerated exhaustive exact nearest-neighbor search using CUDA. The primary workload is single query; batch query is secondary. Keep V0–V3 as the core optimization scope and V4 as optional stretch work.

This file governs how agents work. It does not define or amend project semantics, approve a design, authorize implementation, or record temporary milestone state.

## Canonical Documentation and Source of Truth

- [FinalProjectSpecification-v1.0.md](docs/specifications/FinalProjectSpecification-v1.0.md) is authoritative for approved behavior, numerical contracts, optimization scope, datasets, and acceptance policy.
- [SourceOfTruth.md](docs/governance/SourceOfTruth.md) defines source responsibilities, conflict resolution, and change control.
- [DecisionRegister.md](docs/governance/DecisionRegister.md) records approved decisions and unresolved decisions UD-001 through UD-005; it does not override the specification.
- [AcceptanceCriteria.md](docs/requirements/AcceptanceCriteria.md) defines observable acceptance gates.
- [RequirementsTraceability.md](docs/requirements/RequirementsTraceability.md) maps requirements to gates and evidence destinations.
- [Architecture.md](docs/design/Architecture.md), [CorrectnessPlan.md](docs/verification/CorrectnessPlan.md), [BenchmarkProtocol.md](docs/performance/BenchmarkProtocol.md), and [ExperimentTemplate.md](docs/experiments/ExperimentTemplate.md) are derived plans. They may not establish or weaken project requirements.

Use each source only for its assigned responsibility. Treat implementation or configuration that disagrees with the specification as drift, not as a contract update. Requirement changes require explicit project-owner approval and versioned canonical documentation.

## Skill Routing and Central Orchestrator

Route multi-stage tasks, lifecycle coordination, and gate decisions through [`cuda-dev-workflow`](.agents/skills/cuda-dev-workflow/SKILL.md). Use its state machine and routing references; do not skip required transitions.

Use a specialist directly for a genuinely bounded task:

| Outcome | Skill |
|---|---|
| Requirements, conflicts, or acceptance analysis | [`requirement-analysis`](.agents/skills/requirement-analysis/SKILL.md) |
| Approval-ready design without implementation | [`solution-design`](.agents/skills/solution-design/SKILL.md) |
| Authorized CUDA implementation | [`cuda-kernel-engineering`](.agents/skills/cuda-kernel-engineering/SKILL.md) |
| Build, test, and correctness evidence | [`verification`](.agents/skills/verification/SKILL.md) |
| Benchmarking, profiling, and performance assessment | [`cuda-performance`](.agents/skills/cuda-performance/SKILL.md) |
| Revision-bound experiment reporting | [`cuda-experiment-reporter`](.agents/skills/cuda-experiment-reporter/SKILL.md) |
| Findings-first artifact or change review | [`change-review`](.agents/skills/change-review/SKILL.md) |
| Documentation discovery and synchronization | [`docs-knowledge`](.agents/skills/docs-knowledge/SKILL.md) |
| Git and GitHub workflow | [`github-flow`](.agents/skills/github-flow/SKILL.md) |

Specialists do not inherit authority to perform adjacent stages. Use only the stages required by the request and stop at the requested milestone.

## Engineering Invariants

- Search is exhaustive exact nearest neighbor using squared Euclidean distance (L2²); equal computed distances select the smallest index.
- CPU/GPU production computation uses Float32. Float64 is the numerical reference and diagnostic path, not a production substitute or automatic acceptance mechanism.
- Validate argmin index, distance error, and numerical ambiguity separately.
- Reject NaN, infinity, `N = 0`, and `D = 0`; accept signed zero. Keep core squared distances finite in Float32.
- V0–V3 preserve one common distance, input, exact-search, output, and tie-breaking contract. V4 is optional and cannot replace core evidence.
- Seeded synthetic data, required workload coverage, benchmark sampling/statistics, and timing/environment requirements come from the canonical specification and acceptance documents; do not restate them differently in code or local plans.
- Disable fast math in core benchmarks. Keep single-query, batch-query, kernel-only, end-to-end, and optional V4 evidence separately identifiable.
- Do not invent a performance target, numerical tolerance, near-tie threshold, ambiguous-case verdict, hardware-specific setting, or detailed algorithm.
- Never silently resolve UD-001 through UD-005. Record an authorized resolution in the canonical decision/change process before relying on it.

## Workflow and Milestone Gates

At intake, inspect applicable instructions, repository state, canonical documents, current configuration, requested scope, and mutation authorization.

For full lifecycle work, follow:

`requirement-analysis -> solution-design -> cuda-kernel-engineering -> verification -> cuda-performance -> cuda-experiment-reporter -> change-review -> docs-knowledge`

Apply these gates:

- Requirements and all design-critical contracts must be resolved before design approval.
- Implementation requires an approved design and explicit task-specific local mutation authorization.
- Verification requires actual implementation/change evidence and authoritative executable commands.
- Correctness acceptance for the same relevant revision and configuration is required before performance acceptance.
- Benchmarking requires available compatible hardware, revision-bound evidence, actual environment capture, and an explicit hypothesis interpretation.
- Documentation-only, requirements-only, design-only, review-only, and Git/GitHub-only requests use their bounded workflow paths and do not imply implementation stages.
- A later-stage request does not retroactively satisfy an earlier gate or authorize unrelated mutations.

## Read-only, Local Mutation, and External Mutation Permissions

- Read-only inspection and analysis within the requested scope need no additional approval.
- Repository-local edits require an explicit change request and satisfied prerequisite gates. Preserve unrelated user changes.
- Source or CUDA implementation edits additionally require an approved design and authorization covering the target scope.
- Dependency installation, environment changes, destructive operations, repository initialization, and local Git mutations require specific authorization unless the current request explicitly asks for that exact action.
- Pushes, pull requests, issues, releases, uploads, messages, and all other external mutations require explicit approval immediately before execution.
- Authorization is task-specific. Never convert a prior task, milestone, or approval into standing permission.

## Verification and Evidence Standards

- Discover build, test, benchmark, and profiler commands from repository configuration or authoritative instructions; do not guess conventional commands.
- Record exact executed commands, revision and working-tree state, configuration, inputs/seed, toolchain, environment, GPU identity/availability, timing boundaries, and raw or traceable output as applicable.
- Report checks as `planned`, `passed`, `failed`, `skipped`, or `blocked`. Only executed evidence meeting an approved criterion is `passed`; skipped or unavailable work is never a pass.
- Keep Float32 CPU/GPU results distinct from Float64 reference evidence, and primary single-query results distinct from secondary batch-query results.
- Do not fabricate or extrapolate commands, logs, tests, measurements, profiler output, hardware facts, revision identifiers, or runtime behavior.
- If required GPU or CUDA support is unavailable, mark GPU-dependent verification or benchmarking `blocked`; CPU-only evidence is not equivalent.
- A performance hypothesis may be `supported`, `rejected`, or `inconclusive`. A rejected hypothesis is a valid experiment outcome when supported by complete evidence.

## Missing-contract and Blocked-state Behavior

When a required contract, decision, approval, tool, environment, compatible GPU, revision identity, or external dependency is missing—or authoritative sources materially conflict—set the affected work or gate to `blocked`. Do not select a convenient default or weaken the acceptance criterion.

A blocked report must state:

- the exact missing or conflicting condition;
- the sources and evidence inspected;
- the affected requirement, acceptance gate, or workflow transition;
- work that remains valid;
- the smallest action needed to unblock it; and
- the state or gate from which work should resume.

Resume only after re-checking and confirming that the blocking condition has been resolved. A blocked state is neither failure nor success.
