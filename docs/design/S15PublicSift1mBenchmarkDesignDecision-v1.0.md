# S15 Public SIFT1M Benchmark Design Decision v1.0

Status: **APPROVED — implementation and correctness/integrity verification authorized; formal performance benchmark requires separate Owner approval**\
Approval record: [S15PublicSift1mBenchmarkApprovalRecord.md](../governance/S15PublicSift1mBenchmarkApprovalRecord.md)\
Approval date: 2026-11-11 (Asia/Bangkok)\
Approved candidate design SHA-256: `4d91b8a659b94100654077988491a4a1b00a1405c067bfad508324bd267e0b16`\
Prepared against repository baseline: `4113b0705759` (`main`, clean when audited)\
Prepared date: 2026-11-11 (Asia/Bangkok)\
Workload: primary single-query public-dataset experiment\
Dataset: INRIA TexMex ANN_SIFT1M\
Numerical policy: `S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0`\
Numerical-policy SHA-256: `503aa6b2c97567d814a9316fb0f2f9890d5472aea2bcf1f928ac505c29d1aa39`

## 1. Decision requested and authority boundary

This document is the Owner-approved S15 experiment design for single-query
exact nearest-neighbor search on the public SIFT1M dataset. Design approval is
not a correctness receipt, benchmark result, or performance claim.

The Project Owner explicitly confirmed all 12 decisions in Section 14 and
authorized S15 implementation and correctness/integrity verification on
2026-11-11 (Asia/Bangkok). This approval does not authorize the formal
performance benchmark, dependency or environment changes, dataset download,
Git/GitHub mutation, or amendment of the canonical project specification.

S15 supplements the required seeded synthetic evidence. Public SIFT1M has a
fixed dimension and only 1,000,000 base vectors, so it cannot satisfy or replace
the canonical 12-case synthetic matrix in AC-011. V4 and all batch-query
evidence are excluded from S15 and remain separately identifiable as optional
secondary work.

## 2. Sources and audit findings

### 2.1 Governing sources

- `docs/specifications/FinalProjectSpecification-v1.0.md`
- `docs/specifications/FinalProjectSpecification-S6-Numerical-Addendum-v1.0.md`
- `docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md`
- `docs/design/S6ApprovedImplementationDesign-v1.0.md`
- `docs/governance/SourceOfTruth.md`
- `docs/governance/DecisionRegister.md`
- `docs/requirements/AcceptanceCriteria.md`
- `docs/requirements/RequirementsTraceability.md`
- `docs/performance/BenchmarkProtocol.md`

The controlling existing requirements are exhaustive L2-squared search,
Float32 production arithmetic, Float64 diagnostic reference, smallest-index
tie-breaking, correctness before performance, explicit timing boundaries,
10 warm-ups plus 100 retained samples, median/p5/p95 reporting, fast math
disabled, revision-bound evidence, and primary single-query separation from
batch/V4 evidence. No absolute performance target or required speedup is
currently approved.

### 2.2 S10 public-dataset assets

| Asset | Reusable capability | S15 limitation or gap |
|---|---|---|
| `benchmarks/s10_sift1m_loader.{hpp,cpp}` | Strict fvecs size/header parsing; little-endian Float32 decoding; full base/query finiteness validation; in-process SHA-256; external-dataset-root enforcement; AoS prefix views | Retains only query ordinal 0; expected hashes are recorded but not compared with approved constants by the runner; API cannot return a selected query set |
| `tests/s10_sift1m_loader_tests.cpp` | Loader fixtures and failure-path coverage | Needs selected-query, ordinal-list, expected-hash, duplicate/out-of-range ordinal, and multi-query retention coverage |
| `benchmarks/s10_sift1m.cpp` | Matching-revision correctness gate; CPU and CUDA V0-V3 paths; Raw E2E and kernel timing; identity-bound raw samples and manifest | Hard-coded to query 0, `D=128`, four sizes, fixed block size 256, 20 correctness cases, and no Prepared V2 boundary or construction costs |
| `benchmarks/config/s10-sift1m.json` | Public source, file names, sizes, S10 matrix, sampling, and boundary declarations | Exploratory S10 identity and choices must not be silently inherited as S15 approval |
| `scripts/run-s10-sift1m.ps1` | Clean revision checks; source/build/binary identities; strict floating-point flag checks; external output/dataset controls; provenance manifest; pre/post environment capture | Older telemetry lifecycle has no bounded per-query timeout or attempt ledger; no structured effective top-level invocation; no preserved prerequisite CTest log/JUnit pair; final design is fixed to S10 schemas |
| `scripts/analyze-s10-sift1m.mjs` | Matrix validation; deterministic 2,000-resample median bootstrap; conservative ratio intervals; environment analysis; atomic acceptance publication | Hard-coded S10 matrix/schemas and fixed V0-V3 comparisons; S14 fail-safe telemetry classification is absent |
| `docs/experiments/S10-SIFT1M-Exploratory/ExploratoryResultsReport.md` | Historical dataset identity and exploratory context | Immutable `EXPLORATORY / VALID-UNREVIEWED` evidence; must not be relabeled, modified, or rebound to S15 |

