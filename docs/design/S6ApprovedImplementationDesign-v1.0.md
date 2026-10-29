# S6 Approved Implementation Design v1.0

Status: Owner-approved implementation design; runtime evidence not executed
Design approval: `S6-DESIGN-OWNER-APPROVAL-001`
Approving role: Project Owner
Approval date: Not captured in supplied approval evidence
Synchronization baseline: `e848668e48a3ea119bd9d032c0be3c1ed37dfb71`
Numerical policy: `S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0`
Numerical-policy SHA-256: `503aa6b2c97567d814a9316fb0f2f9890d5472aea2bcf1f928ac505c29d1aa39`
Approval record: [S6DesignApprovalRecord.md](../governance/S6DesignApprovalRecord.md)

## 1. Authority and source manifest

This record normalizes the implementation design approved under
`S6-DESIGN-OWNER-APPROVAL-001`.

| Source | Role | Integrity statement |
|---|---|---|
| `S6 Final Implementation Design Review Package v1.0` | Base implementation design | Approved by title/version; no source checksum claimed |
| `S6 Final Design Refinement & Owner Decision Package v1.1` | Benchmark, API, V3 and resource refinement | Approved by title/version; no source checksum claimed |
| COND-01 — V3 Warp Participation | Controlling clarification | Explicit Owner condition |
| COND-02 — Nonfinite Valid Candidates | Controlling clarification | Explicit Owner condition |
| COND-03 — Independent Tuning | Controlling clarification | Explicit Owner condition |

Precedence within this design scope is:

1. Canonical specifications and the approved numerical policy.
2. COND-01 through COND-03.
3. v1.1 within its refinement scope.
4. Unaffected v1.0 provisions.
5. Derived documents that summarize this record.

This record does not claim byte-level ratification of either conversational
design report. Its durable repository identity is the Owner-created squash
commit that first publishes this file.

## 2. Scope and invariants

- Primary workload: one query.
- Secondary workload: batch query, reported separately.
- V0 through V3 are core.
- V4 remains optional and deferred.
- Production CPU and CUDA computation uses Float32.
- The Float64 path is independent validation and diagnostics, not production.
- Search is exhaustive L2² over every candidate.
- Equal computed Float32 distances select the smallest index.
- Per-candidate dimensions use the approved ascending sequential fused-FMA graph.
- Candidate reduction operates only after a candidate distance is complete.
- No design provision changes UD-001 through UD-003 or their verdict policy.

## 3. Public operations and typed outcomes

### 3.1 Common production outcome

Each production operation reports:

    invocation_status:
      completed | contract_invalid | api_misuse |
      resource_failure | backend_failure | not_executed

    input_outcome:
      valid | invalid_nonfinite | invalid_N_zero |
      invalid_D_zero | not_applicable

    domain_status:
      core_eligible | valid_outside_approved_domain |
      unknown | not_applicable

    result:
      optional { index:uint64, squared_distance:float32 }

    backend_detail:
      non-contractual diagnostic code and message

An NN verdict is not part of the production API. NN verdicts are produced only
by the approved independent verification process.

Contract-invalid input is limited to NaN, infinity, `N=0` and `D=0`. Signed
zero is valid.

Null views, inconsistent element counts, address/size overflow, stale handles,
incompatible devices or workspaces, and unsupported configuration are API
misuse rather than contract-invalid input.

A valid input outside the approved numerical domain is never relabeled
contract-invalid. It receives distinct domain evidence and no accepted NN
verdict.

### 3.2 Operations

| Operation | Ownership and behavior |
|---|---|
| Raw one-shot GPU search | Accepts host AoS dataset/query, validates them, creates transient storage, converts V1–V3 data to SoA, transfers inputs, runs the full production sequence, returns host result/status and releases call-owned storage |
| Dataset preparation | Validates the dataset, creates the selected AoS/SoA device representation and returns an immutable move-only prepared object |
| Prepared-dataset search | Borrows an immutable prepared dataset and exclusive compatible workspace; validates/transfers the query, executes production kernels and returns result/status |
| Workspace allocation | Creates query, result, status and reduction buffers for one device, variant and maximum configuration |
| CPU Float32 search | Synchronous exhaustive single-threaded production baseline using the approved Float32 graph |
| Float64 reference | Verification-only computation using exact binary32-to-binary64 promotion and the approved Float64 graph |

