---
name: requirement-analysis
description: Analyze and reconcile requirements for the CUDA exact-nearest-neighbor project. Use when acceptance criteria, source-of-truth conflicts, scope boundaries, or unresolved numerical and optimization decisions must be identified before design or implementation.
---

# Requirement Analysis

Produce an evidence-backed requirements analysis without silently deciding unresolved policy.

## Inputs

- The user's requested outcome and scope.
- Current repository instructions and canonical project specifications.
- Relevant accepted decisions, issues, and existing implementation evidence.

Resolve authority with [project-contracts.md](../cuda-dev-workflow/references/project-contracts.md). Search the current repository; do not rely on remembered paths or cached conclusions.

## Workflow

1. Identify applicable sources and cite file paths or supplied artifacts.
2. Separate confirmed requirements, derived implications, implementation choices, and unresolved decisions.
3. Preserve the primary single-query workload, secondary batch evaluation, Float32 computation, Float64 reference, V0-V3 core scope, optional V4 scope, correctness-first gate, and revision-bound reproducibility.
4. Map each in-scope requirement to an observable acceptance criterion only when the canonical contract supplies one.
5. Report contradictions and missing decisions. If they affect the requested downstream stage, return `blocked` with the smallest decision needed.

Do not invent numerical tolerances, distance semantics, tie rules, dataset constraints, optimization definitions, or performance thresholds.

## Mutations

Read-only by default. Do not edit requirements or specifications during analysis-only work. When the user explicitly requests documentation changes, preserve approved meaning and route specification changes through the required approval boundary; analysis does not authorize implementation.

## Output

Return a requirements analysis containing sources, confirmed invariants, acceptance criteria, conflicts, unresolved decisions, assumptions explicitly permitted by evidence, status, and the recommended next route. Follow the requirements artifact contract in [artifact-requirements.md](../cuda-dev-workflow/references/artifact-requirements.md).
