# S14 Batch V4 Final Experiment Report

Status: **ready for Owner acceptance**. This report archives a completed
optional V4 batch experiment. It does not change the predeclared acceptance
rule, and a count of supported configurations is not an aggregate hypothesis
acceptance.

## 1. Identity and scope

| Field | Value |
|---|---|
| Experiment | `S14-BATCH-V4-20261111-02` |
| Benchmark revision | `00e3207cc020a99f88b927865c213b7100cfb5de` |
| Benchmark tree state | `clean` |
| Analyzer revision | `d9fd6613a9e089300bb92bf7fef407ccdbeec682` |
| Recovery | Analysis of existing measurements; no performance rerun |
| Workload | Secondary batch query |
| Candidate | Optional GPU V4 batch |
| References | CPU batch and sequential Prepared V2 |
| Design | `S14BatchBenchmarkDesignDecision-v1.0`, Owner approval `S14-EVIDENCE-REMEDIATION-OWNER-APPROVAL-001` |
| Numerical policy | `S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0` |

Single-query V0-V3 remains the primary/core evidence. This S14 result is
separate optional V4 evidence and cannot replace it.

## 2. Question and method

S14 evaluated two universal hypotheses across all 36 combinations of
`N in {1024,16384,262144,1048576}`, `D in {8,32,128}`, and batch size in
`{1,8,32}`:

- H1: GPU V4 Batch E2E latency is lower than CPU Batch E2E latency.
- H2: GPU V4 Batch E2E latency is lower than sequential Prepared V2 E2E
  latency.

Each distribution used 10 warm-ups and 100 retained measurements with
deterministic run-order rotation. The ratio is
`median(reference) / median(candidate)`. A configuration is supported only when
the conservative interval is wholly above 1, rejected when wholly below 1,
and otherwise inconclusive. The predeclared `optional-all-36` aggregate rule
requires all 36 configurations to be supported; one rejected configuration
rejects the universal hypothesis.

The benchmark retained 21,600 raw samples: 216 distributions with 100 samples
each. Analysis produced 216 summaries, 72 comparisons, and 72 break-even rows.
Kernel-only timing was intentionally omitted by the approved minimal S14
design.

## 3. Correctness prerequisite

Correctness status is **PASS** (`passed-clean-candidate`) at the benchmark
revision and configuration. All 36 batch configurations passed:

- distinct-query construction;
- CPU batch, sequential Prepared V2, and V4 batch results;
- bitwise Float32 equivalence; and
- the independent Float64 reference.

The gate records 384 Float64 query checks, zero failed configurations, and zero
blocked configurations. Float64 remains a reference/diagnostic lane; it does
not replace the Float32 CPU or GPU production result.

## 4. Environment and execution

The captured environment is Windows 11 on an Intel Core i7-11370H with an
NVIDIA GeForce RTX 3060 Laptop GPU, compute capability 8.6, CUDA Toolkit 13.3,
MSVC 19.51, Release/SM86, strict floating-point settings, and fast math
disabled.

Environment analysis is `accepted-for-analysis`. There are 2,438 metric,
process, and attempt records. The maximum metric and process sampling gap is
1,211 ms, below the approved 3,000 ms maximum. No unauthorized GPU process or
hardware thermal slowdown sample was observed.

The preserved top-level benchmark command is:

```powershell
& 'D:\ku\Parallel Project\scripts\run-s14-batch-benchmarks.ps1' '-Mode' 'Final' '-BuildDirectory' 'D:\ku\Parallel Project\build-s14-fix-clean-00e3207-sm86' '-OutputDirectory' 'D:\ku\Parallel Project\build-s14-fix-clean-00e3207-sm86\s14-final-02' '-ExpectedRevision' '00e3207cc020a99f88b927865c213b7100cfb5de' '-ExperimentId' 'S14-BATCH-V4-20261111-02' '-EnvironmentApproval' 'D:\ku\Parallel Project\build-s14-fix-clean-00e3207-sm86\s14-environment-approval-02.json'
```

The prerequisite CTest, correctness, telemetry, and benchmark stages record
exit code `0`. The original analysis stage records exit code `1`. Analyzer
revision `d9fd6613...` subsequently recovered only the analysis products from
the unchanged 30-file measurement packet. SHA-256 comparison found zero
original/recovery mismatches. The recovery did not rerun performance and did
not modify original evidence.

## 5. Performance outcomes

| Hypothesis | Supported | Rejected | Inconclusive | Aggregate outcome |
|---|---:|---:|---:|---|
| H1 — V4 Batch vs CPU Batch | 35 | 1 | 0 | `rejected` |
| H2 — V4 Batch vs sequential Prepared V2 | 28 | 2 | 6 | `rejected` |

The 35/36 and 28/36 counts describe per-configuration classifications only.
They do not satisfy or alter `optional-all-36`; neither hypothesis is accepted
overall.

Break-even output is construction-plus-batch amortization, not complete
lifetime cost, because persistent-resource destruction is excluded. Against
CPU batch, 35 rows report a finite supported break-even and one reports no
sustained finite break-even. Against sequential Prepared V2, 28 rows report a
finite supported break-even, four are uncertainty-inconclusive point estimates,
and four report no sustained finite break-even.

## 6. Evidence integrity and limitations

- The complete 36-file recovered packet, including all raw samples and
  telemetry, is retained under [`evidence/`](evidence/).
- [`EVIDENCE_INDEX.md`](EVIDENCE_INDEX.md) records the source mapping and
  independent count/hash audit. The packet manifest binds every retained
  source-derived file.
- The analysis recovery and benchmark have different revisions and roles. The
  analyzer revision must not be presented as a new measured revision.
- The exact recovery shell invocation was not captured. Recovery provenance,
  analyzer revision, source/recovery directories, file counts, hashes, and the
  no-rerun classification are retained; no missing command is invented.
- Results cover one Windows laptop and one RTX 3060 Laptop GPU. They are not
  universal hardware claims.
- S14 is secondary optional batch evidence. It does not amend V0-V3 core
  acceptance or the primary single-query conclusions.

## 7. Final disposition

The evidence packet is internally consistent and complete for Owner review.
Correctness and environment gates pass, all supplied counts independently
reconcile, and the rejected H1/H2 aggregate outcomes are valid completed
experiment results. Remaining work is Owner-only Git/PR/CI review and the
acceptance decision. S15 implementation has not started; the bounded planning
handoff is in [`S15Handoff.md`](S15Handoff.md).
