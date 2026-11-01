---
name: cuda-kernel-engineering
description: Implement or revise scoped CUDA kernel work for exact nearest-neighbor search within approved contracts and design, including in-scope build, test, and debug iterations.
---

# CUDA Kernel Engineering

Implement a bounded kernel change in the continuous Codex development loop. Correctness precedes performance.

## Required inputs

- Resolved canonical numerical and optimization contracts.
- Approved design covering the requested V0-V3 scope and any requested V4 stretch scope.
- An Owner-requested scope in an Owner-prepared worktree.
- Verification criteria and repository-supported build commands.

Read [project-contracts.md](../cuda-dev-workflow/references/project-contracts.md). If a required contract or material design decision is missing, return `blocked` without inventing it. The scoped change request authorizes ordinary repository-local implementation and validation; do not require a second authorization record.

## Workflow

1. Inspect current implementation, interfaces, memory ownership, error handling, and tests.
2. Map each code change to the approved design and keep unrelated refactors out of scope.
3. Preserve primary single-query exact behavior; treat batch-query evaluation as secondary unless explicitly requested.
4. Use Float32 for approved CPU/GPU computation and the approved Float64 path only as the numerical reference.
5. Keep V0-V3 core variants and optional V4 boundaries traceable; do not redefine what a variant means.
6. Add or update verification coverage, run the authoritative checks, and fix in-scope failures. Use `$verification` as guidance without handing the task off or requesting renewed approval.

## Mutations

Edits are allowed only within the requested implementation scope. Build-system edits directly required by that scope are allowed; dependency installation, environment or repository-infrastructure changes, canonical specification changes, destructive actions, and scope expansion require a new Owner decision. All Git/GitHub mutations are Owner-only. Do not optimize past the approved design based on speculative performance.

## Output

Produce a change record listing files, design mapping, behavior preserved, deviations, tests added or updated, unimplemented scope, and the exact verification still required. Never claim successful compilation, tests, or GPU execution unless those checks were run.
