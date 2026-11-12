---
name: verification
description: Verify correctness, buildability, and test evidence for the CUDA exact-nearest-neighbor project. Use when changes or milestones need executable checks, especially Float32 CPU/GPU results against the Float64 reference and GPU-required gate decisions.
---

# Verification

Establish evidence-backed status; do not infer a pass from intent, compilation alone, or unavailable hardware.

## Inputs

- Approved requirements and design relevant to the check.
- Current revision and changed scope.
- Checked-in build and test configuration.
- Available CPU, CUDA toolchain, and GPU environment.

Discover commands from repository configuration or authoritative instructions. Never fabricate a command because a conventional one seems likely.

## Workflow

1. Record revision, environment, toolchain, GPU identity/availability, and test inputs.
2. Run the smallest authoritative checks that cover the changed scope, then required broader gates.
3. Evaluate Float32 CPU and GPU computation against the approved Float64 numerical reference criteria.
4. Keep primary single-query results separate from secondary batch-query evaluation.
5. Classify every check as planned, passed, failed, skipped, or blocked and retain exact command/result evidence.
6. If a required GPU or compatible CUDA environment is unavailable, mark GPU-dependent verification `blocked`; never mark it passed or treat a CPU-only substitute as equivalent.

For the Owner-approved optional/secondary V5 Batch Optimization experiment, Codex may add and debug non-performance correctness and integrity tests and execute permitted non-GPU checks. Codex must not execute GPU diagnostics, GPU tests, preflight, or formal benchmarks; record those as requiring separate Owner authorization and execution, never as passed. The November 12, 2026 V5-A audit and Owner approval need not be repeated merely because concrete API or kernel details were left to routine engineering judgment. Do not invent a V5 design carrier, approval ID, signed record, or historical approval of those details.

Correctness must be accepted before performance can be accepted for the relevant revision and configuration.

The V5 actor restriction does not alter the approved correctness or numerical criteria, applicable CPU baselines, timing boundaries, confidence intervals, telemetry policy, or evidence-acceptance rules. A material change to any of those requires Owner review.

## Development-loop behavior

For a scoped change task, verification and debugging are part of one Codex loop. If a build or test fails, Codex may fix source, tests, or directly related configuration and rerun checks without a new approval when the fix remains within the requested scope and approved contracts. Dependency installation, environment or repository-infrastructure changes, destructive actions, scope expansion, and canonical contract changes still require an Owner decision. All Git/GitHub mutations remain Owner-only.

For a verification-only request, keep repository sources read-only except for disposable artifacts produced by authoritative commands.

## Output

Produce the verification artifact defined in [artifact-requirements.md](../cuda-dev-workflow/references/artifact-requirements.md), including exact commands, per-check evidence, failures, blocks, and rerun guidance.