### 2.3 S13 single-query methodology to reuse

The S13 configuration, harness, runner, and analyzer provide the closest
single-query method for S15:

- CPU Raw, CUDA V2 Raw, and CUDA V2 Prepared are measured as distinct lanes.
- Prepared dataset and workspace construction costs are measured separately.
- Persistent prepared resources are reused across prepared searches; their
  destruction remains excluded and the result is not described as a complete
  lifetime cost.
- Lane order rotates deterministically by workload ordinal and iteration.
- Each distribution uses 10 warm-ups and 100 retained repetitions.
- Median, p5, p95, and deterministic 2,000-resample 95% bootstrap intervals
  are retained.
- Speedup is `median(reference) / median(candidate)`. The conservative ratio
  interval is `[reference_ci_low / candidate_ci_high,
  reference_ci_high / candidate_ci_low]`.
- A configuration is `supported` only when the ratio interval is wholly above
  1, `rejected` only when wholly below 1, and otherwise `inconclusive`.
- Construction-plus-query break-even excludes persistent-resource destruction
  and is not complete lifetime cost.

S15 should reuse those methods, not any unreviewed S13 measurement as S15
evidence. S15 must add the canonical `BND-KERNEL` distribution, which remains
required supporting evidence for the public single-query experiment.

### 2.4 S14 integrity controls to reuse

S15 should reuse the prospectively approved S14 controls where they are
workload-independent:

- 1,000 ms telemetry cadence, 3,000 ms maximum accepted start-time gap, and a
  900 ms timeout for each `nvidia-smi` query;
- monotonic cadence accounting plus wall-clock correlation;
- contiguous attempt identities and explicit query duration, exit, failure,
  and timeout records;
- collector ready/stop/lifecycle evidence, complete benchmark-interval
  coverage, benchmark-process observation, and unauthorized-process checks;
- independently derived environment status rather than trusting a
  runner-authored verdict;
- structured effective top-level invocation and derived human-readable command;
- preserved prerequisite CTest text log and JUnit output, with sizes and hashes;
- blocking treatment for malformed or identity-contradictory evidence; and
- retained samples but performance-hypothesis `inconclusive` treatment for
  incomplete telemetry, excessive gaps, query failure/timeout, early or forced
  collector termination, unauthorized GPU processes, or observed hardware
  thermal slowdown.

The S14 evidence directories are immutable and are not inputs to S15 results.

## 3. Recommended experiment question

For public SIFT1M single-query exact search, how do CPU Float32 Raw E2E, CUDA V2
Raw E2E, and CUDA V2 Prepared E2E latency compare across candidate-set size and
a predeclared, non-cherry-picked set of public queries, and what setup cost must
be amortized before Prepared V2 provides a sustained benefit?

The formal experiment keeps four reporting levels distinct:

