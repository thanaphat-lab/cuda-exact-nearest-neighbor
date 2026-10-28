# Architecture

Status: S6-synchronized derived architecture; no implementation approval\
Requirements: [FinalProjectSpecification-v1.0.md](../specifications/FinalProjectSpecification-v1.0.md) and [S6 Numerical Policy Addendum v1.0](../specifications/FinalProjectSpecification-S6-Numerical-Addendum-v1.0.md)\
Traceability: [RequirementsTraceability.md](../requirements/RequirementsTraceability.md)

## 1. Architectural intent

The project compares exact nearest-neighbor implementations under a common behavioral contract. The primary path processes one query; batch processing is a separate secondary path. Float32 CPU/GPU computation is evaluated with a Float64 reference and an independent validation layer.

This document defines boundaries and responsibilities only. The arithmetic accumulation graphs are fixed by the approved S6 numerical policy. API, file layout, memory layout, launch configuration, reduction architecture, error representation, ownership, and hardware-specific settings remain future approved-design work under UD-004 and the unresolved portion of UD-005 in [DecisionRegister.md](../governance/DecisionRegister.md).

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
       - every-candidate Float32 graph conformance
       - every-candidate UD-001 bound
       - exact C / conservative Ĉ classification
       - boundary uncertainty and contender provenance
       - Float32 lexicographic argmin and tie checks
       - ordered UD-003 verdict
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
| Float64 reference | Execute the approved ascending-dimension sequential Float64 FMA graph after exact promotion of binary32 operands; retain per-candidate distances, argmin, and tie diagnostics. | Diagnostic/reference only; index agreement never substitutes for Float32 correctness. |
| Float32 CPU production baseline | Execute the same approved Float32 graph and behavior contract required of CUDA V0–V3. | Uses exhaustive L2² and smallest-index ties; graph substitutions or evaluation-order changes are nonconforming. |
| CUDA V0 | Provide the naive GPU baseline. | Same semantics/input/tie contract as every core variant. |
| CUDA V1 | Target coalesced memory access. | Detailed layout/access scheme is future design work. |
| CUDA V2 | Target block-level reduction. | Detailed reduction and synchronization scheme is future design work. |
| CUDA V3 | Target warp-level or otherwise optimized reduction. | Detailed primitives and hardware tuning are future design work. |
| Optional V4 | Explore batch optimization and data reuse. | Separate secondary scope; not required for core completion. |
| Result/numerical validator | Independently reproduce the approved Float32 graph for every candidate; check UD-001, exhaustive lexicographic argmin, exact/conservative contender sets, ties, provenance, and ordered verdict. | Cannot use Float64 agreement, contender membership, or boundary uncertainty alone as acceptance. |
| Exact-dyadic stress oracle | Evaluate mathematical ties, proof-sensitive values, exact-zero, underflow-to-zero, and interval-boundary cases with exact dyadic arithmetic. | Supplements the fast validator; it is not required to process every candidate in every large core matrix case. |
| Benchmark harness | Produce separate kernel-only and end-to-end samples only after the complete matching correctness matrix is accepted. | Excludes validator, oracle, diagnostics, and report-generation costs from production timing boundaries; preserves the existing sampling methodology. |
| Experiment reporter | Bind evidence to revision/configuration and report supported, rejected, or inconclusive outcomes. | Does not generate missing measurements or smooth over blocked evidence. |

## 4. Common result contract

For every valid single-query invocation, each production implementation conceptually yields:

- one candidate index in `[0, N)`;
- that candidate's computed Float32 squared distance;
- an execution status sufficient to distinguish success from rejected input.

This is a behavioral boundary, not a prescribed language-level function signature. Equal computed distances select the smallest index. V0–V3 cannot change the distance definition, candidate set, valid-input domain, output meaning, or tie rule.

The Float64 reference may retain additional distances for diagnostics. Its output is not interchangeable with the production Float32 result.

Production distance computation conforms to `S6-NUM-GRAPH-v1.0` and `S6-NUM-ENV-v1.0`. Passing the distance bound does not prove graph conformance, and graph conformance does not replace every-candidate bound or lexicographic-result validation.

## 5. Workload boundaries

### Primary: single query

One query is evaluated against all `N` candidate vectors. Correctness and performance reporting for this path are mandatory for the applicable core milestone.

### Secondary: batch query

Multiple queries are evaluated under the same per-query behavioral contract. Batch results are labeled and reported separately so aggregation or reuse cannot obscure the primary single-query behavior.

### Optional: V4

V4 may introduce batch-oriented reuse only after a future design approval. It cannot alter per-query exactness/tie semantics or satisfy missing V0–V3 completion evidence.

## 6. Error and evidence boundaries

- Invalid input is reported through the independent input outcome; no NN verdict is issued.
- Valid input outside the approved numerical domain is distinct from invalid input.
- Graph conformance, UD-001 distance error, UD-002 contender classification, Float32 lexicographic correctness, and the final NN verdict remain separate evidence.
- Exact contender membership and conservative outward membership retain distinct provenance.
- A demonstrated defect fails; otherwise missing mandatory evidence blocks.
- A missing compatible GPU makes required GPU execution blocked, not passed.
- Measurement acceptance begins only after the complete correctness matrix is accepted for the matching revision, tree state, build, workload, arithmetic environment, and variant.
- Raw or traceable samples and environment manifests are retained outside the logical compute components.

## 7. Design risks and deferred choices

| Risk/deferred item | Control |
|---|---|
| Compiler or implementation changes the approved arithmetic graph or environment. | Independently verify graph behavior, RNE, fused FMA, FTZ/DAZ state, reassociation, and intermediates; nonconformance fails. |
| Outward interval implementation is unsound or conservative membership is mislabeled exact. | Verify every arithmetic enclosure and comparison; retain exact/conservative provenance and boundary uncertainty. |
| Conservative contender sets become broad. | Treat breadth as diagnostic; acceptance still requires independent every-candidate Float32 correctness. |
| An optimization changes semantics. | Run one common V0–V3 contract suite and require AC-010. |
| Dataset values overflow Float32 squared distances. | Constrain and assert core data finiteness under AC-009. |
| Benchmark setup hides transfer/preprocessing costs. | Maintain separate timing classes with explicit boundaries under AC-013. |
| Hardware tuning becomes an undocumented requirement. | Record settings as experiment configuration; keep UD-004 separate from canonical semantics. |
| Optional batch work displaces core evidence. | Keep V4 and batch evidence separate; enforce AC-016. |

## 8. Approval boundary

The S6 numerical policy approves the arithmetic accumulation graphs and numerical evidence contracts. UD-004 and the remaining UD-005 design choices still require an approved implementation design before code creation. Neither policy approval nor this derived architecture is implementation authorization.
