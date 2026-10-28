# Benchmark Protocol

Status: Derived protocol; not executed\
Requirements: [FinalProjectSpecification-v1.0.md](../specifications/FinalProjectSpecification-v1.0.md) and [S6 Numerical Policy Addendum v1.0](../specifications/FinalProjectSpecification-S6-Numerical-Addendum-v1.0.md)\
Acceptance gates: AC-012–AC-015 in [AcceptanceCriteria.md](../requirements/AcceptanceCriteria.md)

## 1. Gate and scope

Performance acceptance requires acceptance of the complete applicable correctness matrix for the same revision and working-tree state, build configuration, workload, arithmetic environment, and implementation variant. Both `accepted-unambiguous` and qualified `accepted-ambiguous` cases may contribute only when issued under the approved UD-003 policy with complete mandatory evidence and no required failed or blocked gate. One accepted case is insufficient. No benchmark result is accepted merely because it was measured. If correctness is failed or blocked, any separately authorized exploratory timing must be labeled non-acceptance evidence.

The primary benchmark is single query. Batch query is secondary and reported separately. V0–V3 are core; V4 is optional and separately labeled.

No absolute runtime or speedup threshold is approved. Every performance experiment must state its hypothesis and interpretation before execution.

## 2. Required configurations

For each in-scope core variant, evaluate the required matrix:

- `N ∈ {1024, 16384, 262144, 1048576}`
- `D ∈ {8, 32, 128}`
- a recorded synthetic-data seed

Comparisons use identical logical datasets, query inputs, correctness policy, build mode, and environmental conditions to the extent possible. Any mismatch is a reported comparison limitation.

The Float32 CPU lane may be reported as a comparator when the future experiment question calls for it. The Float64 reference is a correctness/diagnostic reference, not a directly interchangeable production-performance variant.

## 3. Pre-run capture

Capture actual values before measurement:

| Category | Required fields |
|---|---|
| Identity | Revision identifier, working-tree state, experiment ID, date/time/timezone |
| Hardware | GPU vendor/model, GPU identifier if relevant, memory capacity, CPU, host memory |
| Software | Operating system/environment, GPU driver, CUDA Toolkit, compiler and versions |
| Build | Build type/flags, architecture target, fast-math state (`disabled` for core), relevant libraries |
| Workload | Single/batch label, variant, `N`, `D`, batch size if applicable, seed, generator/configuration |
| Runtime configuration | Launch/configuration values actually used, power/clock settings if known, concurrency/stream policy if applicable |
| Method | Exact commands, timer mechanism, timing boundaries, allocation/reuse policy, percentile calculation method |
| Correctness prerequisite | Link to the complete accepted correctness matrix, including per-case NN verdict, contender provenance, and matching revision/tree/build/workload/arithmetic-environment/variant identifiers |

Unknown environment facts are recorded as `unknown`; required missing facts can make AC-014 blocked. Hardware-specific values are observations/configuration, not canonical optimization requirements.

## 4. Timing classes

### 4.1 Kernel-only

Semantic boundary:

- inputs and output storage are already available in device memory before the measured interval;
- start immediately before the evaluated kernel sequence;
- stop after that sequence completes, including the synchronization needed to establish completion for timing;
- exclude host preprocessing, allocation outside the interval, host-to-device transfer, device-to-host transfer, independent validators, exact-dyadic oracles, diagnostic buffers/reporting, logging, and dataset generation.

If one logical variant requires multiple kernels, the declared kernel-only interval includes every kernel required to produce its result. Any excluded setup kernel or device preprocessing step must be disclosed; otherwise comparisons are not treated as equivalent.

### 4.2 End-to-end

Semantic boundary:

- start immediately before per-invocation preprocessing of an already-generated host-side input;
- include preprocessing performed for that invocation, required host-to-device transfer, the full evaluated compute sequence, required device-to-host transfer, and synchronization;
- stop when the result needed by the caller is available in host memory;
- exclude dataset generation, program startup, compiler activity, independent validation, exact-dyadic oracle work, diagnostic/report generation, and correctness analysis;
- include allocation or other setup when it occurs per invocation; exclude genuinely one-time setup only when reuse is declared and that exclusion is applied consistently.

The report must itemize transfer, preprocessing, allocation, and reusable-state treatment. For batch measurements, state what is reused across queries and what is charged to the measured batch.

## 5. Iteration procedure

For each unique configuration and timing class:

1. Confirm the complete matching correctness matrix is accepted under AC-012 and record all matching revision, tree, build, workload, arithmetic-environment, and variant identifiers.
2. Capture the pre-run environment/configuration manifest.
3. Prepare the seeded workload and verify Float32 finite-distance constraints.
4. Execute exactly 10 warm-up iterations; do not include them in statistics.
5. Execute exactly 100 measured repetitions.
6. Retain all 100 raw or traceable samples with units and configuration identity.
7. Report median, p5, and p95 from those samples.
8. Record the percentile calculation method; use the same method for compared configurations.
9. Repeat for the second timing class without combining its distribution with the first.
10. Classify the stated hypothesis as `supported`, `rejected`, or `inconclusive` under its predeclared interpretation.

Run ordering or randomization may be selected by a future experiment design, but it must be recorded and applied consistently. No unapproved hardware-specific setting is prescribed here.

## 6. Reporting layout

Use separate tables for:

- single-query kernel-only;
- single-query end-to-end;
- batch-query kernel-only, if evaluated;
- batch-query end-to-end, if evaluated;
- optional V4, if evaluated.

Minimum row schema:

| Field | Value |
|---|---|
| Experiment/configuration ID | `<required>` |
| Revision / tree state | `<required>` |
| Workload / variant | `<required>` |
| `N`, `D`, seed | `<required>` |
| Batch size | `<required for batch; otherwise n/a>` |
| Timing class and boundary ID | `<required>` |
| Warm-ups / repetitions | `10 / 100` |
| Median / p5 / p95 | `<measured values with units>` |
| Raw evidence | `<path or durable reference>` |
| Correctness evidence | `<matching accepted report>` |
| Environment manifest | `<reference>` |
| Limitations | `<required, or none observed>` |

## 7. Comparison rules

- Compare kernel-only only with kernel-only and end-to-end only with end-to-end.
- Do not merge single-query and batch-query conclusions.
- Keep V4 out of core-completion claims.
- Identify changes in device, driver, toolkit, compiler, build flags, timing mechanism, seed/workload, or runtime settings.
- Do not claim causality from a version label alone; attribute conclusions only to the controlled experiment.
- A rejected hypothesis is a valid evidence-backed outcome. Missing or incompatible evidence is `inconclusive` or `blocked`, as applicable, not a favorable result.

## 8. Failure and block handling

- Incorrect results: performance acceptance is blocked until correctness is restored and reaccepted.
- Missing revision identity: reproducibility/acceptance is blocked.
- Missing required GPU or incompatible CUDA environment: GPU benchmark is blocked.
- Missing raw samples, timing boundary, or required environment metadata: affected performance gate is blocked.
- Measurement anomaly: retain the sample and document the approved exclusion rule if any; do not silently discard it.
- Unresolved hardware-specific tuning: record the chosen experimental configuration only after its later design approval; do not promote it to a universal requirement.

Future reports use [ExperimentTemplate.md](../experiments/ExperimentTemplate.md).