| Level | Meaning | Permitted status or output |
|---|---|---|
| **Evidence Acceptance** | Whether correctness, provenance, environment, sampling, schema, identity, and integrity gates authorize formal analysis | `accepted for performance analysis`, `failed correctness`, or `blocked`; environment contamination may retain measurements while making performance interpretation inconclusive |
| **Per-workload Performance Outcomes** | H1-H3 comparisons for each exact `(N, query_ordinal)` workload | `supported`, `rejected`, or `inconclusive` under the predeclared conservative ratio interval |
| **Per-N Descriptive Results** | Grouped summaries and counts across the 32 query-specific rows at one `N` | Descriptive latency, speedup, interval, and outcome counts only; never an additional acceptance gate or replacement hypothesis |
| **Universal Hypothesis Outcomes** | The predeclared all-128 verdict for each of H1-H3 | `supported`, `rejected`, or `inconclusive` under Section 8.2 |

Evidence Acceptance never depends on obtaining a favorable speedup. If all
evidence gates pass, a rejected per-workload comparison or rejected Universal
Hypothesis Outcome remains a valid result in a completed experiment. It is not
a failed evidence packet and does not justify changing the query set,
configuration, statistical rule, or acceptance criterion after execution.

## 4. Recommended workload matrix

### 4.1 Dataset sizes and dimension

Use unmodified prefixes of `sift_base.fvecs`:

- `N in {1024, 16384, 262144, 1000000}`
- `D = 128`

The 1,000,000-row endpoint is the complete public base file. The canonical
synthetic endpoint 1,048,576 is unavailable in SIFT1M and must not be implied.
No normalization, reordering, padding, augmentation, pruning, or approximate
index is permitted. All paths consume the same original Float32 AoS logical
values; V2 preparation may perform the approved internal AoS-to-SoA conversion.

### 4.2 Query coverage

Recommend 32 query ordinals selected deterministically across the complete
10,000-record query file by:

`q[k] = floor(k * 9999 / 31)` for `k in [0,31]`.

The expanded, configuration-bound list is:

`0, 322, 645, 967, 1290, 1612, 1935, 2257, 2580, 2902, 3225, 3548, 3870, 4193, 4515, 4838, 5160, 5483, 5805, 6128, 6450, 6773, 7096, 7418, 7741, 8063, 8386, 8708, 9031, 9353, 9676, 9999`.

This retains query 0 for an explicitly limited S10 bridge while covering the
full public query-file order. The ordinal list is an approved experiment-design
choice, not a claim of statistical representativeness of all possible query
distributions. The configuration must carry both the formula and expanded list;
the runner must reject any mismatch, duplicate, or out-of-range ordinal.

The primary matrix therefore contains 128 `(N, query_ordinal)` workloads. Rows
remain query-specific. Across-query summaries may be reported descriptively by
`N`, but they must not replace the 128 primary rows or be treated as independent
repetitions of one latency distribution.

### 4.3 Production lanes and V2 configuration

Required lanes for each workload:

| Lane | Role |
|---|---|
| CPU Float32 `CPU-E2E-RAW` | Production CPU comparator |
| CUDA V2 `BND-KERNEL` | Complete V2 production kernel sequence |
| CUDA V2 `BND-E2E-RAW` | Primary GPU raw one-shot result |
| CUDA V2 `BND-E2E-PREP` | Supplementary steady-state prepared dataset/workspace reuse result |

No V0, V1, V3, V4, batch API, throughput claim, or cross-hardware claim is in
scope. S15 answers a public-data V2 reuse question; it does not replace the
synthetic V0-V3 core evidence.

No S15 tuning is approved. The approved predeclared V2 block mapping is
`{1024:256, 16384:128, 262144:512, 1000000:256}` for `D=128`. The first three
values are the exact matching S7 frozen selections. The 1,000,000 value is a
approved carry-forward of the S7 1,048,576/D128 selection and the historical
S10 value; it is not represented as independently tuned for 1,000,000.
Owner approval of this mapping is recorded. Retuning or selection from S15
final samples is prohibited.

### 4.4 Construction distributions

For each `N`, independently record 10 warm-ups and 100 retained samples for:

- `COST-PREP-DATASET-CONSTRUCTION`;
- `COST-V2-WORKSPACE-CONSTRUCTION`; and
- `COST-PREPARED-READY-CONSTRUCTION`.

