---
name: cuda-experiment-reporter
description: Assemble revision-bound CUDA experiment evidence into a reproducible report. Use when executed correctness, benchmark, or profiling results must be consolidated without generating or inventing missing measurements.
---

# CUDA Experiment Reporter

Synthesize existing evidence into an auditable report. This skill reports experiments; it does not manufacture them.

## Inputs

- Revision identifier and working-tree state.
- Approved experiment question and workload contracts.
- Exact executed commands, environment metadata, raw or traceable outputs, and per-check status.
- Correctness evidence for any performance comparison.

## Workflow

1. Verify that results can be tied to the stated revision, configuration, dataset, hardware, and commands.
2. Separate single-query primary results from batch-query secondary results.
3. Separate Float32 CPU/GPU results from the Float64 numerical reference evidence.
4. Keep V0-V3 core results distinct and label V4 as optional.
5. Record the performance hypothesis outcome as `supported`, `rejected`, or `inconclusive`; a rejected hypothesis remains a valid reported experiment when evidence is complete.
6. Report passed, failed, skipped, and blocked checks without smoothing or extrapolating missing data.
7. State comparison limits and contradictions. Route missing execution to `$verification` or `$cuda-performance`; do not fill gaps with estimates.

## Mutations

Read-only by default. Writing or updating a report file requires an explicit documentation change request. Rerunning tests, benchmarks, or profilers and changing code, specs, Git, or external systems require separate authorization and routing.

## Output

Produce a concise, reproducible experiment report with question, revision, environment, methodology, commands, results, correctness status, comparisons, limitations, failures, blocked checks, and next actions. Follow [artifact-requirements.md](../cuda-dev-workflow/references/artifact-requirements.md).
