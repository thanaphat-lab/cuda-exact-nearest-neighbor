# CUDA Exact Nearest Neighbor

This project is a CUDA performance-engineering study of exhaustive exact nearest-neighbor search. The workload computes squared Euclidean distance (L2²) from a query to every candidate and returns the smallest index when the evaluated computation produces equal distances.

The primary workload is a single query. Batch-query evaluation is secondary and must remain separately identifiable.

## Numerical and optimization scope

- CPU and GPU production computation use Float32.
- A Float64 path is the numerical reference and diagnostic lane, not a production substitute.
- V0 is the naive CUDA baseline.
- V1 focuses on coalesced memory access.
- V2 focuses on block-level reduction.
- V3 focuses on warp-level or otherwise optimized reduction.
- V4 is optional stretch work for batch optimization and data reuse; it cannot replace V0–V3 evidence.

The exact distance, input, exhaustive-search, output, and smallest-index tie contracts are common across V0–V3. Numerical acceptance policies that remain unresolved in the decision register must not be inferred by an implementation or experiment.

## Project status

The repository is at the local-infrastructure milestone. It contains approved documentation, governance automation, and an infrastructure-only CMake project, but no CPU or CUDA implementation, build targets, tests, benchmark results, or verified GPU-support claim.

Build instructions are not yet available. The current [CMake project](CMakeLists.txt) only establishes the future C++17 build boundary and intentionally creates no executable, library, test, or CUDA target.

## Documentation

- [Canonical project specification](docs/specifications/FinalProjectSpecification-v1.0.md)
- [Source-of-truth policy](docs/governance/SourceOfTruth.md)
- [Decision register](docs/governance/DecisionRegister.md)
- [Acceptance criteria](docs/requirements/AcceptanceCriteria.md)
- [Requirements traceability](docs/requirements/RequirementsTraceability.md)
- [Architecture](docs/design/Architecture.md)
- [Correctness plan](docs/verification/CorrectnessPlan.md)
- [Benchmark protocol](docs/performance/BenchmarkProtocol.md)
- [Experiment template](docs/experiments/ExperimentTemplate.md)

## Agent and skill system

[AGENTS.md](AGENTS.md) defines repository operating rules. Multi-stage work is coordinated through the [`cuda-dev-workflow`](.agents/skills/cuda-dev-workflow/SKILL.md) skill, which routes bounded work to the nine repository specialists. Skills govern workflow and evidence discipline; they do not redefine the project specification.

## Reproducibility principles

Future correctness, profiling, and benchmark evidence must be tied to a revision and working-tree state and must retain exact commands, configuration, seeded inputs, environment details, and raw or traceable output. Single-query and batch evidence, Float32 production lanes and the Float64 reference, kernel-only and end-to-end timings, and V0–V3 versus optional V4 results remain separate. Missing measurements or environment facts stay explicit; they are never estimated.
