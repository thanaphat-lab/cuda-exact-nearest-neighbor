# Final Experiment Report: S8 Cache-Blocked Transpose A/B Evaluation

## 1. Identity and question

| Field | Observed value |
|---|---|
| Experiment | `S8-TRANSPOSE-AB-20261104-01` |
| Revision | `958677033f48d3d6543c03ed36dfd5cb10e16c59` |
| Recorded tree | `clean` |
| Scope | Primary single-query Float32 CPU/CUDA V1-V3 evaluation |
| Tuning seed | `11820040416388919760` |
| Final seed | `589684135938649225` |
| Matrix | `N in {1024,16384,262144,1048576}` x `D in {8,32,128}` |
| Sampling | 10 warm-ups; 100 retained measurements per distribution |
| Fast math | Disabled |

S8 evaluated whether a cache-blocked host AoS-to-SoA transpose improved the original implementation. Tuning selected among `32x32`, `64x32`, and `64x64` using the `BND-HOST-TRANSPOSE` median. Final measurement retained the original and all candidates, so both the selected comparison and the broader CPU/GPU raw end-to-end context remain auditable.

The recorded ratio is `median(original) / median(candidate)`. A comparison is `supported` when its conservative interval is wholly above 1, `rejected` when wholly below 1, and `inconclusive` when it includes 1.

## 2. Correctness prerequisite

The existing G4 correctness receipt reports `passed-clean-candidate` for the same revision and configuration hash:

| Check | Existing result |
|---|---|
| CTest | 18 passed, 0 failed |
| Required matrix | 60 accepted-unambiguous cases |
| Frozen V2/V3 matrix | 24 accepted-unambiguous cases |
| Selector implementations | `original`, `32x32`, `64x32`, `64x64` |
| Source identity | 52/52 recorded source hashes matched during packaging review |

CPU/GPU production computation remains Float32. The retained correctness manifests keep the independent Float32 and Float64-reference checks, argmin verdicts, distance evidence, and ambiguity classifications distinct. No correctness check was rerun during packaging.

The original gate's ignored `build-s8-g4-clean-9586770/evidence` reference is resolved by [`portable-reference.json`](evidence/correctness/portable-reference.json), which maps it to the retained compact correctness tree while preserving the original gate byte-for-byte.

## 3. Measurement evidence

| Stage | Raw rows | Distributions | Samples per distribution |
|---|---:|---:|---:|
| Tuning | 108,000 | 1,080 | 100 |
| Final | 145,200 | 1,452 | 100 |

All retained rows record `completed`, `warmup=false`, the expected revision, and a clean tree. Recalculated median, p5, and p95 values matched every summary row. CSV and JSON summaries are semantically identical. All 36 frozen selections match the minimum tuning median with the predeclared candidate order.

Exact recorded invocations are retained in [`tuning/commands.txt`](evidence/tuning/commands.txt) and [`final/commands.txt`](evidence/final/commands.txt). Raw values are preserved in [`tuning/raw_samples.csv`](evidence/tuning/raw_samples.csv) and [`final/raw_samples.csv`](evidence/final/raw_samples.csv).

## 4. Classifications

All 216 comparison records recomputed from the final summaries with no ratio, interval, candidate, or outcome mismatch.

| Comparison boundary | Supported | Rejected | Inconclusive | Total |
|---|---:|---:|---:|---:|
| Host transpose (`BND-HOST-TRANSPOSE`) | 18 | 18 | 0 | 36 |
| GPU raw end-to-end (`BND-E2E-RAW`) | 18 | 8 | 10 | 36 |
| CPU/GPU raw end-to-end | 0 | 144 | 0 | 144 |

The host-transpose ratios ranged from `0.227273` to `2.138033`; selected-candidate GPU raw end-to-end ratios ranged from `0.775711` to `1.417185`. CPU/GPU ratios ranged from `0.043949` to `0.601477` and all 144 were classified `rejected` under the recorded CPU/GPU ratio rule.

These are case classifications for the recorded machine, workload, implementations, and timing boundaries. They do not establish a universal performance claim. The evidence supports improvements in some cases and rejects or leaves them inconclusive in others.

## 5. Environment

Both stages report an NVIDIA GeForce RTX 3060 Laptop GPU, compute capability 8.6, 6,441,926,656 bytes of GPU memory, driver/runtime value `13040`, Release build, and fast math disabled. Host capture records Windows `10.0.26300.0`, Intel Family 6 Model 140, and PowerShell 7.6.9.

The packet retains 4,442 tuning and 6,489 final GPU telemetry records plus process telemetry. No telemetry error was recorded, hardware thermal slowdown remained `Not Active`, and the only observed GPU process during measurement was the benchmark executable. The configuration defines no approved numeric temperature, clock, or power rejection threshold, so none was invented during analysis.

## 6. Integrity and preservation

- The configuration SHA-256 is `5c1a9638067370e636ad2d9d5f77582670f793780bc0c413226902f85cbcb03a` in the repository, tuning copy, final copy, manifests, and host captures.
- Tuning raw SHA-256: `57e16a634a69d0ef0057e64f14040dbc585dd5772804d3a3506438d78ed33707`.
- Final raw SHA-256: `c89674d99b1bfd1edf392c53119b96e7077674ed08f57a7274d985e5a0ec8412`.
- Frozen-selection SHA-256: `48046727009120ef2a22df65944bbe89a0243f8c8a5d1866c31ad5c658a66b27`.
- Comparison SHA-256: `6e355f67e4abb51f1b62882a89d7858d073cddbae83b84c74aec0acd8a421550`.
- Original correctness-gate SHA-256: `440935d92720b1a38622237ef6f57b405bedf5e5c740b23707c0f2d493af8fea`.

[`PACKET_MANIFEST.sha256`](evidence/PACKET_MANIFEST.sha256) covers every retained evidence file except itself. S7 accepted evidence was not changed.

## 7. Limitations and outcome

- Results apply to one recorded laptop GPU/Windows environment and deterministic synthetic single-query workloads.
- Batch queries, V0 performance, V4, other hardware, and other data distributions were not evaluated by S8.
- The ratio interval combines separate bootstrap median intervals; it is not a paired-sample causal interval.
- The compact correctness packet omits bulky candidate binaries and disassembly. It preserves the original receipt, source identity, functional/binary manifests, matrix verdicts, and CTest log, but cannot replay candidate-level checks independently.
- Packaging performed no benchmark, tuning, build, or test execution.

The experiment is complete and internally consistent. Cache-blocked transpose benefit was workload-dependent: 18 of 36 selected host-transpose comparisons and 18 of 36 selected raw GPU end-to-end comparisons were supported. The remaining selected comparisons were rejected or inconclusive as recorded above. Submission preservation is complete once the Owner reviews and commits this packet and required PR CI passes on the resulting revision.
