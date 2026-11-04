# GPU-Accelerated Exact Nearest-Neighbor Search Using CUDA

This project implements and evaluates exhaustive exact nearest-neighbor search for a primary single-query workload. Given a query vector `q` and `N` candidate vectors `x_i`, each with `D` dimensions, every candidate is evaluated using squared Euclidean distance:

`d_i = Σ_j (x_i[j] - q[j])²`

The result is the lexicographic minimum `(computed Float32 distance, index)`, so equal computed distances select the smallest index. Search is exhaustive: no approximate index or candidate pruning is used.

CPU and GPU production paths use Float32. Float64 is retained only as an independent numerical reference and diagnostic path. Inputs containing NaN or infinity, and inputs with `N = 0` or `D = 0`, are rejected; signed zero is accepted. The full behavioral contract is defined by the [canonical project specification](docs/specifications/FinalProjectSpecification-v1.0.md) and [approved numerical policy](docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md).

## Implementations

| Variant | Data layout and reduction strategy |
|---|---|
| Sequential CPU | Row-major array of structures (AoS); candidates are visited sequentially using the approved ascending per-dimension Float32 arithmetic graph and lexicographic argmin. |
| CUDA V0 | AoS; one CUDA thread computes one candidate distance; a one-thread device kernel performs the final reduction. |
| CUDA V1 | Structure of arrays (SoA) for candidate-coalesced access; retains the V0 serial final reducer. |
| CUDA V2 | SoA; shared-memory block reduction followed by pair-only grid-reduction stages. |
| CUDA V3 | SoA; warp-shuffle and warp-leader shared-memory reduction followed by pair-only grid-reduction stages. |

V0–V3 preserve the same input, distance, exhaustive-search, output, and smallest-index tie contracts. V4 is optional stretch work and is not implemented or evaluated here. The implementation architecture is documented in the [approved S6 design](docs/design/S6ApprovedImplementationDesign-v1.0.md).

## Repository layout

- `include/s6/` and `src/` contain the CPU baseline, numerical-evidence support, and CUDA V0–V3 implementations.
- `tests/` contains CPU/CUDA functional tests, required-matrix tests, and frozen-configuration checks.
- `benchmarks/` contains the S7 and S8 benchmark executables and immutable experiment configurations.
- `scripts/` contains the checked-in PowerShell orchestration, telemetry, and Node.js analysis tools.
- `docs/` contains the specification, design, requirements, benchmark protocol, and revision-bound experiment reports/evidence.

## Requirements

The checked-in CMake configuration and accepted experiment environment require:

- 64-bit Windows and PowerShell for the repository benchmark orchestration;
- CMake 3.21 or later and Ninja;
- Git;
- a C++17 MSVC toolchain with `dumpbin.exe` (the current CPU evidence closure is MSVC-specific);
- the NVIDIA CUDA Toolkit and an NVIDIA GPU supported by an explicitly selected CUDA architecture;
- Node.js, required by the benchmark-analysis integrity tests and analysis scripts.

Core builds must use `Release`, explicitly set `CMAKE_CUDA_ARCHITECTURES`, and keep CUDA fast math disabled. The accepted S7/S8 machine used Windows 11, an Intel Core i7-11370H, an NVIDIA GeForce RTX 3060 Laptop GPU (compute capability 8.6), CUDA Toolkit 13.3, and MSVC 19.51. Those versions describe the measured environment, not universal minimum versions.

## Quick start: build, test, and demonstrate

Run the following from a clean checkout in a PowerShell environment where MSVC, CUDA, CMake, Ninja, Git, and Node.js are available. Set the architecture to the value appropriate for the installed GPU; `86` is shown only because it matches the recorded RTX 3060 Laptop GPU.

```powershell
$TargetCudaArchitecture = '86' # Replace for a different supported GPU.

cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_CUDA_ARCHITECTURES=$TargetCudaArchitecture"

cmake --build build
ctest --test-dir build --output-on-failure
```

This builds the CPU baseline, CUDA V0–V3, `s7_benchmark`, `s8_benchmark`, and their test/evidence targets. The CTest suite exercises CPU and CUDA functional behavior, the required correctness matrices, source identity, benchmark self-tests, analysis self-tests, an S7 dry run, and frozen V2/V3 configurations. The last revision-bound S8 correctness receipt recorded **18 passed and 0 failed**, including 60 required-matrix and 24 frozen-matrix cases; see the [S8 report](docs/experiments/S8-TRANSPOSE-AB-20261104-01/FinalExperimentReport.md).

