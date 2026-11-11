# S14 Batch V4 Evidence Index

## Identity and preservation status

| Field | Value |
|---|---|
| Experiment | `S14-BATCH-V4-20261111-02` |
| Workload | Secondary batch-query evaluation; optional V4 |
| Original measurement revision | `00e3207cc020a99f88b927865c213b7100cfb5de` |
| Original run directory | `build-s14-fix-clean-00e3207-sm86/s14-final-02` |
| Recovered-analysis revision | `d9fd6613a9e089300bb92bf7fef407ccdbeec682` |
| Recovery directory | `build-s14-fix-clean-00e3207-sm86/s14-recovery-d9fd661-01` |
| Recovery classification | `analysis-only`; no performance rerun |
| Original artifact identity | 30 files; 0 SHA-256 mismatches in the recovery copy |
| Repository packet | 36 source-derived files plus [`PACKET_MANIFEST.sha256`](evidence/PACKET_MANIFEST.sha256) |
| Environment classification | `accepted-for-analysis` |
| Overall H1 outcome | `rejected` under `optional-all-36` |
| Overall H2 outcome | `rejected` under `optional-all-36` |

The repository packet is a byte-for-byte copy of the recovered-analysis
directory. It contains all 30 original artifacts and six additions: the
recovery provenance record and five recovered analysis products. The original
measurements were not modified. The preservation authority for the relationship
between the two revisions is
[`recovery-provenance.json`](evidence/recovery-provenance.json).

## Source-to-submission mapping

All source paths below are relative to the recovered-analysis directory. Packet
paths are relative to this index.

| Source | Packet copy | Purpose |
|---|---|---|
| `recovery-provenance.json` | [`evidence/recovery-provenance.json`](evidence/recovery-provenance.json) | Binds the unchanged original artifacts to the analyzer revision and records that no performance rerun occurred |
| `correctness/*` | [`evidence/correctness/`](evidence/correctness/) | Clean-revision correctness gate and 36-configuration distinct-query matrix |
| `environment-capture/*` | [`evidence/environment-capture/`](evidence/environment-capture/) | Approved design, configuration, build, command, prerequisite, lifecycle, GPU, and telemetry evidence |
| `final/raw_samples.csv` | [`evidence/final/raw_samples.csv`](evidence/final/raw_samples.csv) | All 21,600 retained measurements |
| `final/manifest.json` | [`evidence/final/manifest.json`](evidence/final/manifest.json) | Benchmark revision, clean-tree state, workload, build, hardware, and timing-policy identity |
| `final/correctness-gate.json` | [`evidence/final/correctness-gate.json`](evidence/final/correctness-gate.json) | Correctness prerequisite copied into the measured result |
| `final/environment-status.json` | [`evidence/final/environment-status.json`](evidence/final/environment-status.json) | Recovered environment classification and telemetry audit |
| `final/summary.{csv,json}` | [`evidence/final/`](evidence/final/) | 216 distributions, each with 100 retained samples |
| `final/comparisons.json` | [`evidence/final/comparisons.json`](evidence/final/comparisons.json) | 72 H1/H2 per-configuration comparisons and two aggregate outcomes |
| `final/break-even.json` | [`evidence/final/break-even.json`](evidence/final/break-even.json) | 72 construction-plus-batch amortization results |

## Audit results

- The 30 original files and their 30 recovery copies have identical relative
  paths and SHA-256 values.
- `raw_samples.csv` contains 21,600 completed rows grouped into 216 unique
  distributions with exactly 100 rows per distribution.
- The packet contains 216 summaries, 72 comparisons, and 72 break-even rows.
- Correctness is `passed-clean-candidate`: all 36 batch configurations passed
  distinct-query, CPU batch, sequential Prepared V2, V4 batch, bitwise Float32
  equivalence, and Float64 checks; 384 Float64 query checks are recorded.
- Environment status is `accepted-for-analysis`. The maximum metric and process
  telemetry gap is 1,211 ms against a 3,000 ms limit; no unauthorized GPU
  process or hardware thermal slowdown sample is recorded.
- H1 has 35 supported and 1 rejected configuration. H2 has 28 supported, 2
  rejected, and 6 inconclusive configurations. Both universal hypotheses are
  therefore `rejected` under the unchanged `optional-all-36` rule.

## Provenance limitation

The original runner records an exit code of `1` for its analysis stage and
preserves the missing analysis artifacts at that point. The recovered products
were generated later by analyzer revision `d9fd6613...` from unchanged original
measurements. The recovery provenance record is retained, but an exact recovery
shell invocation was not captured in the packet; no command is reconstructed or
claimed as executed.

