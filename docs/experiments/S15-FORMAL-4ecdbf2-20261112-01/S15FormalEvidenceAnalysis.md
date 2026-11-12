# S15 Formal Evidence Analysis and Root-Cause Investigation

Status: **ANALYSIS COMPLETE — official experiment disposition remains `INCOMPLETE / environment inconclusive`**  
Analysis date: 2026-11-12 (Asia/Bangkok)  
Formal experiment: `S15-FORMAL-4ecdbf2-20261112-01`  
Evidence packet: `D:\temp\s15-formal-4ecdbf2-20261112`  
Source revision: `4ecdbf20b53c57239b8e9b0904f7b2c33d289881`  
Branch observed read-only: `feature/s15-public-sift1m`  

## 1. Executive finding

The formal benchmark completed its approved matrix and its matching correctness
gate passed, but the evidence cannot support or reject H1, H2, or H3. The
official acceptance artifact classifies it as `environment inconclusive` because
one telemetry metric-query wrapper duration exceeded the fixed 900 ms policy.
This report preserves that classification and treats all retained performance
ratios only as descriptive observations from an inconclusive environment.

The telemetry event is isolated to attempt sequence 3594. The metric subprocess
completed with exit code 0 and returned a valid row, but its end-to-end wrapper
duration was 2,314.272 ms. The following process query also completed in
198.120 ms. The collector neither crashed nor recorded a subprocess timeout,
and it caught up without violating the independent 3,000 ms maximum start-gap
limit. The existing evidence proves where the delay was observed but cannot
separate process launch, parent scheduling, `WaitForExit`, output reading, or
NVIDIA/driver service time. Therefore no unique low-level root cause is verified.

The retained performance shape is nevertheless explainable from source. Raw V2
performs full-dataset validation, AoS-to-SoA conversion, transient host/device
allocation, dataset and query transfer, kernel execution, result transfer,
synchronization, and transient cleanup for every call. Prepared V2 moves
full-dataset validation, conversion, dataset transfer, and persistent workspace
allocation into construction, leaving only per-query validation, a 512-byte
query transfer, the kernel sequence, one-record result transfer, and
synchronization in the recurring boundary. S15 directly measures the composite
Raw, Prepared, construction, and kernel boundaries; it does not retain separate
Raw measurements for validation, conversion, allocation, transfer,
synchronization, or cleanup.

The smallest performance experiment justified by this evidence is therefore a
non-formal, exact-revision stage diagnostic that retains the already available
V2 staged timing fields for one approved query at all four approved `N` values.
It should precede any CUDA optimization. Independently, the telemetry wrapper
should be instrumented and corrected to enforce the existing 900 ms limit over
the whole subprocess lifecycle, without changing the threshold, before any new
formal acceptance run.

## 2. Scope, authority, and method

### 2.1 In scope

- Read-only inspection of the formal packet, approved S15 design and approval
  record, implementation, runner, collector, and analyzer.
- Static calculations from retained summaries and comparisons.
- A new analysis document outside the formal evidence packet.
- Planning future verification and experiments for Owner approval.

### 2.2 Excluded

- GPU tests, builds, preflight, profiling, or benchmarks.
- Changes to source, policy, canonical contracts, or existing evidence.
- Restarting or rerunning S15.
- Any Git or GitHub mutation.
- Reclassification of the formal experiment or its hypotheses.

### 2.3 Governing sources

- `docs/design/S15PublicSift1mBenchmarkDesignDecision-v1.0.md`
- `docs/governance/S15PublicSift1mBenchmarkApprovalRecord.md`
- `docs/governance/S15DatasetProvenanceRecoveryRecord.md`
- `docs/specifications/FinalProjectSpecification-v1.0.md` and the approved S6
  numerical policy chain
- `docs/governance/SourceOfTruth.md` and `DecisionRegister.md`
- `docs/requirements/AcceptanceCriteria.md` and
  `RequirementsTraceability.md`
- Formal packet manifests, environment status, raw samples, summaries,
  comparisons, break-even results, correctness gate, and acceptance report
- `benchmarks/s15_public_sift1m_benchmark.cpp`, `src/cuda_v2.cu`,
  `src/cpu_baseline.cpp`, `scripts/capture-s14-telemetry.ps1`, and
  `scripts/analyze-s15-public-sift1m.mjs`

## 3. Evidence integrity and official disposition