Construction is dataset-specific, not query-specific, so it is measured once
per `N`. Persistent-resource destruction is excluded and declared in every
row. These costs support amortization only; they are not folded into
`BND-E2E-PREP`. They must accompany every Prepared V2 speedup claim together
with the applicable break-even result; a Prepared V2 latency ratio may not be
presented alone as first-query or complete lifetime performance.

## 5. Correctness prerequisite

Performance analysis is authorized only by a matching clean-revision S15
correctness gate for the same config hash, dataset hashes, build and arithmetic
environment, binary/source identities, query list, V2 block mapping, and public
workload matrix.

For all 128 workloads, the gate must retain separate results for:

- CPU Float32 production;
- CUDA V2 Raw production; and
- CUDA V2 Prepared production using the same immutable prepared prefix.

The approved minimum matrix is 384 production-result checks. For every
workload, evidence must demonstrate:

- exhaustive candidate coverage and smallest-index equal-computed-distance
  behavior;
- approved ascending-dimension fused-FMA graph and arithmetic environment;
- independent all-candidate Float32 lexicographic verification;
- independent Float64 reference and every-candidate UD-001 bound evidence;
- UD-002 exact or conservative contender classification with provenance;
- ordered UD-003 verdict with argmin, distance error, and ambiguity retained
  separately;
- finite Float32 candidate distances;
- complete production-path and reduction evidence; and
- bitwise agreement of CPU, Raw V2, and Prepared V2 production results with
  the independently verified Float32 result.

Each workload must end in qualified `accepted-unambiguous` or
`accepted-ambiguous`; any failed or blocked required workload blocks formal
performance acceptance. Published `sift_groundtruth.ivecs`, if present, may be
captured as supplementary full-dataset provenance/diagnostics only. It is not
the approved numerical oracle and does not apply to smaller prefixes.

Existing S6 fixtures for invalid inputs, signed zero, ties, graph behavior,
reduction, and production paths remain mandatory prerequisites. They need not
be manufactured from the public dataset, but their revision/build identity must
match the S15 candidate.

## 6. Timing boundaries and run ordering

### 6.1 `CPU-E2E-RAW`

Start at entry to the CPU Float32 production call with the selected AoS prefix
and query already loaded. Include production validation and synchronous host
result return. Exclude dataset download, archive extraction, file I/O, fvecs
validation, SHA-256 calculation, query selection, Float64/reference work,
correctness classification, logging, and report generation.

### 6.2 `BND-KERNEL`

Use CUDA events on one stream immediately before the first required V2
production kernel and immediately after the last required production kernel.
Include all V2 reduction stages and status propagation. Dataset/query/result and
workspace storage are already device-resident. Exclude allocation, AoS-to-SoA
conversion, host preprocessing, transfers, independent validation, diagnostics,
logging, and report generation.

### 6.3 `BND-E2E-RAW`

Use the canonical raw one-shot boundary: include validation, per-call
AoS-to-SoA conversion, transient allocation, dataset/query H2D transfer,
complete production kernels, result/status D2H transfer, synchronization, and
call-owned cleanup. Stop after host result/status is available and transient
resources are released. Apply the common exclusions in Section 6.1.

### 6.4 `BND-E2E-PREP`

Prepare the selected dataset prefix and compatible workspace before timing.
Start immediately before per-query validation. Include status reset, query H2D,
complete production kernels, result/status D2H, synchronization, and per-search
cleanup. Stop when the synchronous prepared search returns. Exclude dataset
preparation, persistent workspace allocation, and persistent-resource
destruction; report the first two separately under Section 4.4.

This boundary measures steady-state reuse after both the immutable prepared
dataset and compatible workspace exist. It does not measure first-query
latency, time-to-first-result from raw host data, or complete resource lifetime.
Any report, table, plot, abstract, or conclusion that states a Prepared V2
speedup must identify this boundary and place the three construction-cost
distributions and applicable point/conservative break-even result beside it or
through a direct evidence reference.

### 6.5 Ordering

Measure construction distributions separately before the stable prepared
resources used for search samples. For each workload, rotate the four latency
lanes deterministically by `(workload_ordinal + iteration) mod 4`; record global
run order and lane order. Warm-ups participate in the same rotation but are not
retained. The exact expanded workload and lane sequence must be derivable from
the configuration and validated by the analyzer.

