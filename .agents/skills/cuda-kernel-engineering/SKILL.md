---
name: cuda-kernel-engineering
description: Implement or revise scoped CUDA kernel work for exact nearest-neighbor search within approved contracts and design, including in-scope build, test, and debug iterations.
---

# CUDA Kernel Engineering

Implement a bounded kernel change in the continuous Codex development loop. Correctness precedes performance.

## Required inputs

- Resolved canonical numerical and optimization contracts.
- Approved design covering the requested V0-V3 scope and any requested V4 stretch scope.
- For V5, the optional/secondary Batch Optimization scope established by the November 12, 2026 V5-A audit and Owner approval; no fabricated design carrier or approval identifier is required.
- An Owner-requested scope in an Owner-prepared worktree.
- Verification criteria and repository-supported build commands.

Read [project-contracts.md](../cuda-dev-workflow/references/project-contracts.md). If a required contract or material design decision is missing, return `blocked` without inventing it. The scoped change request authorizes ordinary repository-local implementation and validation; do not require a second authorization record.

For V5, missing prescriptions for concrete internal APIs, kernel structure, launch organization, memory ownership, or test organization are not missing material design decisions by themselves. Select and document compatible routine engineering details without repeating the V5-A audit or implying that those details were historically approved. Material scope, numerical, experimental-policy, acceptance, timing, confidence-interval, telemetry, or evidence-interpretation changes still require Owner review.

## Workflow

1. Inspect current implementation, interfaces, memory ownership, error handling, and tests.
2. Map each code change to the approved design and keep unrelated refactors out of scope.
3. Preserve primary single-query exact behavior; treat batch-query evaluation as secondary unless explicitly requested.
4. Use Float32 for approved CPU/GPU computation and the approved Float64 path only as the numerical reference.
5. Keep V0-V3 core variants and optional V4 boundaries traceable; do not redefine what a variant means.
6. Keep optional/secondary V5 Batch Optimization separate from V0-V4 and preserve primary single-query behavior and applicable CPU baselines.
7. Add or update verification coverage, run the checks permitted for the task, and fix in-scope failures. Use `$verification` as guidance without handing the task off or requesting renewed approval.

## Mutations

Edits are allowed only within the requested implementation scope. Build-system edits directly required by that scope are allowed; dependency installation, environment or repository-infrastructure changes, canonical specification changes, destructive actions, and scope expansion require a new Owner decision. All Git/GitHub mutations are Owner-only. Do not optimize past the approved design based on speculative performance.

For V5, Codex may implement and debug kernels, APIs, and non-performance correctness and integrity tests, and may execute permitted non-GPU checks. Codex must not execute GPU diagnostics, GPU tests, preflight, or formal benchmarks; the Owner must authorize and execute those separately. Do not weaken numerical behavior, correctness, CPU baselines, timing boundaries, confidence intervals, telemetry policy, or evidence acceptance to accommodate that actor boundary.

## Output

Produce a change record listing files, design mapping, behavior preserved, deviations, tests added or updated, unimplemented scope, and the exact verification still required. Never claim successful compilation, tests, or GPU execution unless those checks were run.