### 3.1 Verified identities

Observed repository state matched the requested Owner-prepared state:

| Identity | Observed value |
|---|---|
| Branch | `feature/s15-public-sift1m` |
| HEAD | `4ecdbf20b53c57239b8e9b0904f7b2c33d289881` |
| Read-only `git status --short` | empty |
| Formal experiment ID | `S15-FORMAL-4ecdbf2-20261112-01` |
| Formal manifest tree state | `clean` |
| Configuration SHA-256 | `70cdde89dec8d73361ecd2fb9ab1ca5529cf78e0cba21439302fb1aa7339de90` |
| Source-manifest SHA-256 | `aa8a434979a2ee6a43e6a9d4954ebac787e15548f06bc6ff2e0bc5f744775895` |
| Benchmark binary SHA-256 | `98763775fde3203659a8f175cd282f3174ce33a49a3cc4b6b8ba71794f2550f5` |
| Base dataset SHA-256 | `21f66e2975057b5728ba56de1c825bac4f4d89d596609ae985741c6242631816` |
| Query dataset SHA-256 | `f7fc9be140accdfd64116c2fa2365ecdb69b8f084970c6b0532db5ff79ac8fdc` |
| Correctness ledger SHA-256 | `bd4e2172d88d873ee04688961a4f4c55fdac6f76647649f3fe1ddb0ca7f7a168` |
| GPU | NVIDIA GeForce RTX 3060 Laptop GPU, UUID `6119f54aff96d0a6df230b48a8bd991f` |
| Build | Release, SM 86, strict FP, fast math disabled |

A read-only recomputation checked all 39 entries in `packet-manifest.json`.
Every file matched its recorded byte count and SHA-256; mismatches: **0**.

The matching correctness gate is `passed-clean-candidate`: 128 required
workloads, 384 required production results, 384 accepted, 0 failed, and 0
blocked. This authorizes interpretation of performance evidence only if the
environment gate is also acceptable; it does not cure an inconclusive
environment.

### 3.2 Official status preserved

The following are observed formal facts, not new judgments:

- `environment-status.json` says
  `retain-samples-mark-hypotheses-inconclusive` with reason
  `telemetry-query-duration-exceeded-timeout-policy`.
- `acceptance.json` says `evidence_acceptance: environment inconclusive`.
- Each of H1, H2, and H3 has 128 required workload outcomes and an official
  universal outcome of `inconclusive`.
- All 384 per-workload hypothesis rows remain `inconclusive` (32 for each H/N
  group).
- Performance favorability is explicitly not an evidence gate.

Accordingly, this report does **not** call H1 or H3 supported and does **not**
call H2 rejected, even where retained point estimates or confidence intervals
would otherwise appear favorable or unfavorable.

## 4. Telemetry root-cause investigation

### 4.1 Exact policy-triggering attempt

| Field | Value |
|---|---|
| Attempt sequence | `3594` |
| Scheduled elapsed | `3,594,000.0 ms` |
| Attempt start | `2026-11-12T02:27:56.1259986+07:00` |
| Attempt finish | `2026-11-12T02:27:58.6410471+07:00` |
| Whole attempt duration | approximately `2,515.071 ms` |
| Metric status / exit | `completed` / `0` |
| Metric wrapper duration | **`2,314.272 ms`** |
| Process status / exit | `completed` / `0` |
| Process wrapper duration | `198.120 ms` |
| Attempt status | `completed` |

The source-defined metric query was:

```text
nvidia-smi.exe --query-gpu=timestamp,uuid,pstate,temperature.gpu,utilization.gpu,utilization.memory,clocks.current.sm,clocks.current.memory,power.draw,power.limit,pcie.link.gen.current,pcie.link.width.current,clocks_event_reasons.hw_thermal_slowdown --format=csv,noheader,nounits
```

The runner resolved the executable with `Get-Command nvidia-smi.exe` and passed
that resolved path to the collector. The packet does not retain the absolute
resolved `nvidia-smi.exe` path, so this report does not invent it.

### 4.2 Neighboring attempts and GPU state