## 7. Sampling and statistical confidence

For each of the 128 workloads and each of the four timing lanes:

- execute exactly 10 warm-ups;
- retain exactly 100 measurements in integer nanoseconds;
- report median, p5, and p95 using `linear_n_minus_1` interpolation; and
- compute a deterministic percentile-bootstrap 95% interval for the median
  using 2,000 resamples and an identity-derived seed.

Do not discard outliers silently. Any approved exclusion must be predeclared,
retain the original row, and produce an audit record. No exclusion rule is
proposed for S15; anomalies therefore remain in the primary distribution and
are discussed as limitations.

The bootstrap interval describes sampling uncertainty for the measured
distribution on the observed environment. It is not a cross-query,
cross-hardware, or causal confidence interval. Conservative speedup intervals
combine separate median intervals and are not paired-sample causal intervals.

### 7.1 Bounded non-performance preflight

Before separately authorized formal execution, run a bounded planning
preflight whose only purposes are to estimate wall-clock duration, required
free disk, and expected evidence-packet size. Preflight is operational planning,
not tuning, correctness acceptance, performance evidence, or a partial formal
run.

The preflight has two mandatory phases:

1. **Static projection:** enumerate the unchanged 128-workload matrix, four
   latency lanes, three construction distributions, 10/100 formal sampling
   counts, correctness record classes, telemetry cadence, and required packet
   artifacts. Compute projected raw-row counts and evidence bytes from the
   proposed S15 schemas, measured serialization sizes from integrity fixtures,
   and explicitly recorded assumptions. Capture available disk space and report
   the projected requirement without inventing an unapproved safety factor.
2. **Bounded operational probe:** use only approved workload identities and cap
   execution at query ordinal 0 for each of the four approved `N` values, one
   invocation per planned production lane, and one invocation per construction
   cost at each `N`: at most 28 production/construction invocations in one
   preflight attempt. There are no automatic retries, warm-up loops, repetition
   distributions, CUDA-event or boundary sample rows, percentiles, bootstrap
   intervals, comparisons, break-even verdicts, or hypothesis classifications.
   Record only coarse stage and whole-probe wall-clock duration to project a
   planning range for the later full run.

Preflight output must use a fresh directory labeled `preflight-non-performance`
and a schema that the formal analyzer rejects. It must not be copied, promoted,
merged, or counted in correctness or final performance evidence. A successful
preflight means only that the projected runtime/storage plan was produced and
the bounded orchestration completed; it cannot pass AC-012 through AC-015.

The preflight must consume the same approved config hash, dataset hashes, query
formula and expanded list, V2 block mapping, timing-boundary definitions,
statistical criteria, and no-retuning rule as the planned formal run. Its
bounded probe does not amend or reduce the formal query set. Preflight findings
may change only operational provisioning, output location, or a later execution
authorization decision. Any proposed change to query selection, benchmark
configuration, statistical criteria, block sizes, tuning decisions, or
hypotheses requires a new Owner-reviewed design revision rather than an
automatic preflight adjustment.

## 8. Speedups, hypotheses, and approved S15 performance rules

### 8.1 Speedup calculation

For compatible boundaries, define:

`speedup = median(reference) / median(candidate)`.

Define the conservative interval as:

`[reference_ci_low / candidate_ci_high,
  reference_ci_high / candidate_ci_low]`.

Per workload, classify `supported` when the lower bound is greater than 1,
`rejected` when the upper bound is less than 1, and `inconclusive` otherwise.
Compare only like workload/query identities. Kernel-only is not compared with
an end-to-end boundary. These 128 query-specific classifications are the
Per-workload Performance Outcomes; they do not by themselves state the
Universal Hypothesis Outcome.

For each `N`, report the 32 query-specific latency and speedup rows plus counts
of supported, rejected, and inconclusive outcomes. Optional grouped medians or
ranges across those rows are Per-N Descriptive Results only. They must retain
the number of contributing queries and must not be labeled as confidence from
3,200 interchangeable repetitions, a per-N hypothesis verdict, or Evidence
Acceptance.

### 8.2 Proposed hypotheses

