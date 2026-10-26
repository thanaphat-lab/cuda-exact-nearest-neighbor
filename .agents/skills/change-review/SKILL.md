---
name: change-review
description: Review project changes against approved requirements, design, safety boundaries, and verification evidence. Use for diff, patch, commit, or milestone review; keep review-only requests read-only and prioritize actionable findings.
---

# Change Review

Review the actual change and its evidence, not the author's stated intent.

## Inputs

- Diff, patch, commit range, or explicit artifact set.
- Applicable canonical requirements and approved design.
- Verification and experiment evidence when available.

## Workflow

1. Establish the exact review scope and inspect surrounding code or documentation needed to judge it.
2. Check correctness, contract compliance, exactness, Float32/Float64 roles, V0-V4 scope, error handling, reproducibility, and regression risk as applicable.
3. Verify that claimed tests and measurements are backed by evidence and tied to the reviewed revision.
4. Rank findings by impact and cite precise locations. Distinguish defects, missing evidence, risks, and optional suggestions.
5. Treat missing required GPU verification as a blocked gate, not a review pass.

## Mutations

Review-only work is read-only. Do not fix findings, modify specifications, commit, push, or create a pull request unless the user separately authorizes that mutation.

## Output

Lead with findings. For each finding, provide severity, location, violated contract or risk, evidence, and a concrete remediation direction. Then report coverage, verification gaps, residual risks, and overall readiness.