| Seq. | Start time (+07:00) | Metric ms | Process ms | GPU utilization | SM clock MHz | Power W |
|---:|---|---:|---:|---:|---:|---:|
| 3591 | 02:27:53.1129136 | 188.8143 | 159.1713 | 0% | 2040 | 50.02 |
| 3592 | 02:27:54.1229084 | 109.6941 | 106.9662 | 0% | 2040 | 49.92 |
| 3593 | 02:27:55.1329344 | 120.6348 | 99.0643 | 88% | 2040 | 49.69 |
| **3594** | **02:27:56.1259986** | **2314.2720** | **198.1200** | **0%** | **1522** | **30.51** |
| 3595 | 02:27:58.6420710 | 164.6103 | 137.4482 | 0% | 1522 | 30.51 |
| 3596 | 02:27:58.9477626 | 137.5711 | 111.5838 | 0% | 1522 | 30.51 |
| 3597 | 02:27:59.2031994 | 145.5235 | 220.8991 | 0% | 1522 | 30.51 |

Important timing qualification: the GPU row for sequence 3594 carries NVIDIA's
timestamp `02:27:58.424`, near the end of the delayed query. Its 0% utilization
does not reveal GPU utilization during the preceding two seconds. Sequence 3593
did observe 88% utilization, and the benchmark PID was the recorded compute
process throughout this neighborhood. None of these rows reported hardware
thermal slowdown.

The collector immediately issued overdue attempts 3595 and 3596, then returned
to cadence. The resulting maximum metric/process start-time gap was 2,517 ms,
below the separate approved 3,000 ms continuity limit.

### 4.3 Distribution and lifecycle facts

Read-only diagnostic calculations over all 11,698 attempts found:

| Query | Median ms | p95 ms | p99 ms | >500 ms | >900 ms |
|---|---:|---:|---:|---:|---:|
| Metric | 72.6770 | 94.6776 | 118.5777 | 6 | **1** |
| Compute-process list | 75.3419 | 96.4900 | 118.7485 | 0 | 0 |

These percentiles are diagnostic calculations from the attempt ledger, not new
formal acceptance statistics.

Verified lifecycle evidence:

- Session: 11,698 attempted, 11,698 completed, 0 failed.
- Metric samples: 11,698; process samples: 11,698.
- Collector start: `01:28:02.1089763`; end: `04:42:59.9069686` (+07:00).
- Benchmark interval: `01:28:02.9846129` to `04:42:58.7715933` (+07:00).
- Benchmark and telemetry exited with code 0.
- Telemetry did not exit before benchmark end or stop signal and was not forced
  to terminate.
- Telemetry error, launch-error, collector stdout, benchmark stdout, and
  benchmark stderr logs are all zero bytes.
- The benchmark executable was the only named compute process in all 11,692
  non-empty process observations; sequence 3594 recorded benchmark PID 21436.

### 4.4 What is verified about the timeout implementation

`Invoke-BoundedProcess` starts its stopwatch before constructing and starting
the child, but applies `WaitForExit(900)` only after `Process.Start()` returns.
It reads redirected stdout/stderr only after the wait. Therefore:

1. the recorded duration is end-to-end wrapper time;
2. the 900 ms wait is **not** an end-to-end subprocess deadline; and
3. time in `Process.Start`, parent-process descheduling before the wait, and
   post-wait output reads is not bounded by the `WaitForExit` argument.

This explains how an attempt can be recorded `completed` with exit code 0 while
its policy-checked duration exceeds 900 ms. It does not establish which
unbounded phase consumed the extra time. The analyzer intentionally checks the
recorded end-to-end duration and therefore correctly applied the approved
environment-inconclusive action.

### 4.5 Root-cause classification

| Candidate cause | Classification | Evidence-based assessment |
|---|---|---|
| Wrapper does not enforce a whole-lifecycle 900 ms deadline | **Verified implementation fact** | `Process.Start` and output reads lie outside `WaitForExit(900)`; analyzer evaluates the larger end-to-end duration. |
| Isolated metric-query path delay | **Verified observation** | Only metric sequence 3594 exceeded 900 ms; the paired process query and neighboring attempts did not. |
| Collector crash, timeout kill, query failure, or malformed output | **Ruled out for this attempt** | Status `completed`, exit 0, valid metric row, zero error log, complete session. |
| Unauthorized competing compute process | **Not observed** | Only the benchmark process appears in retained compute-process rows. This cannot exclude non-CUDA system activity. |
| Parent/child subprocess scheduling delay on Windows | **Plausible, unverified** | The stopwatch includes launch and parent scheduling, but no phase timestamps or OS scheduling trace were retained. |
| NVIDIA driver/NVML service delay during `nvidia-smi` startup/query | **Plausible, unverified** | The delayed path was the GPU metric query and returned data late, but no NVML/driver trace exists. |
| GPU contention from the benchmark | **Plausible but weakly evidenced** | The benchmark was active and the preceding sample was 88% utilized; the delayed sample was taken near query completion and read 0%, so causal overlap is unknown. |
| Thermal throttling | **Not supported by retained row** | Neighboring rows say `Not Active`; cadence is too coarse to prove absence throughout the delay. |
| Monitoring overhead as the cause of the single outlier | **Unverified** | Two subprocesses per second impose measurable monitoring work, but the evidence does not show that monitoring caused its own isolated delay or materially changed benchmark latency. |