- **H1:** Prepared V2 E2E latency is lower than CPU Raw E2E latency for every
  one of the 128 workloads.
- **H2:** Raw V2 E2E latency is lower than CPU Raw E2E latency for every one of
  the 128 workloads.
- **H3:** Prepared V2 E2E latency is lower than Raw V2 E2E latency for every one
  of the 128 workloads.

For each universal hypothesis, the approved rule is:

- `supported` only if all 128 per-workload comparisons are supported;
- `rejected` if at least one per-workload comparison is rejected; and
- `inconclusive` if none is rejected and at least one is inconclusive.

Always report the supported/rejected/inconclusive counts by `N` and query and
the complete decision table. Counts never override the universal rule. This is
an approved S15-specific interpretation; it is not a new canonical speedup
requirement. When Evidence Acceptance passes, a Universal
Hypothesis Outcome of `rejected` is a valid completed S15 result, just as a
universal `supported` or `inconclusive` outcome is; favorability is not an
evidence gate.

### 8.3 Prepared break-even

For references whose median per-query latency exceeds the Prepared V2 median,
report the point estimate:

`floor(median(COST-PREPARED-READY-CONSTRUCTION) /
       (median(reference) - median(BND-E2E-PREP))) + 1`.

Report the conservative estimate only when
`reference_ci_low - prepared_ci_high > 0`, using construction `ci_high` in the
numerator. Otherwise classify sustained break-even as inconclusive or absent as
defined in the configuration. Keep CPU-reference and Raw-V2-reference
break-even tables separate. These are construction-plus-query amortization
results, not complete lifetime costs, because persistent destruction is
excluded. Every H1 or H3 Prepared V2 speedup statement must be accompanied by
the applicable construction-cost distributions and break-even classification.
If sustained break-even is absent or inconclusive, the steady-state speedup may
still be reported but must carry that limitation prominently and must not be
rephrased as a first-query or lifetime speedup.

## 9. Dataset provenance and SHA-256 verification

The repository records the public source as:

- homepage: `http://corpus-texmex.irisa.fr/`;
- archive: `ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz`;
- dataset label: INRIA TexMex ANN_SIFT1M.

Live availability of the source was not established by this design audit and is
not assumed. Download/acquisition remains an external Owner action. Dataset
bytes must remain outside Git and must not be redistributed in the evidence
packet.

Owner approval establishes the following S10-recorded extracted-file identities
as required S15 configuration values:

| File | Required bytes | Approved required SHA-256 |
|---|---:|---|
| `sift_base.fvecs` | 516,000,000 | `21f66e2975057b5728ba56de1c825bac4f4d89d596609ae985741c6242631816` |
| `sift_query.fvecs` | 5,160,000 | `f7fc9be140accdfd64116c2fa2365ecdb69b8f084970c6b0532db5ff79ac8fdc` |

Before any correctness or performance stage, the runner must recompute both
SHA-256 values over the complete files, verify byte counts and fvecs schema,
validate all base and all 10,000 query values as finite Float32, and fail closed
on any mismatch. The benchmark must independently bind the observed hashes to
its config and correctness gate; command-line strings alone are insufficient.

If `sift.tar.gz`, `sift_groundtruth.ivecs`, or `sift_learn.fvecs` is retained,
record its observed byte count and SHA-256 in the provenance manifest without
inventing an expected value. Also retain acquisition timestamp, source URL,
method/tool, archive hash when available, and extraction procedure. An upstream
MD5 may be captured as supplementary transport provenance, but it does not
replace the mandatory SHA-256 checks above.

## 10. Evidence integrity and acceptance classification

S15 must write only to a fresh output directory and retain at least:

- exact revision, clean tree state, source/config/design/numerical-policy hashes;
- dataset provenance and mandatory file hashes;
- expanded query list and V2 mapping;
- build cache, compile commands, source identity, strict floating-point flags,
  artifact manifest, binary hashes, GPU/CPU/toolchain/OS identities;
- structured top-level invocation, per-stage command and result logs;
- prerequisite CTest text/JUnit evidence;
- matching correctness ledger and gate;
- non-performance preflight projection/probe records kept outside formal
  correctness and performance inputs;
