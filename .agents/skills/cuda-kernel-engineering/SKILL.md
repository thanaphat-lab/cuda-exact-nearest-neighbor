---
name: cuda-kernel-engineering
description: Implement or revise authorized CUDA kernel work for exact nearest-neighbor search. Use only when resolved contracts, an approved design, and local mutation authorization define the V0-V3 core or optional V4 scope.
---

# CUDA Kernel Engineering

Implement only an approved, bounded kernel design. Correctness precedes performance.

## Required inputs

- Resolved canonical numerical and optimization contracts.
- Approved design with explicit V0-V3 scope and any authorized V4 stretch scope.
- Local mutation authorization covering the target files.
- Verification criteria and repository-supported build commands.

Read [project-contracts.md](../cuda-dev-workflow/references/project-contracts.md). If any required contract, design approval, or mutation authorization is missing, return `blocked` without editing code.

## Workflow

1. Inspect current implementation, interfaces, memory ownership, error handling, and tests.
2. Map each code change to the approved design and keep unrelated refactors out of scope.
3. Preserve primary single-query exact behavior; treat batch-query evaluation as secondary unless explicitly requested.
4. Use Float32 for approved CPU/GPU computation and the approved Float64 path only as the numerical reference.
5. Keep V0-V3 core variants and optional V4 boundaries traceable; do not redefine what a variant means.
6. Add or update verification coverage required by the approved design, then route execution to `$verification`.

## Mutations

Edits are allowed only within the explicitly authorized implementation scope. Dependency, build-system, specification, Git, infrastructure, and external mutations require separate authorization. Do not optimize past the approved design based on speculative performance.

## Output

Produce a change record listing files, design mapping, behavior preserved, deviations, tests added or updated, unimplemented scope, and the exact verification still required. Never claim successful compilation, tests, or GPU execution unless those checks were run.
