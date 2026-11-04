# S8 Evidence Index

## Identity

| Field | Value |
|---|---|
| Experiment | `S8-TRANSPOSE-AB-20261104-01` |
| Measured revision | `958677033f48d3d6543c03ed36dfd5cb10e16c59` |
| Recorded worktree | `clean` |
| Workload | Primary single query; 12 `(N,D)` cases |
| Configuration SHA-256 | `5c1a9638067370e636ad2d9d5f77582670f793780bc0c413226902f85cbcb03a` |
| Packet integrity | [`evidence/PACKET_MANIFEST.sha256`](evidence/PACKET_MANIFEST.sha256) |

The tuning and final directories are byte-for-byte copies of the completed G5 output. Raw measurements were not filtered, regenerated, or reformatted.

## Evidence map

| Packet path | Contents |
|---|---|
| [`evidence/tuning/`](evidence/tuning/) | 108,000 raw tuning samples, 1,080 summaries, frozen transpose selections, manifests, commands, configuration, environment, and telemetry |
| [`evidence/final/`](evidence/final/) | 145,200 raw final samples, 1,452 summaries, 216 comparisons, manifests, commands, configuration, environment, and telemetry |
| [`evidence/correctness/correctness-gate.json`](evidence/correctness/correctness-gate.json) | Original G5 correctness receipt: 18 passed, 0 failed; 60 required and 24 frozen matrix cases accepted unambiguously |
| [`evidence/correctness/source_identity.txt`](evidence/correctness/source_identity.txt) | Clean revision, source-manifest hash, and 52 source-file hashes |
| [`evidence/correctness/ctest-last-test.log`](evidence/correctness/ctest-last-test.log) | Existing G4 execution log; no checks were rerun for packaging |
| [`evidence/correctness/cpu/`](evidence/correctness/cpu/) | CPU Float32/Float64-reference fixture, evidence, and matrix manifests |
| [`evidence/correctness/cuda-v0/`](evidence/correctness/cuda-v0/) through [`cuda-v3/`](evidence/correctness/cuda-v3/) | Functional, binary-correlation, and required-matrix receipts for core CUDA variants |
| [`evidence/correctness/frozen/`](evidence/correctness/frozen/) | Frozen V2/V3 matrix receipts, 12 accepted-unambiguous cases each |
| [`evidence/correctness/portable-reference.json`](evidence/correctness/portable-reference.json) | Mapping from the original ignored G4 evidence root to this portable compact packet |

## Source mapping

- G5 source: `build-s8-g5-9586770/{tuning,final}` and `build-s8-g5-9586770/correctness-gate.json`.
- G4 source: `build-s8-g4-clean-9586770`, limited to compact correctness receipts and source identity.
- The tracked experiment configuration remains [`benchmarks/config/s8-transpose-ab-20261104-01.json`](../../../benchmarks/config/s8-transpose-ab-20261104-01.json); identical copies are retained under both tuning and final evidence.
- S7 accepted evidence remains authoritative and unchanged. S8 manifests reference its frozen block mapping at `docs/experiments/S7-ACCEPT-20261103-01/evidence/final/tuning-freeze.csv`.

## Integrity and completeness

`PACKET_MANIFEST.sha256` covers every retained file under `evidence/` except the manifest itself. Verification recalculates every listed digest and checks that no unlisted evidence file exists.

The packet intentionally omits approximately 1.86 GB of G4 candidate binaries, per-case candidate payloads, and diagnostic disassembly. Their compact matrix verdicts, binary manifests, source identity, and original CTest log are retained. The omission prevents candidate-level replay from this Git packet but does not alter the preserved correctness receipt or S8 measurement evidence.

See [`FinalExperimentReport.md`](FinalExperimentReport.md) for methodology, classifications, limitations, and the final evidence-bounded outcome.