- raw samples, summaries, comparisons, break-even output, and final
  classification;
- pre/post GPU snapshots, telemetry attempts/metrics/processes/errors, collector
  lifecycle, and benchmark interval; and
- a SHA-256 packet manifest covering every retained non-dataset artifact.

Acceptance order is:

1. **Blocked:** malformed evidence, identity/hash/schema contradiction, missing
   mandatory provenance, failed prerequisite, or absent matching correctness.
2. **Failed correctness:** any demonstrated contract, graph, numerical,
   exhaustive-search, tie, production-path, or result-equivalence defect.
3. **Environment inconclusive:** retain valid measurements but force all
   performance comparisons, universal hypotheses, and supported break-even
   claims to `inconclusive` under the adopted S14 conditions.
4. **Accepted for performance analysis:** all mandatory correctness,
   provenance, identity, environment, and sampling gates pass. Hypotheses then
   receive their evidence-backed supported/rejected/inconclusive outcomes.

The final receipt must expose Evidence Acceptance separately from all 128
Per-workload Performance Outcomes, four Per-N Descriptive Result groups, and
the three Universal Hypothesis Outcomes. A universal rejection cannot downgrade
accepted evidence or change a completed experiment into a failed run.

No `acceptance.json` or equivalent final receipt may be published until the
analyzer independently validates the full packet. Runner-authored status is
input evidence, not the final authority.

## 11. Authorized implementation scope

No CUDA production algorithm or public API change appears necessary. The
existing CPU, Raw V2, prepared-dataset, workspace, and Prepared V2 operations
cover the approved lanes. The experiment infrastructure requires these changes:

1. Add an S15 configuration carrying approved hashes, query formula/list,
   sizes, V2 mapping, hypotheses, statistics, timing, and telemetry policy.
2. Extend the S10 loader through a backward-compatible API that retains a
   selected query set while still validating and hashing the complete query
   file. Keep the S10 single-query API behavior unchanged.
3. Add a new S15 benchmark harness and schemas rather than rewriting S10
   historical schemas. Implement multi-query correctness, Raw/Prepared V2,
   kernel-only, construction distributions, and deterministic four-lane order.
4. Add an S15 runner using S14 bounded telemetry, structured invocation,
   prerequisite-log preservation, identity revalidation, fresh-output rules,
   and mandatory expected-hash rejection.
5. Add an S15 analyzer combining S13 statistics/comparisons/break-even with S14
   environment and evidence-integrity classification.
6. Add loader, harness, analyzer, runner, telemetry, corruption, identity, and
   negative-classification integrity tests and register them in CMake/CTest.
7. Create a new S15 evidence/report directory only after a separately
   authorized execution. Do not modify S10-S14 evidence.

Implementation and correctness/integrity verification are authorized within
this approved scope. Any discovery that a CUDA production or public API change
is actually required must return to the Owner as scope expansion before
modification. Formal performance benchmark execution remains unauthorized
until the Owner separately approves it.

## 12. Alternatives considered

### A. Reuse only query 0

Lowest cost and closest S10 comparison, but too vulnerable to query-specific
behavior for a formal public-dataset study. Rejected as the recommendation;
retained only as one member of the proposed 32-query set.

### B. Use all 10,000 queries

Maximum public-query coverage, but applying 10 warm-ups and 100 retained
samples independently to every `(N,query,boundary)` distribution is
disproportionate for S15 and would greatly expand correctness/evidence volume.
Not recommended without a different, separately approved sampling design.

### C. Use the first 32 queries

Simple and aligns with a common batch prefix, but covers only one contiguous
region of file order. Deterministic full-file stratification is preferred.

### D. Retune V2 for SIFT1M

Could optimize this exact data/size domain, but violates the current no-tuning
scope and would require independent tuning samples, a separate seed/query set,
configuration freeze, and correctness rerun. Deferred.

### E. Include V0, V1, and V3

Would reproduce S10's broad variant comparison, but dilutes the S15 question
about S12-S14 prepared V2 improvements and substantially increases runtime.
The accepted synthetic V0-V3 evidence remains the core variant study. Not
recommended for S15.

### F. Treat SIFT1M as core AC-011 coverage

