# GPU-Accelerated Exact Nearest Neighbor Search Using CUDA

This repository implements and evaluates exhaustive exact nearest-neighbor search for a primary single-query workload. It computes squared Euclidean distance (L2²) for every candidate and returns the lexicographic minimum of `(computed Float32 distance, index)`, so equal computed distances select the smallest index.

The accepted S7 experiment is documented in the [final experiment report](docs/experiments/S7-ACCEPT-20261103-01/FinalExperimentReport.md). Its compact, reviewable artifacts are indexed in the accompanying [evidence index](docs/experiments/S7-ACCEPT-20261103-01/EVIDENCE_INDEX.md).

## Implemented variants

- **CPU Float32 baseline:** sequential exhaustive search using the approved production arithmetic graph.
- **CUDA V0:** AoS candidate data, one thread per candidate, and a serial device reduction.
- **CUDA V1:** SoA candidate data for coalesced access while retaining the serial reducer.
- **CUDA V2:** SoA distance computation, shared-memory block reduction, and pair-only grid reduction stages.
- **CUDA V3:** SoA distance computation, warp-shuffle/block reduction, and pair-only grid reduction stages under the approved warp-participation rules.

V0–V3 preserve the same distance, input, exhaustive-search, output, and smallest-index tie contracts. CPU and GPU production computation uses Float32. The Float64 implementation is a numerical reference and diagnostic path, not a production substitute. V4 remains optional and was not evaluated in the accepted S7 experiment.

## Build and test

The repository requires CMake 3.21 or later, a C++17/MSVC toolchain, the CUDA Toolkit, an NVIDIA GPU compatible with the explicitly selected CUDA architecture, Ninja for the commands below, and Node.js for benchmark analysis. Core acceptance builds require `Release` and disable fast math.

Configure with the architecture observed for the target GPU; do not copy `86` to unrelated hardware merely because it was used for the accepted RTX 3060 Laptop GPU run.

```powershell
$TargetCudaArchitecture = '86' # Replace with the architecture observed for the target GPU.
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_CUDA_ARCHITECTURES=$TargetCudaArchitecture"
cmake --build build
ctest --test-dir build --output-on-failure
```

The checked-in CMake project defines the CPU baseline/evidence targets, CUDA V0–V3 libraries and functional/matrix tests, the `s7_benchmark` executable, benchmark integrity tests, and frozen-matrix checks. Acceptance configuration requires a clean worktree. `S7_ALLOW_DIRTY_LOCAL_BUILD=ON` is only for explicitly labeled local development evidence and cannot produce clean acceptance evidence.

## Benchmark workflow

The checked-in orchestration script validates revision identity, clean-tree state, approved numerical-policy hash, Release/architecture/fast-math configuration, GPU identity, environment approval, and telemetry before it performs candidate qualification, independent tuning, correctness after freeze, and final measurement.

```powershell
$BenchmarkBuild = 'build-s7'
$BenchmarkOutput = 'build-s7/s7-run-unique-id'
$ExpectedRevision = '0123456789abcdef0123456789abcdef01234567'
$EnvironmentApproval = 'D:\path\to\owner-environment-approval.json'
& scripts/run-s7-benchmarks.ps1 `
  -BuildDirectory $BenchmarkBuild `
  -OutputDirectory $BenchmarkOutput `
  -ExperimentConfig benchmarks/config/s7-accept-20261103-01.json `
  -ExpectedRevision $ExpectedRevision `
  -EnvironmentApproval $EnvironmentApproval
```

The output directory must be new; accepted evidence must never be overwritten. Analyze an existing result directory with:

```powershell
$ResultDirectory = 'build-s7/s7-run-unique-id/final'
$RunRoot = 'build-s7/s7-run-unique-id'
node scripts/analyze-s7-benchmarks.mjs $ResultDirectory --shared-root $RunRoot
```

The accepted experiment used 10 warm-ups and 100 retained measurements per configuration across the required 12-case matrix, with independent tuning and final seeds. `BND-KERNEL` excludes allocation, conversion, and transfers; primary `BND-E2E-RAW` includes validation, per-call preprocessing/allocation, transfers, the complete kernel sequence, synchronization, and cleanup. These distributions must not be compared as if they represented the same boundary.

## Accepted result

Experiment `S7-ACCEPT-20261103-01` is bound to revision `e6bf8f39d7807cc0eecf68956e92a599332ef4ff` and a clean worktree. Its final acceptance is `accepted`; H1, H2, and H4 were rejected, while H3 was supported in all 12 required cases. This does not imply that V3 is universally fastest: raw end-to-end GPU latency was slower than the CPU baseline in every H1 case, and V3 did not universally improve on V2 kernel latency.

The full 257-file evidence set is preserved outside Git as an immutable ZIP. The repository intentionally contains only the compact evidence needed for review; bulky raw samples, candidate binaries, telemetry logs, and build products remain in the external archive identified by the [evidence index](docs/experiments/S7-ACCEPT-20261103-01/EVIDENCE_INDEX.md).

## Documentation

- [Final experiment report](docs/experiments/S7-ACCEPT-20261103-01/FinalExperimentReport.md)
- [Compact evidence index](docs/experiments/S7-ACCEPT-20261103-01/EVIDENCE_INDEX.md)
- [Canonical project specification](docs/specifications/FinalProjectSpecification-v1.0.md)
- [Approved numerical policy](docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md)
- [Approved implementation design](docs/design/S6ApprovedImplementationDesign-v1.0.md)
- [Acceptance criteria](docs/requirements/AcceptanceCriteria.md)
- [Correctness plan](docs/verification/CorrectnessPlan.md)
- [Benchmark protocol](docs/performance/BenchmarkProtocol.md)
- [Development and merge policy](docs/governance/DevelopmentAndMergePolicy.md)

Git and GitHub mutations remain Owner-only. Normal integration uses a focused branch, applicable CI, human review, and squash merge; direct pushes to `main` remain prohibited.
