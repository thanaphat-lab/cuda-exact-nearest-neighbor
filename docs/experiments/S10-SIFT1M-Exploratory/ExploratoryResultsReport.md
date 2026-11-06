# S10 SIFT1M Exploratory Results

**Status: EXPLORATORY / VALID-UNREVIEWED.** These results are not formal performance-acceptance evidence. No S10 `acceptance.json` was generated or inferred.

## Scope and identity

S10 evaluates the primary single-query workload on the public INRIA TexMex ANN_SIFT1M dataset. The measured performance packet is the existing, immutable external directory `S10-SIFT1M-PERFORMANCE-G10-20261106-01`, produced from clean revision `b5002e2c91cc288565a92fdd3a5e75d3dc3dddf4`. Revision `5711c824904b44d8a5ec3d834708202830687ada` verifies the committed analyzer fix; it did not rerun or replace the measurements.

The source files were downloaded from the recorded [TexMex corpus homepage](http://corpus-texmex.irisa.fr/) and archive URL `ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz`. Project policy prohibits redistributing the dataset bytes. The measured workload uses query ordinal 0, dimension 128, original Float32 AoS values without normalization, and candidate prefixes of 1,024, 16,384, 262,144, and 1,000,000 rows. Recorded SHA-256 identities are:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `sift_base.fvecs` | 516,000,000 | `21f66e2975057b5728ba56de1c825bac4f4d89d596609ae985741c6242631816` |
| `sift_query.fvecs` | 5,160,000 | `f7fc9be140accdfd64116c2fa2365ecdb69b8f084970c6b0532db5ff79ac8fdc` |
| G10 `raw_samples.csv` | external evidence | `4edeeaa9128646b5bdc6ec0ba4f713be0cdbdfd03f23abc5a6b0bca9740f6502` |

## Correctness prerequisite

The external G9 correctness gate is bound to the same measured revision, source manifest, configuration, dataset hashes, binary, and numerical-build identities as G10. It records **20/20 accepted**, zero failed, and zero blocked cases: CPU Float32 and CUDA V0–V3 for each of the four candidate counts, evaluated with the independent Float64 reference and the approved ambiguity classification. This supports exploratory interpretation only; it does not turn this report into formal performance acceptance.

## Existing performance results

The existing run used 10 warm-ups and 100 retained measurements per variant/boundary on an NVIDIA GeForce RTX 3060 Laptop GPU, CUDA architecture 86, Release build, strict floating-point settings, and fast math disabled. Values below are medians in milliseconds. CPU and GPU Raw E2E are application-visible boundaries; kernel-only excludes validation, layout conversion, allocation, transfers, synchronization outside the kernel sequence, and cleanup.

| N | CPU Raw E2E | V0 GPU Raw E2E | V1 GPU Raw E2E | V2 GPU Raw E2E | V3 GPU Raw E2E |
|---:|---:|---:|---:|---:|---:|
| 1,024 | 0.543800 | 1.160450 | 1.563050 | 1.515700 | 1.530100 |
| 16,384 | 10.201300 | 17.487500 | 25.886950 | 23.561500 | 24.072550 |
| 262,144 | 153.380850 | 204.367900 | 357.819300 | 333.157800 | 346.572850 |
| 1,000,000 | 646.473150 | 805.153300 | 1,028.526300 | 976.876850 | 1,039.974250 |

**Raw E2E outcome:** all four dataset sizes show no GPU Raw E2E win. Every V0–V3 GPU Raw E2E median is slower than the corresponding CPU median; V0 is the fastest GPU Raw E2E variant in each row.

| N | V0 kernel-only | V1 kernel-only | V2 kernel-only | V3 kernel-only |
|---:|---:|---:|---:|---:|
| 1,024 | 0.129024 | 0.126976 | 0.034816 | 0.035840 |
| 16,384 | 2.799104 | 2.831360 | 0.288256 | 0.288768 |
| 262,144 | 30.111232 | 28.008960 | 0.509440 | 0.517120 |
| 1,000,000 | 92.921856 | 80.863232 | 1.552384 | 1.553408 |

**Kernel-only outcome:** V2 and V3 improve over V0 at every size. The V0/V2 median ratio rises from about `3.71x` at 1,024 rows to `59.86x` at 1,000,000 rows. This device-only improvement is not an end-to-end speedup and does not contradict the Raw E2E outcome.

## Clean verification of the analyzer fix

At clean revision `5711c824904b44d8a5ec3d834708202830687ada`, a fresh Ninja build was configured for Release/SM86 with MSVC 19.51.36248.0 and CUDA 13.3.73. The default all-target build completed 53/53 steps. Full CTest completed **23/23 passed, 0 failed**, including `s10.analysis.integrity`, `s10.benchmark.integrity`, `s10.orchestration.integrity`, all CPU/CUDA functional and required-matrix checks, and the frozen V2/V3 matrices. The 24-minute S10 benchmark was not rerun.

## Limitations

- The measurement and correctness packets are external historical evidence from revision `b5002e2c...`; the analyzer-fix verification is revision `5711c824...` and does not retroactively rebind them.
- The status remains `VALID-UNREVIEWED`: no formal environment/telemetry acceptance is claimed, and no `acceptance.json` is present or generated.
- Results cover one query, one dimension, four dataset prefixes, one Windows laptop, and one RTX 3060 Laptop GPU; they do not establish batch-query or cross-hardware behavior.
- Raw samples, telemetry, binaries, and SIFT1M bytes remain outside Git. This report records traceable identities and derived medians only.
- S7 and S8 historical evidence remains unchanged and retains its original interpretation.
