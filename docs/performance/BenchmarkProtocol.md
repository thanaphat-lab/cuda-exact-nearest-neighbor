# Benchmark Protocol

Status: Derived protocol; not executed\
Requirements: [FinalProjectSpecification-v1.0.md](../specifications/FinalProjectSpecification-v1.0.md) and [S6 Numerical Policy Addendum v1.0](../specifications/FinalProjectSpecification-S6-Numerical-Addendum-v1.0.md)\
Acceptance gates: AC-012–AC-015 in [AcceptanceCriteria.md](../requirements/AcceptanceCriteria.md)

## 1. Gate and scope

Performance acceptance requires acceptance of the complete applicable correctness matrix for the same revision and working-tree state, build configuration, workload, arithmetic environment, and implementation variant. Both `accepted-unambiguous` and qualified `accepted-ambiguous` cases may contribute only when issued under the approved UD-003 policy with complete mandatory evidence and no required failed or blocked gate. One accepted case is insufficient. No benchmark result is accepted merely because it was measured. If correctness is failed or blocked, any separately authorized exploratory timing must be labeled non-acceptance evidence.

The primary benchmark is single query. Batch query is secondary and reported separately. V0–V3 are core; V4 is optional and separately labeled.

No absolute runtime or speedup threshold is approved. Every performance experiment must state its hypothesis and interpretation before execution.

The primary end-to-end boundary is `BND-E2E-RAW`. Prepared-dataset reuse is
the supplementary `BND-E2E-PREP` track. `BND-KERNEL` measures the complete
production kernel sequence. Supplementary reuse cannot replace the primary
raw-input single-query evidence.

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
| Identity | Revision identifier, working-tree state, approved design revision, experiment ID, sample role (`tuning` or `final`), date/time/timezone |
| Hardware | GPU vendor/model, GPU identifier if relevant, memory capacity, CPU, host memory |
| Software | Operating system/environment, GPU driver, CUDA Toolkit, compiler and versions |
| Build | Build type/flags, architecture target, fast-math state (`disabled` for core), relevant libraries |
| Workload | Single/batch label, variant, `N`, `D`, batch size if applicable, seed, generator/configuration |
| Runtime configuration | Launch/configuration values actually used, power/clock settings if known, concurrency/stream policy if applicable |
| Method | Exact commands, timer mechanism, timing boundaries, allocation/reuse policy, percentile calculation method |
| Correctness prerequisite | Link to the complete accepted correctness matrix, including per-case NN verdict, contender provenance, and matching revision/tree/build/workload/arithmetic-environment/variant identifiers |

Unknown environment facts are recorded as `unknown`; required missing facts can make AC-014 blocked. Hardware-specific values are observations/configuration, not canonical optimization requirements.

## 4. Timing classes

### 4.1 BND-KERNEL — complete production kernel sequence

Dataset, query, result, status and workspace storage are device resident before
timing. Record a device start event immediately before the first required
production kernel and an end event immediately after the last required
production kernel on the same stream. Include V0/V1 serial reduction, all V2/V3
hierarchical stages and production failure-status propagation. Synchronize the
end event to establish completion.

Exclude allocation, layout conversion, host preprocessing, H2D/D2H transfer,
independent validation, Float64 reference, exact-dyadic oracle, diagnostic-only
kernels, logging and report generation.

### 4.2 BND-E2E-RAW — primary raw-input one-shot

Start immediately before the one-shot API performs validation, preprocessing,
allocation or transfer on an already-generated host AoS dataset and query.
Include structural and contract validation, per-call allocation, V1–V3 host
AoS-to-SoA conversion, dataset/query H2D transfer, the full production kernel
sequence, result/status D2H transfer, synchronization and call-owned transient
cleanup.

Stop when the synchronous call returns with host result/status available and
transient resources released.

Exclude dataset generation, startup, compilation, validators, Float64
reference, exact-dyadic oracle, diagnostic-only capture, correctness
classification, logging and report generation.