Invalid because SIFT1M is fixed at `D=128`, has 1,000,000 rather than 1,048,576
base rows, and is not the recorded seeded synthetic generator. Rejected.

## 13. Risks and mitigations

| Risk | Consequence | Mitigation |
|---|---|---|
| Public source becomes unavailable or changes | Dataset acquisition cannot be reproduced from URL alone | Bind extracted files by mandatory byte counts/SHA-256; retain acquisition metadata; keep bytes external |
| Query selection is mistaken for population inference | Overstated generality | Predeclare deterministic ordinals; retain per-query rows; label across-query summaries descriptive |
| 128-workload universal rule is stringent | One rejected case rejects a universal speedup claim | Treat rejection as a valid result; report all per-case counts and intervals; do not weaken post hoc |
| Repeatedly hashing/parsing large files contaminates timing | Inflated or incomparable latency | Complete loading/validation/hashing before timed regions; record boundaries explicitly |
| Prepared setup is hidden | Misleading steady-state claim | Report three construction distributions and separate break-even tables |
| Persistent destruction is excluded | Break-even is not lifetime-complete | Label every setup/amortization result accordingly |
| Carry-forward block size is not exact-size tuning | Configuration may not be optimal at 1,000,000 | Preserve recorded Owner approval and provenance; prohibit optimality claims |
| Long execution increases environmental drift | Wider or biased distributions | Use S14 telemetry continuity/lifecycle controls and deterministic rotation |
| Historical S10/S14 evidence is accidentally rebound | Invalid provenance claim | New schemas, output directory, experiment ID, packet manifest, and immutable-history policy |
| Loader generalization regresses S10 | Existing integrity path changes | Backward-compatible API and regression tests; do not alter historical artifacts |

## 14. Owner-approved decisions

The Project Owner explicitly approved all 12 decisions on 2026-11-11
(Asia/Bangkok):

1. **Experiment status:** S15 is approved as a formal public-dataset experiment
   supplementing, not replacing, synthetic core acceptance.
2. **Question and scope:** CPU Float32, V2 Raw, V2 Prepared, V2 kernel,
   construction costs, and exclusion of V0/V1/V3/V4/batch.
3. **Matrix:** `N={1024,16384,262144,1000000}`, `D=128`, and the
   32-query formula plus expanded ordinal list.
4. **V2 configuration:** `{1024:256,16384:128,262144:512,1000000:256}`
   and the 1,000,000 carry-forward limitation, with no S15 retuning.
5. **Correctness gate:** the 128-workload/384-production-check matrix,
   required S6 evidence, bitwise equivalence, and matching-identity rule.
6. **Timing and sampling:** the four timing lanes, three construction
   distributions, deterministic rotation, 10/100 counts, percentile method,
   and 2,000-resample 95% median bootstrap.
7. **Performance interpretation:** H1-H3, speedup/interval formulas,
   and universal-all-128 outcome rule. This remains S15-specific and does not
   amend the absence of a canonical required speedup.
8. **Break-even:** the point/conservative formulas and explicit
   exclusion of persistent-resource destruction.
9. **Dataset identity:** the two recorded extracted-file byte counts and
   SHA-256 values as mandatory S15 inputs and the external/no-redistribution
   policy.
10. **Integrity policy:** reuse of S14 telemetry, lifecycle, command,
    prerequisite-log, blocking, and environment-inconclusive controls for S15.
11. **Implementation scope:** new S15 config/harness/runner/analyzer/tests
    plus backward-compatible loader extension, with no CUDA production/API
    change unless separately escalated.
12. **Future execution boundary:** design/implementation approval
    does not authorize the formal benchmark; a clean exact-revision run requires
    separate Owner authorization after integrity and correctness verification.

## 15. Approval status and authorization boundary

Current status: **Owner-approved**.

No implementation, build, test, correctness run, hardware probe, or benchmark
was performed while preparing or synchronizing this design. No existing S10-S14
evidence was modified. S15 implementation and correctness/integrity
verification are authorized in an Owner-prepared worktree. Formal performance
benchmark execution requires separate explicit Owner approval and remains
unauthorized by this design approval.