Prepared datasets may be shared after preparation completes. A workspace may
not be used by overlapping invocations. One workspace is required per
concurrent search.

GPU search operations accept a CUDA stream, order work on that stream, and
synchronously establish completion for the returned host result. Normal
operation does not require device-wide synchronization.

Handles use deterministic ownership. Destruction must not race outstanding
work and cleanup must not throw.

## 4. Data representation and ownership

- Public dataset format: contiguous row-major AoS, `x[i*D+j]`.
- CPU and V0 consume AoS.
- V1 through V3 consume SoA, `x[j*N+i]`.
- Initial V1–V3 preparation uses host-side AoS-to-SoA conversion.
- The caller owns input views through operation completion.
- A prepared object owns its device dataset representation and metadata.
- A workspace owns query, result, status and pair buffers.
- Internal byte-size and index calculations are overflow-checked.
- The public and reduction index type is `uint64`.

The proposed packed transport record is:

    distance : Float32
    flags    : uint32
    index    : uint64

The flags contain separate bits for:

- logical real-candidate presence;
- finite-minimum eligibility;
- nonfinite arithmetic;
- applicable numerical/domain-failure status.

The proposed physical size is 16 bytes and must be verified independently on
host and device before it is relied upon.

The record represents two logically independent reductions:

1. an optional lexicographic minimum over completed finite candidate distances;
2. monotonic aggregation of failure/domain status across all real candidates.

NaN and infinity never enter the ordinary floating-point minimum comparator.

## 5. CPU and Float64 designs

The CPU Float32 baseline validates the input, then for each candidate performs
the approved subtraction and fused-FMA nodes for dimensions `0..D-1` in
strictly ascending order. It selects the lexicographic minimum of
`(computed Float32 distance,index)`.

The Float64 reference promotes each binary32 operand exactly, uses the approved
ascending sequential Float64 fused-FMA graph, retains per-candidate distances
and computes independent Float64 argmin and tie diagnostics.

The Float64 reference does not substitute for Float32 production correctness.
Source spelling does not establish graph or arithmetic-environment conformance.

## 6. CUDA V0 through V3

### V0

- AoS dataset.
- One thread owns one candidate and evaluates dimensions sequentially.
- The distance kernel writes `N` Float32 distances.
- A one-thread device kernel performs final lexicographic reduction.

### V1

- SoA dataset for candidate-coalesced access.
- The per-candidate graph is unchanged.
- The same serial device reducer is retained to isolate the layout change.

### V2

- SoA dataset.
- Each thread completes one candidate distance.
- Each block performs a shared-memory lexicographic reduction.
- Successive pair-only kernels reduce block outputs until one record remains.
- Kernel boundaries provide cross-block synchronization.

### V3

- SoA dataset and the same per-candidate graph.
- Warp shuffles reduce completed records.
- Warp leaders publish to shared memory.
- The first warp reduces warp-leader records.
- Pair-only kernels complete the grid hierarchy.

No V0–V3 implementation may parallelize or reorder contributions within one
candidate distance.

## 7. COND-01 — V3 warp participation

- Approved block candidates are `128`, `256` and `512`; launched physical
  warps are therefore full.
- All lanes in a warp collective use the same participation mask.
- No lane returns before a required shuffle or block barrier.
- Tail lanes remain active and carry `logical_candidate_present=false`.
- Noncontiguous divergent participation is outside approved V3 core scope.
- Every shuffle source names an active lane. If the intended partner is absent,
  the current lane is the safe source and that value is ignored.
- Distance, full-width `uint64` index, flags and validity are transported
  consistently.
- Lane 0 publishes the warp result.
- All block threads participate in the barrier after warp publication.
- The first warp loads real warp results and logical-invalid records for
  unused lanes, then applies the same safe-source reduction.