The defensible root-cause statement is therefore: **the acceptance trigger was
an isolated end-to-end metric-query delay that escaped the collector's
wait-only timeout boundary; the retained evidence cannot distinguish Windows
scheduling, subprocess launch, NVIDIA/driver query latency, or transient GPU
interaction as the underlying delay source.**

## 5. CPU Raw versus GPU Raw performance investigation

### 5.1 Source-defined pipeline boundaries

CPU Raw (`CPU-E2E-RAW`) includes:

1. structural and full dataset/query finiteness validation;
2. Float32 arithmetic-environment validation;
3. one exhaustive AoS CPU distance/argmin pass; and
4. synchronous host result return.

GPU Raw V2 (`BND-E2E-RAW`) includes:

1. structural and full dataset/query finiteness validation;
2. CUDA device/options validation;
3. host AoS-to-SoA allocation and tiled conversion;
4. transient device allocation for dataset, query, and reduction buffers;
5. dataset and query H2D transfers;
6. complete V2 distance and hierarchical reduction kernel sequence;
7. one-record D2H transfer and stream synchronization; and
8. call-owned transient host/device cleanup.

Prepared V2 construction validates and converts the dataset, allocates and
transfers the persistent device dataset, synchronizes, and constructs persistent
query/reduction workspace. Prepared search validates only the query against the
prepared metadata, transfers 128 Float32 values (512 bytes), runs the same
kernel sequence, returns one result record, and synchronizes while reusing the
persistent dataset and workspace.

### 5.2 Retained descriptive measurements by N

The table uses the median across the 32 retained per-query medians for each N.
It is a transparent descriptive calculation, not an approved cross-query
population statistic and not a hypothesis reclassification.

| N | CPU Raw ms | GPU Raw ms | Prepared ms | Kernel ms | CPU/GPU Raw ratio (H2 median) | GPU Raw relative to CPU |
|---:|---:|---:|---:|---:|---:|---:|
| 1,024 | 0.6040 | 1.1966 | 0.0638 | 0.0343 | 0.5469 | about 1.83x slower |
| 16,384 | 8.4206 | 29.3855 | 1.0535 | 0.6740 | 0.2854 | about 3.50x slower |
| 262,144 | 146.6108 | 311.7944 | 0.8670 | 0.5202 | 0.4770 | about 2.10x slower |
| 1,000,000 | 675.1274 | 814.6846 | 1.7143 | 1.5524 | 0.8242 | about 1.21x slower |

The H2 point-ratio ranges across queries were 0.3737–0.6409, 0.2779–0.3223,
0.4170–0.5073, and 0.8034–0.8432 respectively. They remain officially
inconclusive.

At N=262,144, one retained per-query median was visibly high in both Prepared
(4.4969 ms) and Kernel (4.1308 ms), while the respective median-across-query
values were 0.8670 and 0.5202 ms. No outlier was excluded, consistent with the
approved method.

### 5.3 Why GPU Raw is slower in the retained measurements

Observed facts:

- GPU Raw is slower than CPU Raw at every retained workload point estimate.
- The kernel sequence is only a small fraction of the GPU Raw boundary.
- Prepared construction scales similarly to GPU Raw and is close to Raw latency
  at the two largest sizes.
- Prepared recurring latency is much smaller than Raw latency once persistent
  setup is excluded.

Source-supported explanation:

- Both CPU and GPU Raw scan the full dataset for finiteness before search.
- CPU then performs the exhaustive search directly over the existing AoS data.
- GPU Raw additionally allocates and writes a full SoA host copy, allocates GPU
  resources, transfers the full dataset, and later synchronizes and destroys
  those transient resources.
