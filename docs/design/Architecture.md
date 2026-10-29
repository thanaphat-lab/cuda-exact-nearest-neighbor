# Architecture

Status: S6 approved implementation architecture; runtime unverified\
Requirements: [FinalProjectSpecification-v1.0.md](../specifications/FinalProjectSpecification-v1.0.md) and [S6 Numerical Policy Addendum v1.0](../specifications/FinalProjectSpecification-S6-Numerical-Addendum-v1.0.md)\
Traceability: [RequirementsTraceability.md](../requirements/RequirementsTraceability.md)\
Approved design: [S6ApprovedImplementationDesign-v1.0.md](S6ApprovedImplementationDesign-v1.0.md)

## 1. Architectural intent

The project compares exact nearest-neighbor implementations under a common behavioral contract. The primary path processes one query; batch processing is a separate secondary path. Float32 CPU/GPU computation is evaluated with a Float64 reference and an independent validation layer.

The arithmetic accumulation graphs remain fixed by the approved S6 numerical policy. The API, ownership, AoS/SoA layouts, V0–V3 reduction architecture, timing boundaries and UD-004 tuning method are approved by `S6-DESIGN-OWNER-APPROVAL-001`. Actual block-size winners, ABI behavior, compiler/toolchain compatibility, arithmetic environment, resource capacity, correctness and performance remain unverified.

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
| Input-domain validator | Enforce rejection of NaN, infinity, `N = 0`, and `D = 0`; permit signed zero; distinguish API misuse and valid-outside-domain outcomes. | Uses the approved typed outcome model; does not issue an NN verdict. |
| Float64 reference | Execute the approved ascending-dimension sequential Float64 FMA graph after exact promotion of binary32 operands; retain per-candidate distances, argmin, and tie diagnostics. | Diagnostic/reference only; index agreement never substitutes for Float32 correctness. |
| Float32 CPU production baseline | Execute the same approved Float32 graph and behavior contract required of CUDA V0–V3. | Uses exhaustive L2² and smallest-index ties; graph substitutions or evaluation-order changes are nonconforming. |
| CUDA V0 | Use an AoS candidate-parallel distance kernel followed by a serial device reducer. | One thread completes one approved per-candidate graph. |
| CUDA V1 | Use host-prepared SoA candidate data followed by the same serial device reducer. | Preserves the graph while isolating the coalescing change. |
| CUDA V2 | Use SoA candidate computation, shared-memory block reduction and a pair-only grid hierarchy. | Reduces only completed records; kernel boundaries provide cross-block ordering. |
| CUDA V3 | Use SoA candidate computation, COND-01 safe-source warp/block reduction and a pair-only hierarchy. | Full physical warps, common masks, active logical tail lanes and smallest-index ties are mandatory. |
| Optional V4 | Explore batch optimization and data reuse. | Separate secondary scope; not required for core completion. |
| Result/numerical validator | Independently reproduce the approved Float32 graph for every candidate; check UD-001, exhaustive lexicographic argmin, exact/conservative contender sets, ties, provenance, and ordered verdict. | Cannot use Float64 agreement, contender membership, or boundary uncertainty alone as acceptance. |
| Exact-dyadic stress oracle | Evaluate mathematical ties, proof-sensitive values, exact-zero, underflow-to-zero, and interval-boundary cases with exact dyadic arithmetic. | Supplements the fast validator; it is not required to process every candidate in every large core matrix case. |
| Benchmark harness | Produce separate kernel-only and end-to-end samples only after the complete matching correctness matrix is accepted. | Excludes validator, oracle, diagnostics, and report-generation costs from production timing boundaries; preserves the existing sampling methodology. |
| Experiment reporter | Bind evidence to revision/configuration and report supported, rejected, or inconclusive outcomes. | Does not generate missing measurements or smooth over blocked evidence. |

### 3.1 Approved API and ownership summary

Production operations are raw one-shot GPU search, dataset preparation,
prepared-dataset search, workspace allocation/reuse and CPU Float32 search.
The Float64 reference is verification-only. Prepared datasets are immutable and
shareable after completion; one workspace is exclusive to one in-flight
invocation. Outcomes separately report contract-invalid input, API misuse,
resource failure, backend failure and numerical-domain status.

### 3.2 Nonfinite production status

Only missing tail records have logical validity false.

Production reduction consists of:

1. optional finite lexicographic argmin over finite `(distance,index)` values;
2. independent OR aggregation of nonfinite/domain-failure status.

A real nonfinite candidate remains logically present but does not enter the
finite floating comparator. Its failure status propagates through every warp,
block and grid stage.

The two component operations and their product are associative. Finite-core
nonfinite arithmetic is a correctness defect. Outside-domain evidence remains
distinct from invalid input, and applicable failure prevents reporting a
successful accepted NN result.

The architecture does not require selection of a first or smallest failing
candidate index.

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

The S6 numerical policy approves the arithmetic graphs and evidence contracts. `S6-DESIGN-OWNER-APPROVAL-001` approves the UD-004 method and UD-005 V0–V3 implementation architecture. Actual hardware values and all runtime evidence remain unverified. Design approval and this derived architecture do not authorize documentation mutation, implementation, environment changes, builds, tests, benchmarks or Git/GitHub actions.