- Kernel boundaries complete cross-block ordering.
- Equal distances across lanes, warps, blocks or hierarchy stages select the
  smallest candidate index.

Direct fixtures cover logical tail counts, masks, invalid tail records, ties
within and across warps, ties across blocks, hierarchy stages and full-width
index transport.

## 8. COND-02 — nonfinite real candidates

Reduction contains two logically independent associative operations.

### 8.1 Finite lexicographic argmin

The finite-minimum state is an optional finite `(distance,index)` pair.

- A finite real candidate contributes its computed Float32 distance and full
  `uint64` index.
- A nonfinite real candidate contributes no value to the finite-minimum
  operation.
- A missing tail lane contributes no value.
- The empty state is the identity.
- Two present finite values combine by distance, then smallest index.

Only values satisfying the finite predicate enter this comparator. NaN and
infinity are never passed to an ordinary floating-point minimum or
lexicographic distance comparison.

The optional finite lexicographic minimum is associative.

### 8.2 Failure and domain-status aggregation

Every record separately carries:

    logical_candidate_present
    nonfinite_arithmetic_seen
    domain_failure_bits

Status combines by boolean OR and bitwise OR. The identity is all fields false
or zero. This aggregation is associative, commutative and idempotent.

A real candidate in `[0,N)` always sets
`logical_candidate_present=true`, including when its completed arithmetic is
NaN or infinity.

A missing tail lane or out-of-range pair-buffer entry sets
`logical_candidate_present=false` and contributes no failure status.

Aggregated presence is status evidence only. It does not prove that every
candidate in `[0,N)` participated; exhaustive candidate coverage remains an
independent mandatory verification obligation.

### 8.3 Product reduction and outcome

Warp, block and grid reduction combine the finite-minimum state and status
state independently. The product operation is associative.

Nonfinite/domain-failure status propagates through every hierarchy stage and
remains observable in the final host result status.

For a required finite-core case, observed nonfinite arithmetic is a correctness
defect and no successful NN result is reported.

For a valid input outside the approved numerical domain, retain
`valid_outside_approved_domain` evidence. Any finite-minimum payload is
diagnostic only and receives no accepted NN verdict when applicable failure
status is present.

A real nonfinite candidate must never be converted to a logical-invalid tail
record. If `N>0` but no logical real candidate reaches the hierarchy result,
candidate coverage or reduction is defective.

Failing-candidate identity is not a normative reduction output. Optional
diagnostic capture of failing indices requires separate Owner approval if
later proposed as mandatory.

## 9. Timing boundaries

### BND-E2E-RAW — primary

Start immediately before the one-shot production API performs validation,
preprocessing, allocation or transfer on an already-generated host AoS dataset
and query.

Include structural and contract validation, per-call allocation, host
AoS-to-SoA conversion for V1–V3, dataset/query H2D transfer, the complete
production kernel sequence, result/status D2H transfer, synchronization and
release of call-owned transient resources.

End when the synchronous call returns with host result/status available and
transient resources released.

Exclude dataset generation, startup, compilation, independent validators,
Float64 reference, exact-dyadic oracle, diagnostic-only capture, logging,
correctness classification and report generation.

### BND-E2E-PREP — supplementary

Prepared dataset construction and persistent workspace allocation complete
before timing. Start immediately before per-query validation. Include query
validation, status reset, query transfer, full production kernel sequence,
result/status transfer, synchronization and per-search cleanup. End when the
synchronous prepared search returns.

Dataset preparation and workspace allocation are excluded and may be reported
separately. This supplementary track cannot replace BND-E2E-RAW.

### BND-KERNEL

Dataset, query, workspace, result and status storage are already device
resident. Record the start event immediately before the first required
production kernel and the end event immediately after the last required kernel
on the same stream. Include every reduction stage and production status
propagation. Event synchronization establishes completion.

Exclude allocation, conversion, host preprocessing, transfers, validators,
reference/oracle work, diagnostic-only kernels, logging and reporting.