- The GPU kernel is fast enough that those host/setup/transfer operations, not
  the kernel sequence, dominate Raw V2 in these measurements.

This is a pipeline explanation, not a measured attribution to any one substage.
S15 did not retain separate Raw validation, conversion, allocation, H2D, D2H,
synchronization, or cleanup distributions.

## 6. GPU Raw bottleneck breakdown

### 6.1 Measurement availability

| Operation | Present in formal boundary | Separately measured in S15 | Interpretation |
|---|---|---|---|
| Full dataset/query validation | Raw; dataset construction; CPU Raw | No | Source-verified inclusion; cost unmeasured separately. |
| AoS-to-SoA host allocation/conversion | Raw; dataset construction | No | Source-verified inclusion; cost unmeasured separately. |
| CUDA device and workspace allocations | Raw; construction | Workspace construction is composite | Workspace distribution also includes validation/device-property work; it is not pure allocation time. |
| Dataset/query H2D | Raw; dataset construction; query in Prepared | No | Source-verified inclusion; cost unmeasured separately. |
| Kernel sequence | Raw and Prepared | **Yes: `BND-KERNEL`** | CUDA events around the complete production kernel sequence. |
| Result D2H | Raw and Prepared | No | One reduction record in production path; included in E2E. |
| Stream synchronization | Raw, Prepared, dataset construction | No | Source-verified inclusion; cost unmeasured separately. |
| Transient cleanup | Raw | No | Explicitly included by Raw resource scope. |
| Persistent destruction | Prepared construction/reuse | **Excluded** | Break-even and Prepared results are not complete lifetime costs. |

### 6.2 Direct construction distributions

| N | Dataset construction median ms | Workspace construction median ms | Ready construction median ms |
|---:|---:|---:|---:|
| 1,024 | 1.2418 | 0.0090 | 1.2527 |
| 16,384 | 23.5695 | 0.1415 | 23.7434 |
| 262,144 | 306.9262 | 0.1594 | 307.1380 |
| 1,000,000 | 796.9452 | 0.2078 | 797.1556 |

These are directly measured composite distributions. Dataset construction
contains validation, conversion/allocation, dataset transfer, and
synchronization. Ready construction additionally contains workspace creation.

### 6.3 Calculated composite indicators

The next table is calculation, not direct stage timing. For each query it
subtracts independent per-boundary medians and forms ratios, then reports the
median across 32 queries. Because the lanes are separately sampled and Raw and
Prepared have different boundaries, the differences must not be treated as an
additive stage decomposition.

| N | Raw minus Prepared ms | Kernel / Raw | Kernel / Prepared | Raw / Prepared point ratio (H3 median) |
|---:|---:|---:|---:|---:|
| 1,024 | 1.0956 | 3.09% | 55.99% | 18.4046 |
| 16,384 | 28.3455 | 2.26% | 63.96% | 27.9965 |
| 262,144 | 310.3794 | 0.17% | 60.33% | 356.1205 |
| 1,000,000 | 812.9405 | 0.19% | 90.55% | 476.0891 |

The kernel fractions strongly justify measuring non-kernel Raw stages before
optimizing CUDA math or reduction code. They do not identify which non-kernel
stage dominates.

## 7. What Prepared V2 can and cannot establish

### 7.1 Defensible use

Prepared V2 is useful as a mechanism demonstration and optimization clue:

- The same approved V2 search result remains bitwise accepted in the matching
  correctness gate.
- Reusing a validated/transposed resident dataset and preallocated workspace
  removes almost all recurring Raw cost in the retained measurements.
- The recurring Prepared boundary becomes increasingly kernel-dominated, reaching
  about 90.55% kernel at N=1,000,000 by the descriptive calculation above.
- Therefore dataset validation/conversion/residency and workspace reuse are the
  highest-value areas to instrument for Raw/first-query optimization.

### 7.2 Prohibited inference

Prepared V2 does not prove a single one-shot GPU speedup because:

- its timing boundary excludes persistent construction and destruction;
- Raw includes call-owned transient cleanup;
- the formal environment is inconclusive;
- construction-plus-query break-even is not complete lifetime cost; and
- a persistent prepared handle changes the usage/lifetime model relative to a
  true one-call Raw search.

The retained break-even rows are all `environment-inconclusive`. Descriptively,
the reported point and conservative query counts range from 2–4 versus CPU Raw
depending on N/query, and 1–2 versus V2 Raw. Those values may guide a future
approved experiment but are not accepted break-even claims.

