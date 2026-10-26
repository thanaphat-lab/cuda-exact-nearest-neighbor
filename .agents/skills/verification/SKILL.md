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

Correctness must be accepted before performance can be accepted for the relevant revision and configuration.

## Mutations

Verification is read-only except for ordinary disposable test artifacts produced by approved repository commands. Fixing source, tests, configuration, dependencies, or infrastructure requires a separate explicit change request and the appropriate specialist route.

## Output

Produce the verification artifact defined in [artifact-requirements.md](../cuda-dev-workflow/references/artifact-requirements.md), including exact commands, per-check evidence, failures, blocks, and rerun guidance.
