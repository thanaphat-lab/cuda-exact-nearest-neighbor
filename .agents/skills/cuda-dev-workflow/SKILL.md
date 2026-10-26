---
name: cuda-dev-workflow
description: Coordinate approval-gated, multi-stage work on the CUDA exact-nearest-neighbor project across requirements, design, implementation, verification, review, documentation, and experiments.
---

# CUDA Development Workflow

Coordinate work; do not replace specialist judgment or the repository's canonical contracts.

## Start every workflow

1. Inspect the workspace, Git state, applicable `AGENTS.md` files, canonical project specifications, and relevant build or test configuration.
2. Resolve sources and invariants with [project-contracts.md](references/project-contracts.md). If a required contract is absent, contradictory, or undecided, enter `blocked`; do not invent it.
3. Classify the requested scope, current state, intended artifacts, and mutation authorization.
4. Route only the stages needed. A narrow specialist request may bypass this orchestrator unless it needs cross-stage coordination or a gate decision.

## State and transitions

Use [workflow-state.json](references/workflow-state.json) as the authoritative transition model. Record the current state, evidence satisfying each transition, routed specialist, and next gate. Do not skip a required transition because work appears straightforward.

Use the explicit requirements-only, design-only, documentation-only, review-only, or Git/GitHub-only path when that is the entire request. These paths terminate without entering CUDA implementation stages.

At any state, move to `blocked` when required evidence, contracts, tools, hardware, authorization, or an external dependency is unavailable. A blocked state is neither a failure nor a pass. Report the exact missing condition, evidence collected, still-valid work, smallest unblock action, and resume state. Resume only after re-checking the condition.

## Approval gates

- Read-only inspection and analysis need no additional approval when inside the requested scope.
- Repository-local edits require an explicit change request and satisfied prerequisite gates. Approval to analyze or review is not approval to edit.
- Implementation requires an approved design or an explicit user instruction that safely supplies the equivalent decision.
- Dependency installation, environment changes, destructive actions, local Git mutations, and repository infrastructure changes require specific authorization unless the user explicitly requested that exact action.
- External mutations—including pushes, pull requests, issues, releases, uploads, and messages—require explicit approval immediately before the action.
- Never interpret later-stage approval as permission to alter canonical project specifications.

## Specialist routing

Use [routing-matrix.md](references/routing-matrix.md) for ownership and overlap rules. Default order for a full implementation-and-experiment workflow:

`requirement-analysis -> solution-design -> cuda-kernel-engineering -> verification -> cuda-performance -> cuda-experiment-reporter -> change-review -> docs-knowledge`

After verification, benchmark only when performance is in scope and correctness is accepted. A supported, rejected, or inconclusive hypothesis proceeds to experiment reporting. When benchmarking is explicitly out of scope, proceed directly to review without making a performance claim. When required benchmark hardware is unavailable, enter `blocked` and resume from verified state after re-checking availability.

Invoke `$github-flow` only where local or remote repository workflow is requested.

Use [skill-inventory.md](references/skill-inventory.md) when auditing responsibilities, and [routing-scenarios.json](references/routing-scenarios.json) when validating deterministic routing and gate policies.

Use [runtime-routing-scenarios.json](references/runtime-routing-scenarios.json) only for isolated, read-only routing probes in fresh Codex sessions. Static scenario success does not establish live routing behavior.

Read [validation-report.md](references/validation-report.md) only when auditing this bootstrap or planning a revalidation; it records environment-specific limitations and must not be treated as current project state indefinitely.

## Artifact discipline

Read [artifact-requirements.md](references/artifact-requirements.md) before declaring a stage complete. Every status claim must point to observable evidence. Record commands exactly as executed and distinguish planned, executed, passed, failed, skipped, and blocked checks.

Never fabricate commands, logs, benchmark values, test outcomes, GPU availability, revision identifiers, or runtime routing behavior. Static validation establishes internal consistency only; it does not prove live Codex routing.

## Completion

Stop at the requested milestone. Mark `complete` only when all in-scope artifacts and gates are satisfied. Report completed, failed, and blocked stages separately, with the next safe action.