## 8. Prioritized optimization and integrity plan

Ranks consider expected benefit, engineering complexity, correctness risk,
experimental validity, and relevance to the course objective of single-query
GPU speedup over CPU.

| Rank | Proposal | Expected benefit | Complexity | Correctness risk | Experimental validity / course relevance | Source change | New design decision | New Owner authorization |
|---:|---|---|---|---|---|---|---|---|
| 1 | Retain Raw staged timings for validation, conversion/transpose, allocation, H2D, kernel, D2H, synchronization, and total cleanup residual in a non-formal diagnostic | High information value; prevents optimizing the wrong stage | Low–medium | Low if measurement-only | Highest immediate validity and direct relevance | Yes, benchmark/reporting only | Yes: approve diagnostic boundaries and non-acceptance status | Yes: source scope, verification, and execution |
| 2 | Make telemetry timeout end-to-end and add phase timings around start, wait, reads, kill/drain, plus deadline overrun reason | High for recovering evidence integrity; no threshold relaxation | Low–medium | Low runtime correctness risk; material evidence-integrity risk | Required before another formal acceptance run; indirect performance relevance | Yes, collector/analyzer/tests | Yes or explicit confirmation that this is the approved 900 ms semantics | Yes; material review and execution required |
| 3 | Use the existing Prepared V2 API where multiple single queries legitimately share one immutable dataset | Very high recurring benefit in retained data | Low for experiment, application-dependent | Low within existing API contract | High for repeated single queries, low for a true one-shot course claim | No production change for experiment | No if using existing approved boundary; yes for any new claim | Yes for new experiment/formal execution |
| 4 | Design a validated/prepared dataset lifetime as the primary application interface, with construction and destruction reported explicitly | High when reuse is legitimate | Medium | Medium: ownership, mutation, device, stream, and lifetime semantics | Valid only if the course question permits setup amortization; not equivalent to Raw | Yes, public/integration API | **Yes** | **Yes**, plus material review and full correctness |
| 5 | Eliminate or move the Raw host AoS-to-SoA conversion, for example an AoS-capable V2 path or an explicitly measured device-side conversion | Potentially high, but unquantified until rank 1 | High | High: memory layout, kernel mapping, numerical/evidence behavior | Directly relevant to one-shot Raw GPU speedup | Yes, CUDA/kernel | **Yes**, approved algorithm/design boundary | **Yes**, plus material review and full correctness |
| 6 | Reduce transient allocation overhead through an explicitly scoped pool/arena while preserving observable call semantics | Potentially moderate at small N; unquantified | Medium–high | Medium–high: reuse, concurrency, failure cleanup | Relevant to Raw but may silently become Prepared-like if lifetime is hidden | Yes | **Yes** | **Yes**, plus material review and full correctness |
| 7 | Optimize V2 kernel/reduction first | Low expected Raw benefit from current fractions | Medium–high | Medium | Low priority for Raw; more relevant only after setup costs are removed | Yes | Likely yes for material kernel change | Yes, plus review/correctness/performance |
| 8 | Add pinned memory or overlap transfers with kernels | Unknown; single query offers little natural overlap | Medium–high | Medium | Not justified by current evidence; profile first | Yes | Yes | Yes |

Skipping full-dataset validation in Raw without a new validated-handle contract
is not recommended: rejection of nonfinite input is canonical behavior. Any
attempt to avoid that scan must preserve the contract through an approved
lifetime/validation design rather than silently trusting input.

## 9. Proposed next verification and experiment matrix

All commands below are plans for the Project Owner; none was executed here.
Exact commands must be derived from the checked-in implementation after the
proposed design is approved.

### 9.1 Gate A — telemetry implementation verification

| Check | Matrix | Required evidence | Status now |
|---|---|---|---|
| Collector unit/self-test | Child completes below deadline; child exceeds deadline; child exits nonzero; start/read phase overrun injection | Whole-lifecycle duration, phase durations, status, kill/drain result, exit/stderr | planned |
| Policy test | End-to-end duration `>900 ms` cannot be recorded as an accepted completed query | Analyzer and collector agree without changing 900 ms | planned |
| Cadence test | Synthetic delayed query followed by catch-up | Contiguous sequence and start gaps; 3,000 ms rule unchanged | planned |
| Read-only material review | Collector/analyzer semantics and evidence interpretation | No threshold weakening or hidden sample loss | planned |

