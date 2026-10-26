# Architecture

Status: S4.2 derived architecture; no implementation approval\
Requirements: [FinalProjectSpecification-v1.0.md](../specifications/FinalProjectSpecification-v1.0.md)\
Traceability: [RequirementsTraceability.md](../requirements/RequirementsTraceability.md)

## 1. Architectural intent

The project compares exact nearest-neighbor implementations under a common behavioral contract. The primary path processes one query; batch processing is a separate secondary path. Float32 CPU/GPU computation is evaluated with a Float64 reference and an independent validation layer.

This document defines boundaries and responsibilities only. It does not select an API, file layout, memory layout, launch configuration, reduction algorithm, error representation, or hardware-specific setting. Those remain future approved-design work under UD-004 and UD-005 in [DecisionRegister.md](../governance/DecisionRegister.md).

## 2. Logical data flow

```text
Seed + (N,D) configuration
          |
          v
Synthetic dataset/query generator ---> Input-domain validation
          |                                      |
          | valid Float32 inputs                 | rejected-input evidence
          v                                      v
   +------+------+------------------+        Verification report
   |             |                  |
   v             v                  v
Float64       Float32 CPU      Float32 CUDA variants
reference     comparator       V0 -> V1 -> V2 -> V3
   |             |                  |
   +-------------+------------------+
                 v
       Result and numerical validator
       - argmin index observation
       - distance-error observation
       - ambiguity diagnostic
                 |
                 v
       Correctness gate / evidence store
                 |
        accepted correctness only
                 v
       Benchmark and experiment reporting
       - single query (primary)
       - batch/V4 (separate, optional as applicable)
```

## 3. Component responsibilities

| Component | Responsibility | Must preserve / must not decide |
|---|---|---|
| Dataset/query generator | Produce seeded synthetic Float32 inputs for required `(N,D)` combinations and derived edge cases. | Keeps core distances finite; records seed/configuration; does not choose numerical acceptance thresholds. |
| Input-domain validator | Enforce rejection of NaN, infinity, `N = 0`, and `D = 0`; permit signed zero. | Common behavior across CPU and V0–V3; exact error representation is deferred. |
| Float64 reference | Compute high-precision reference distances, reference argmin, and near-tie diagnostic inputs. | Diagnostic/reference only; never automatically accepts a Float32 result. |
| Float32 CPU comparator | Provide a Float32 computation lane for cross-checking the common behavior contract. | Uses L2², exhaustive search, and smallest-index ties; it is not the Float64 reference. |
| CUDA V0 | Provide the naive GPU baseline. | Same semantics/input/tie contract as every core variant. |
| CUDA V1 | Target coalesced memory access. | Detailed layout/access scheme is future design work. |
| CUDA V2 | Target block-level reduction. | Detailed reduction and synchronization scheme is future design work. |
| CUDA V3 | Target warp-level or otherwise optimized reduction. | Detailed primitives and hardware tuning are future design work. |
| Optional V4 | Explore batch optimization and data reuse. | Separate secondary scope; not required for core completion. |
| Result/numerical validator | Record argmin, distance error, and ambiguity separately and apply only approved policies. | Cannot invent UD-001–UD-003 or equate Float64 near ties with acceptance. |
| Benchmark harness | Produce separate kernel-only and end-to-end samples after the correctness gate. | Disables fast math for core benchmarks and records actual environment/timing boundaries. |
| Experiment reporter | Bind evidence to revision/configuration and report supported, rejected, or inconclusive outcomes. | Does not generate missing measurements or smooth over blocked evidence. |

## 4. Common result contract

For every valid single-query invocation, each production implementation conceptually yields:

- one candidate index in `[0, N)`;
- that candidate's computed Float32 squared distance;
- an execution status sufficient to distinguish success from rejected input.

This is a behavioral boundary, not a prescribed language-level function signature. Equal computed distances select the smallest index. V0–V3 cannot change the distance definition, candidate set, valid-input domain, output meaning, or tie rule.

The Float64 reference may retain additional distances for diagnostics. Its output is not interchangeable with the production Float32 result.

## 5. Workload boundaries

### Primary: single query

One query is evaluated against all `N` candidate vectors. Correctness and performance reporting for this path are mandatory for the applicable core milestone.

### Secondary: batch query

Multiple queries are evaluated under the same per-query behavioral contract. Batch results are labeled and reported separately so aggregation or reuse cannot obscure the primary single-query behavior.

### Optional: V4

V4 may introduce batch-oriented reuse only after a future design approval. It cannot alter per-query exactness/tie semantics or satisfy missing V0–V3 completion evidence.

## 6. Error and evidence boundaries

- Invalid input is rejected before a successful nearest-neighbor result is claimed.
- A missing compatible GPU makes required GPU execution blocked, not passed.
- Numerical observations remain separate fields through validation and reporting.
- Measurement begins only after correctness acceptance for the same relevant revision/configuration when performance acceptance is sought.
- Raw or traceable samples and environment manifests are retained outside the logical compute components.

## 7. Design risks and deferred choices

| Risk/deferred item | Control |
|---|---|
| Different Float32 evaluation orders change ordering near a tie. | Collect separate index/error/ambiguity evidence; block dependent verdicts until UD-001–UD-003 are resolved. |
| An optimization changes semantics. | Run one common V0–V3 contract suite and require AC-010. |
| Dataset values overflow Float32 squared distances. | Constrain and assert core data finiteness under AC-009. |
| Benchmark setup hides transfer/preprocessing costs. | Maintain separate timing classes with explicit boundaries under AC-013. |
| Hardware tuning becomes an undocumented requirement. | Record settings as experiment configuration; keep UD-004 separate from canonical semantics. |
| Optional batch work displaces core evidence. | Keep V4 and batch evidence separate; enforce AC-016. |

## 8. Approval boundary

This S4.2 architecture is documentation derived from approved contracts. Before CPU/CUDA implementation, a later milestone must approve detailed interfaces, layouts, algorithms, build configuration, and other choices required by UD-004/UD-005. This document does not authorize code creation.