The commands above are the practical demonstration path. Do not treat them as a reproduction of the accepted performance measurements: accepted runs additionally require a clean exact revision, an Owner-supplied environment approval, telemetry, frozen tuning data, and new output directories. The exact executed S7/S8 commands and artifacts are linked from the experiment reports and evidence indexes below. No new benchmark run is necessary to inspect the committed results.

## Datasets and reproducibility

The measured S7 and S8 workloads are **deterministic synthetic datasets**, not an external real-world dataset. The benchmark generator is identified as `S7-DETERMINISTIC-SYNTHETIC-v1` and is implemented in the checked-in benchmark sources. It constructs Float32 query and row-major AoS candidate values deterministically from `(N, D, seed)` using bounded arithmetic; no downloaded data or nondeterministic random source is involved.

The required single-query matrix is the Cartesian product:

- `N ∈ {1,024, 16,384, 262,144, 1,048,576}`
- `D ∈ {8, 32, 128}`

This gives 12 workload sizes. Both experiments used distinct recorded seeds:

| Role | Seed |
|---|---:|
| Independent tuning | `11820040416388919760` |
| Final measurement | `589684135938649225` |

Each reported configuration used 10 warm-ups followed by 100 retained measurements. Final measurements were not reused for tuning. The experiment packets record revision and tree state, configuration hashes, environment, commands, workload parameters, timing boundaries, samples or traceable summaries, and correctness prerequisites.

**Submission gap:** a required external public dataset has not yet been demonstrated. The committed performance conclusions therefore apply only to the deterministic synthetic workloads above and must not be generalized to real-world data distributions.

## Timing boundaries

The reports keep application-visible and device-only measurements separate:

- **CPU-E2E-RAW:** the sequential CPU one-shot call from entry before validation through synchronous result return.
- **BND-E2E-RAW (GPU Raw E2E):** validation; per-call preprocessing and allocation; AoS-to-SoA conversion for V1–V3; host-to-device transfers; the complete production kernel sequence; device-to-host transfer; synchronization; cleanup; and host result return. Dataset generation and reporting are excluded.
- **BND-KERNEL:** the complete production kernel sequence with device-resident inputs, outputs, and workspace, timed with CUDA events. Allocation, validation, layout conversion, transfers, reference work, and reporting are excluded.

Kernel-only timing is useful for comparing kernel designs, but it is **not full end-to-end speedup** and must not be presented as such.

## S7 accepted experiment

[`S7-ACCEPT-20261103-01`](docs/experiments/S7-ACCEPT-20261103-01/FinalExperimentReport.md) is bound to clean revision `e6bf8f39d7807cc0eecf68956e92a599332ef4ff`. Correctness after tuning freeze passed, and final performance acceptance was `accepted`. S7 evaluated all 12 synthetic cases on the recorded RTX 3060 Laptop GPU with CPU Float32 and CUDA V0–V3.

The following table reports the S7 median CPU and V3 **Raw E2E** latency. It is an application-visible comparison at compatible boundaries, not a kernel-only comparison.

| N | D | CPU Raw E2E (ms) | V3 GPU Raw E2E (ms) | CPU/V3 ratio |
|---:|---:|---:|---:|---:|
| 1,024 | 8 | 0.095100 | 0.593650 | 0.160195 |
| 1,024 | 32 | 0.173500 | 1.209750 | 0.143418 |
| 1,024 | 128 | 0.990300 | 1.370250 | 0.722715 |
| 16,384 | 8 | 0.792800 | 0.992100 | 0.799113 |
| 16,384 | 32 | 2.477150 | 9.721600 | 0.254809 |
| 16,384 | 128 | 16.558100 | 28.355200 | 0.583952 |
| 262,144 | 8 | 12.124800 | 22.784200 | 0.532157 |
| 262,144 | 32 | 41.228400 | 133.832000 | 0.308060 |
| 262,144 | 128 | 159.760000 | 429.601000 | 0.371880 |
| 1,048,576 | 8 | 34.987200 | 71.237600 | 0.491133 |
| 1,048,576 | 32 | 131.836000 | 430.270000 | 0.306402 |
| 1,048,576 | 128 | 555.461000 | 1,966.090000 | 0.282520 |

The predeclared outcomes were:

- H1, V3 GPU Raw E2E faster than CPU Raw E2E in every case: **rejected in all 12 cases**.
- H2, V1 kernel faster than V0 kernel in every case: **rejected**.
- H3, V2 kernel faster than V1 kernel in every case: **supported in all 12 cases**, with recorded median kernel ratios from `2.233010×` to `84.286041×`.
- H4, V3 kernel faster than V2 kernel in every case: **rejected**; V3 was not a universal incremental kernel improvement.