No GPU is required for synthetic lifecycle tests. A real `nvidia-smi` diagnostic
is separate and requires Owner execution authorization.

### 9.2 Gate B — smallest performance stage diagnostic

Purpose: attribute Raw cost without making an acceptance claim.

| Dimension | Proposed value |
|---|---|
| Dataset and D | Existing SIFT1M prefixes, D=128 |
| N | 1,024; 16,384; 262,144; 1,000,000 |
| Query | Approved ordinal 0 only |
| Block sizes | Existing approved mapping; no retuning |
| Lanes | CPU Raw, V2 Raw staged, V2 Prepared, Ready construction |
| Raw stage rows | Validation; conversion total and transpose; allocation; H2D; workspace reset if any; kernel; D2H; synchronization; measured total; explicitly calculated residual only |
| Sampling | Reuse 10 warm-ups and 100 retained repetitions if approved for this diagnostic |
| Interpretation | Diagnostic only; no H1–H3 or universal claim; no outlier exclusion |
| Preconditions | Exact-revision correctness accepted after source change; fast math disabled; matching environment identity |

This is 4 N values × 1 approved query rather than the 128-workload formal
matrix. It is the smallest matrix that preserves the observed size-dependent
shape while avoiding unsupported cross-query claims. The residual must be
labelled as a subtraction of independently timed quantities, not a direct
cleanup measurement.

### 9.3 Gate C — targeted telemetry cause discrimination

Only if the phase-instrumented collector is approved:

| Condition | Purpose | Claim limit |
|---|---|---|
| Collector with GPU idle | Establish launch/query phase behavior without benchmark contention | Cannot predict loaded behavior |
| Collector during the small stage diagnostic | Check whether delays correlate with active GPU work and specific phases | Diagnostic correlation, not causation |
| Optional OS/driver trace on a reproduced overrun | Distinguish parent descheduling, child launch, and driver service latency | Requires separately approved tooling/environment handling |

The previous overrun occurred once in 11,698 attempts. A short run that does not
reproduce it cannot rule it out. Duration/sample-count and any statistical
detection rule must be prospectively approved; this report does not invent one.

### 9.4 Gate D — optimization A/B

Proceed only after Gate B identifies a dominant measured stage. Change one
mechanism at a time and compare baseline/candidate at the same exact revision,
environment, N/query/block matrix, rotation, and timing boundaries. Required
verification is:

1. CPU Float32, GPU Float32, and Float64 reference evidence kept distinct;
2. full applicable correctness matrix with index, distance error, and ambiguity;
3. bitwise/evidence requirements preserved where approved;
4. stage diagnostic repeated before any full formal benchmark; and
5. material read-only review for algorithm, numerical, API, or evidence changes.

### 9.5 Gate E — formal acceptance recovery

Only the Owner may authorize and execute a fresh formal S15 benchmark. A new
formal run must use a fresh output directory and new experiment identity, pass
matching exact-revision correctness, preflight, telemetry lifecycle, and
environment gates, and preserve the 900 ms and 3,000 ms policies. The present
packet remains immutable and permanently `environment inconclusive`.

## 10. Verification status and Owner handoff

| Item | Status | Evidence |
|---|---|---|
| Requested HEAD/branch/worktree identity | passed (read-only inspection) | Exact HEAD and branch; empty short status |
| Packet byte/hash integrity | passed (read-only recomputation) | 39/39 entries matched |
| Formal matrix/correctness reading | passed (inspection) | 52,400 raw rows expected by harness; correctness 384/384 accepted |
| Telemetry root-cause localization | passed for localization; low-level cause inconclusive | Sequence 3594 isolated; missing phase trace prevents unique attribution |
| Performance stage attribution | blocked beyond composite boundaries | S15 retained no separate Raw stage distributions except kernel |
| GPU build/test/preflight/benchmark | skipped | Explicitly Owner-only and prohibited for this analysis |
| Existing evidence mutation | skipped | Prohibited; packet unchanged |

File added for Owner review:

- `docs/experiments/S15-FORMAL-4ecdbf2-20261112-01/S15FormalEvidenceAnalysis.md`

No source, policy, configuration, or existing evidence file was changed. The
Owner retains all build, correctness, preflight, formal execution, Git, and
GitHub actions.