### 4.3 BND-E2E-PREP — supplementary prepared reuse

Dataset preparation and compatible workspace allocation complete before
timing. Start immediately before per-query validation. Include query
validation, status reset, query H2D transfer, the complete production kernel
sequence, result/status D2H transfer, synchronization and per-search cleanup.
Stop when the synchronous prepared search returns.

Exclude dataset preparation and persistent workspace allocation. Report those
costs separately if measured; do not silently amortize them. This
supplementary track cannot replace `BND-E2E-RAW`.

## 5. Independent block-size tuning

1. Predeclare candidates `{128,256,512}`.
2. Use a tuning seed distinct from the final-reporting seed.
3. Require applicable correctness qualification before a candidate is eligible.
4. Collect independent tuning samples; reported tuning configurations retain
   10 warm-ups, 100 measured repetitions, median, p5 and p95.
5. Select by the predeclared `BND-KERNEL` median.
6. An exact measured median tie selects the smaller block size.
7. Freeze the selection in a revision/configuration manifest.
8. Rerun the complete applicable correctness matrix for the selected
   revision/configuration.
9. Collect final samples independently.
10. Never retune from final measurements; a later attempt is a new experiment.

Actual winners are observed target-specific values, not canonical constants.

## 6. Iteration procedure

For each unique configuration and timing class:

1. Confirm the complete matching correctness matrix is accepted under AC-012 and record all matching revision, tree, build, workload, arithmetic-environment, and variant identifiers.
2. Capture the pre-run environment/configuration manifest.
3. Prepare the seeded workload and verify Float32 finite-distance constraints.
4. Execute exactly 10 warm-up iterations; do not include them in statistics.
5. Execute exactly 100 measured repetitions.
6. Retain all 100 raw or traceable samples with units and configuration identity.
7. Report median, p5, and p95 from those samples.
8. Record the percentile calculation method; use the same method for compared configurations.
9. Repeat for each required timing boundary without combining its distribution with another boundary.
10. Classify the stated hypothesis as `supported`, `rejected`, or `inconclusive` under its predeclared interpretation.

Run ordering or randomization may be selected by a future experiment design, but it must be recorded and applied consistently. No unapproved hardware-specific setting is prescribed here.

Record `sample_role=tuning|final`, tuning/final seed identities, block candidate
set, selected block size, freeze manifest and timing-boundary ID. Tuning samples
must never appear in final-reporting distributions.

## 7. Reporting layout

Use separate tables for:

- single-query `BND-KERNEL`;
- primary single-query `BND-E2E-RAW`;
- supplementary single-query `BND-E2E-PREP`, if reported;
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

## 8. Comparison rules

- Compare kernel-only only with kernel-only and end-to-end only with end-to-end.
- Do not merge single-query and batch-query conclusions.
- Keep V4 out of core-completion claims.
- Identify changes in device, driver, toolkit, compiler, build flags, timing mechanism, seed/workload, or runtime settings.
- Do not claim causality from a version label alone; attribute conclusions only to the controlled experiment.
- A rejected hypothesis is a valid evidence-backed outcome. Missing or incompatible evidence is `inconclusive` or `blocked`, as applicable, not a favorable result.

## 9. Failure and block handling

- Incorrect results: performance acceptance is blocked until correctness is restored and reaccepted.
- Missing revision identity: reproducibility/acceptance is blocked.
- Missing required GPU or incompatible CUDA environment: GPU benchmark is blocked.
- Missing raw samples, timing boundary, or required environment metadata: affected performance gate is blocked.
- Measurement anomaly: retain the sample and document the approved exclusion rule if any; do not silently discard it.
- Unresolved hardware-specific tuning: record the chosen experimental configuration only after its later design approval; do not promote it to a universal requirement.

Future reports use [ExperimentTemplate.md](../experiments/ExperimentTemplate.md).
