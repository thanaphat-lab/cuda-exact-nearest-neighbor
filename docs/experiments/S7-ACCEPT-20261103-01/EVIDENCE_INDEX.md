# S7 Accepted Evidence Index

## Identity and preservation status

| Field | Value |
|---|---|
| Experiment | `S7-ACCEPT-20261103-01` |
| Accepted-run directory | `build-s7-accept-20261103-01-sm86/s7-accept-20261103-01-retry1` |
| Revision | `e6bf8f39d7807cc0eecf68956e92a599332ef4ff` |
| Recorded worktree | `clean` |
| Acceptance authority | [`evidence/final/acceptance.json`](evidence/final/acceptance.json): `performance_acceptance=accepted` |
| Full archive | `D:\ku\s7-evidence-archive\S7-ACCEPT-20261103-01-e6bf8f39-evidence.zip` |
| Archive SHA-256 | `be6f5d89a9115a961b5eedd2016e59a136b0d2a8ce14ab0941e8b02a990f4677` |
| Archive size | `348864826` bytes |
| Full evidence verification | `PASSED`: 257 source files / 257 ZIP files; relative paths and per-file SHA-256 matched 257/257 |
| Verification receipt | [`evidence/archive/verification-receipt.txt`](evidence/archive/verification-receipt.txt) |
| Compact packet manifest | [`evidence/PACKET_MANIFEST.sha256`](evidence/PACKET_MANIFEST.sha256), 48 source-derived entries |

The archive and original accepted run are immutable evidence. Files in this compact packet are byte-for-byte copies selected for review and submission; original evidence was not edited. Bulky evidence omitted from Git remains recoverable through the archive.

## Source-to-submission mapping

All source paths below are relative to the accepted-run directory. Submission paths are relative to this index.

| Source | Submission copy | Description |
|---|---|---|
| `final/acceptance.json` | `evidence/final/acceptance.json` | Final AC-014/AC-015 and performance-acceptance authority |
| `final/summary.{csv,json}` | `evidence/final/summary.{csv,json}` | 444 summarized configurations with median, p5, p95, and bootstrap median interval |
| `final/comparisons.{csv,json}` | `evidence/final/comparisons.{csv,json}` | 84 compatible ratio comparisons and confidence intervals |
| `final/hypothesis-outcomes.{csv,json}` | `evidence/final/hypothesis-outcomes.{csv,json}` | H1–H4 case classifications and aggregate outcomes |
| `final/experiment-config.json` | `evidence/final/experiment-config.json` | Predeclared workload, sampling, ratio, hypothesis, and environment policy |
| `final/manifest.json` | `evidence/final/manifest.json` | Final run identity, seeds, timing boundary, architecture, and build flags |
| `final/environment.json` | `evidence/final/environment.json` | Runtime environment emitted by the benchmark |
| `final/commands.txt` | `evidence/final/commands.txt` | Exact final benchmark command |
| `final/integrity.json` | `evidence/final/integrity.json` | Final result hashes; status `passed`, 44,400 raw rows and 444 summary rows |
| `final/correctness-gate.json` | `evidence/final/correctness-gate.json` | Correctness prerequisite copied into the final result |
| `final/tuning-freeze.{csv,json}` | `evidence/final/tuning-freeze.{csv,json}` | Frozen V2/V3 block mapping and the disclosed JSON metadata defect |
| `final/plots/*.svg` | `evidence/final/plots/*.svg` | Compatible ratios, kernel latency, raw end-to-end latency, and stage attribution |
| `tuning/summary.{csv,json}` | `evidence/tuning/summary.{csv,json}` | Independent tuning summaries; raw tuning samples remain in the archive |
| `tuning/tuning-freeze.{csv,json}` | `evidence/tuning/tuning-freeze.{csv,json}` | Tuning freeze products; CSV is the mapping consumed by correctness/final execution |
| `tuning/manifest.json` | `evidence/tuning/manifest.json` | Tuning role, seed, configuration, and timing metadata |
| `tuning/commands.txt` | `evidence/tuning/commands.txt` | Exact tuning command |
| `tuning/integrity.json` | `evidence/tuning/integrity.json` | Tuning result hashes; status `passed`, 57,600 raw rows and 576 summary rows |
| `tuning/plots/*.svg` | `evidence/tuning/plots/*.svg` | Compact tuning visualizations |
| `environment-capture/{run-metadata,host-environment,environment-approval,experiment-config}.json` | `evidence/environment-capture/` | Revision, toolchain, hardware, approval, and predeclared configuration provenance |
| `environment-capture/{commands.jsonl,orchestration-command.txt}` | `evidence/environment-capture/` | Executed stage commands and top-level invocation |
| `correctness-after-freeze/correctness-gate.json` | `evidence/correctness-after-freeze/correctness-gate.json` | `passed-clean-candidate` gate bound to revision/configuration |
| `correctness-after-freeze/cuda-v{2,3}-matrix.txt` | `evidence/correctness-after-freeze/` | Twelve accepted-unambiguous cases per frozen variant |
| `candidate-qualification/block-{128,256,512}.csv` | `evidence/candidate-qualification/` | Candidate block mappings used for qualification |
| `candidate-qualification/v{2,3}-block-{128,256,512}-matrix.txt` | `evidence/candidate-qualification/` | Correctness qualification for all approved block candidates |
| External verification receipt | `evidence/archive/verification-receipt.txt` | Full archive digest, size, file counts, path-set verdict, and 257 per-file hashes |

## Intentionally external bulky evidence

The following files are mandatory parts of the full evidence record but are intentionally not duplicated in Git:

- `final/raw_samples.csv` (SHA-256 `3c103983ad7ab6e328a3fb82dcc955298fc7f3dfda8c7b623ce8863eaa07d2cd`);
- `tuning/raw_samples.csv` (SHA-256 `a1cbebf24e41f4629556605975978dd2c5bf835ad4f38c1b5353efce168138a4`);
- every `*.candidates.bin` correctness/candidate-qualification payload;
- per-case candidate metadata not selected for the compact packet;
- `environment-capture/telemetry/*.jsonl`, NVIDIA XML snapshots, `CMakeCache.txt`, and `build.ninja`.

Their original relative paths and SHA-256 values are recorded in the external verification receipt or the final/tuning integrity manifests. The full archive, not the compact packet, is the preservation authority for these files.

## Integrity and known metadata inconsistencies

- Recalculation before packaging matched all 32 hashes declared by `final/integrity.json` and all 30 hashes declared by `tuning/integrity.json`.
- The archive was independently verified against the accepted-run directory by file count, exact relative path set, and SHA-256 for every file.
- `final/tuning-freeze.json` records `tuning_median_ns=0` for all 24 mappings, while the tuning summaries contain nonzero medians. The CSV freeze mapping is correct and was consumed by correctness-after-freeze and final execution. The JSON is preserved unchanged and must not be cited as a source of tuning median values.
- `final/manifest.json` records `performance_acceptance=pending-complete-evidence`, reflecting its pre-analysis generation phase. The later `final/acceptance.json` is the final acceptance authority and records `accepted`.
- The compact packet deliberately omits raw/binary bulk; a reviewer without access to the external archive cannot independently replay every raw-sample or candidate-level check from the compact packet alone.

## Independent-backup limitation

The verified archive exists at the absolute path above. A second independent physical or remote backup was not created by this packaging task and remains an Owner action before submission.
