# V5 Batch Formal Results Report

Status: **FINAL — primary outcome `FAIL`**
Experiment: `V5-FORMAL-20261112-02`
Formal artifact source: `D:\v5-formal-20261112-02`

## 1. Identity and scope

| Field | Value |
|---|---|
| Measured revision | `f2e10d8f620b1fbd45403aae3b0c1c60eb815236` |
| Benchmark tree state | `clean` |
| Workload | Optional/secondary V5 Batch Exact NN experiment |
| Dataset | INRIA TexMex SIFT1M |
| GPU | NVIDIA GeForce RTX 3060 Laptop GPU |
| CUDA architecture | `sm_86` |
| Fast math | disabled |

Single Query remains the original primary project workload, and historical
V0-V4 evidence remains unchanged. This report records only the final V5 Formal
result.

## 2. Primary acceptance rule

The primary speedup is:

`CPU Batch Raw median / GPU V5 Raw median`

Primary acceptance requires the conservative 95% speedup confidence-interval
lower bound to be greater than `1.0x` for every one of the 12 mandatory
workloads.

## 3. Final outcome

**Primary outcome: `FAIL`**

- Passed: **10 / 12 workloads**
- Failed: **2 / 12 workloads**

All non-performance gates were accepted:

- completeness: `accepted`
- identity: `accepted`
- correctness: `accepted`
- telemetry: `accepted`
- environment: `accepted`

## 4. Failed workloads

| N | B | Query tile | Block size | Speedup | Conservative 95% CI | Outcome |
|---:|---:|---:|---:|---:|---:|---|
| 16,384 | 8 | 1 | 256 | `0.7392582734609282x` | `[0.7120028569280505, 0.7756209706152362]` | `FAIL` |
| 262,144 | 8 | 8 | 512 | `0.6825757574689355x` | `[0.6647699928479601, 0.7506786019655385]` | `FAIL` |

For both failed workloads, the complete conservative interval is below `1.0x`.
The other two `B=8` workloads (`N=1,024` and `N=1,000,000`) passed, so the
evidence does not support a claim that `B=8` universally fails.

## 5. Notable passing results

- `N=1,024, B=128`: `13.8675x`
- `N=16,384, B=128`: `7.2227x`
- `N=262,144, B=128`: `7.2163x`
- `N=1,000,000, B=32`: `4.1777x`
- `N=1,000,000, B=128`: `14.2188x`

## 6. Interpretation and disposition

V5 demonstrates substantial Batch Exact NN acceleration for many workloads.
Large batch sizes, especially `B=32` and `B=128`, perform strongly in this
Formal experiment. The remaining formal weakness is concentrated in the two
failed `B=8` workloads identified above; the retained evidence does not by
itself establish a causal bottleneck.

The pre-Formal tuning selection is frozen, and this `FAIL` result is final for
`V5-FORMAL-20261112-02`. Any further optimization or tuning requires a new
experiment identity or version and must not be presented as, or overwrite,
this Formal experiment.

## 7. Evidence

The retained repository evidence is under [`evidence/`](evidence/):

- [`acceptance.json`](evidence/acceptance.json)
- [`primary-comparisons.json`](evidence/primary-comparisons.json)
- [`summary.json`](evidence/summary.json)
- [`environment.json`](evidence/environment.json)
- [`analysis-manifest.json`](evidence/analysis-manifest.json)
