---
name: cuda-performance
description: Plan, profile, benchmark, or assess performance for the CUDA exact-nearest-neighbor project. Use only after correctness evidence exists for the relevant revision and when reproducible single-query or secondary batch measurements are requested.
---

# CUDA Performance

Produce reproducible performance evidence; do not substitute expectations for measurements.

## Inputs

- Correctness acceptance for the relevant revision and configuration.
- Approved optimization scope and workload definitions.
- Explicit performance hypothesis and acceptance interpretation when benchmarking is in scope.
- Repository-supported benchmark or profiling commands.
- Available compatible GPU and recorded hardware/software environment.

If correctness is not accepted, required benchmark hardware is unavailable, or benchmark contracts are unresolved, return `blocked` for performance acceptance. If benchmarking is explicitly out of scope, record that boundary and return to the workflow without claiming performance passed.

## Workflow

1. Record revision, working-tree state, hardware, driver/toolkit, build configuration, dataset/workload, warmup, repetitions, timing boundaries, and statistics required by approved contracts.
2. Measure the primary single-query workload separately from secondary batch-query evaluation.
3. Compare only compatible configurations and identify confounders.
4. Keep V0-V3 core results distinct and label V4 as optional stretch scope.
5. Preserve raw or traceable output and classify the hypothesis as `supported`, `rejected`, or `inconclusive` under the approved interpretation.
6. Treat a rejected hypothesis as valid experiment evidence, not as a workflow failure; report failures and missing data separately.
7. Recommend optimization work only from evidence and route authorized kernel changes to `$cuda-kernel-engineering`.

## Mutations

Read-only for assessment and reporting. Running approved benchmark/profiler commands may create disposable artifacts. Source, benchmark harness, configuration, dependency, or infrastructure changes require explicit authorization and the appropriate prior design gate.

## Output

Return methodology, environment, revision, exact commands, traceable measurements, hypothesis outcome, comparisons, limitations, correctness prerequisite, and status. No measurement means no performance claim.