Full medians, p5/p95 values, bootstrap intervals, plots, stage attribution, and known evidence limitations are in the [S7 final report](docs/experiments/S7-ACCEPT-20261103-01/FinalExperimentReport.md) and [S7 evidence index](docs/experiments/S7-ACCEPT-20261103-01/EVIDENCE_INDEX.md).

## S8 cache-blocked transpose experiment

[`S8-TRANSPOSE-AB-20261104-01`](docs/experiments/S8-TRANSPOSE-AB-20261104-01/FinalExperimentReport.md) is bound to clean measured revision `958677033f48d3d6543c03ed36dfd5cb10e16c59`. It compared the original host AoS-to-SoA conversion with cache-blocked `32×32`, `64×32`, and `64×64` candidates for CUDA V1–V3. Candidate selection used the host-transpose median; final measurement retained the original and every candidate.

| Recorded S8 comparison | Supported | Rejected | Inconclusive |
|---|---:|---:|---:|
| Selected cache-blocked vs original host transpose | 18 | 18 | 0 |
| Selected cache-blocked vs original GPU Raw E2E | 18 | 8 | 10 |
| CPU baseline vs all recorded GPU Raw E2E combinations | 0 | 144 | 0 |

S8 therefore shows that cache blocking **improved GPU Raw E2E latency in some cases relative to the original transpose**, but the benefit was workload-dependent. Selected-candidate GPU Raw E2E ratios ranged from `0.775711` to `1.417185`. Most importantly, GPU Raw E2E **did not outperform the sequential CPU baseline in any recorded S8 comparison**: all 144 CPU/GPU Raw E2E comparisons were rejected under the experiment's ratio rule, with CPU/GPU ratios from `0.043949` to `0.601477`.

See the [S8 final report](docs/experiments/S8-TRANSPOSE-AB-20261104-01/FinalExperimentReport.md) and [S8 evidence index](docs/experiments/S8-TRANSPOSE-AB-20261104-01/EVIDENCE_INDEX.md) for exact commands, complete comparison records, correctness provenance, environment capture, telemetry, and packet limitations. S7 accepted evidence remains historical and was not overwritten or reinterpreted by S8.

## Limitations and future improvements

- Measurements cover deterministic synthetic, single-query workloads only; the required external public dataset remains pending.
- Results come from one Windows laptop environment and one RTX 3060 Laptop GPU. Hardware-specific block choices and performance are not universal constants.
- Raw one-shot GPU calls repeatedly allocate, transpose, transfer, synchronize, and clean up. Future separately evaluated APIs could reuse prepared/device-resident datasets and persistent workspaces.
- Batch-query evaluation is secondary and has not been reported. It may better amortize preprocessing and transfer costs, but requires separate correctness and performance evidence.
- V4 remains optional and unevaluated.
- S7's full 257-file evidence archive is stored outside Git; the compact committed packet is reviewable but omits bulky raw samples, binaries, and telemetry. A second independent backup remains an Owner action.
- S8's compact correctness packet cannot independently replay omitted candidate-level binary/disassembly checks, although it preserves the original correctness receipt and matrix verdicts.

## Presentation file

**Status: pending.** No PowerPoint (`.ppt` or `.pptx`) or other presentation file is committed in this repository at the current revision. Add and review the presentation through the Owner-controlled Git workflow before submission; no placeholder link is provided here.

## Documentation and evidence

- [S7 final experiment report](docs/experiments/S7-ACCEPT-20261103-01/FinalExperimentReport.md)
- [S7 evidence index](docs/experiments/S7-ACCEPT-20261103-01/EVIDENCE_INDEX.md)
- [S8 final experiment report](docs/experiments/S8-TRANSPOSE-AB-20261104-01/FinalExperimentReport.md)
- [S8 evidence index](docs/experiments/S8-TRANSPOSE-AB-20261104-01/EVIDENCE_INDEX.md)
- [Canonical project specification](docs/specifications/FinalProjectSpecification-v1.0.md)
- [Approved numerical policy](docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md)
- [Approved implementation design](docs/design/S6ApprovedImplementationDesign-v1.0.md)
- [Acceptance criteria](docs/requirements/AcceptanceCriteria.md)
- [Requirements traceability](docs/requirements/RequirementsTraceability.md)
- [Correctness plan](docs/verification/CorrectnessPlan.md)
- [Benchmark protocol](docs/performance/BenchmarkProtocol.md)

Git and GitHub mutations are Owner-only. This repository's normal integration path uses a focused branch, applicable CI, human review, and squash merge; direct pushes to `main` are prohibited by the [development and merge policy](docs/governance/DevelopmentAndMergePolicy.md).