## 10. COND-03 — independent tuning

- Candidates are predeclared as `{128,256,512}`.
- Tuning seed and samples are distinct from final-reporting seed and samples.
- A candidate satisfies required correctness gates before tuning eligibility.
- Each reported tuning configuration uses 10 warm-ups and 100 measured
  repetitions and reports median, p5 and p95.
- Selection uses the predeclared BND-KERNEL median.
- An exact measured median tie selects the smaller block size.
- The selected configuration is frozen before final measurements.
- The complete applicable correctness matrix is rerun for the selected
  revision and configuration.
- Final benchmark measurements are never used retrospectively for tuning.
- A later retuning attempt is a new predeclared experiment.

Actual winning values remain target-specific observed evidence. They are not
Owner-approved constants.

## 11. Resource model

Define:

    X = 4*N*D                         dataset bytes
    Q = 4*D                           query bytes
    F = 4*N                           full distance buffer
    P = verified sizeof(reduction record), proposed as 16
    M = ceil(N/B)                     first-stage partial count
    W = 2*M*P                         ping-pong pair buffers
    S = measured result/status/runtime bookkeeping

| Variant | Device model | One-shot host peak |
|---|---|---|
| V0 | `X+Q+F+P+S` | `X+Q` |
| V1 | `X+Q+F+P+S` | `2X+Q` during conversion |
| V2 | `X+Q+W+P+S` | `2X+Q` |
| V3 | `X+Q+W+P+S` | `2X+Q` |

For `N=1,048,576`, `D=128`, `X=512 MiB`, `F=4 MiB` and
`Q=512 bytes`. With `B=128` and `P=16`, `W=256 KiB`.

Diagnostic capture, Float64 distances, interval/oracle state, allocator
metadata, CUDA context, driver reservations, display use, profiler overhead and
compiler spills remain separately identified.

Calculated bytes do not establish hardware capacity. Future preflight records
total/free memory, actual type sizes, compiler resources and the exact largest
allocation sequence.

## 12. Verification obligations

Verification retains:

- independent every-candidate Float32 graph and argmin validation;
- independent Float64 reference;
- UD-001 checks for every candidate;
- exact-zero assertions;
- exact `C` or sound outward `Ĉ`;
- exact-dyadic stress fixtures;
- direct V2/V3 reduction fixtures;
- COND-01 mask/tail/tie fixtures;
- COND-02 fixtures proving real nonfinite candidates remain present, nonfinite
  values never enter the finite comparator, the two reductions combine
  independently, status survives all hierarchy stages, and applicable failure
  prevents a successful accepted result;
- actual production-path evidence separate from diagnostic capture;
- ABI verification for the proposed 16-byte record;
- compiler/runtime graph and arithmetic-environment evidence;
- revision, working-tree, build, configuration and environment binding.

Missing required evidence is blocked/unverified unless a demonstrated defect
already establishes failure.

## 13. Status and exclusions

- UD-004 design method: approved.
- UD-004 hardware values: unverified and measurement-dependent.
- UD-005 V0–V3 implementation architecture: approved.
- V4: deferred and optional.

Not executed or approved by this record:

- ABI conformance;
- compiler/toolchain compatibility;
- arithmetic-environment conformance;
- hardware capacity;
- correctness;
- performance;
- dependency or environment changes;
- implementation;
- Git or GitHub mutation.

## 14. Integration and authorization boundary

Design approval alone authorizes no repository or runtime action.

Each future action requires its applicable separate authorization, including
documentation editing, branch creation, staging and commit, publication/push,
PR creation, implementation, dependency/environment change, and build, test,
probe, profiling or benchmark execution.

Documentation and implementation changes use focused branches and reviewed PRs
into `main` only after those stages are authorized.

The Project Owner alone performs Squash & Merge into `main`. Codex may not
merge, squash merge, rebase merge, enable auto-merge or submit the PR to an
automatic merge mechanism. Branch deletion remains an Owner decision.

Future implementation authorization must cite the observed `main` squash
commit that contains this design and approval record.
