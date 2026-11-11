# S15 Public SIFT1M Benchmark Design Approval Record

Status: **APPROVED — implementation and correctness/integrity verification authorized; formal performance benchmark separately gated**\
Approval ID: Not assigned\
Approval date: 2026-11-11 (Asia/Bangkok)\
Approving role: Project Owner\
Approved design: [S15PublicSift1mBenchmarkDesignDecision-v1.0.md](../design/S15PublicSift1mBenchmarkDesignDecision-v1.0.md)\
Approved candidate design SHA-256: `4d91b8a659b94100654077988491a4a1b00a1405c067bfad508324bd267e0b16`\
Post-approval synchronized design SHA-256: `80b3984924d53f83323e85706d8a3138ce047664ec4d8b5649c2efd6393747df`\
Preparation baseline: `180c76738e35e3dd488abcc30e2121138909197d`

## 1. Purpose and current approval state

The Project Owner explicitly confirmed all 12 S15 design decisions and
authorized S15 implementation and correctness/integrity verification in the
conversation on 2026-11-11 (Asia/Bangkok). No official approval ID or signature
was supplied, and none is inferred by this record.

Before synchronization, the approved candidate design was independently
verified at SHA-256
`4d91b8a659b94100654077988491a4a1b00a1405c067bfad508324bd267e0b16`.
The post-approval design changes are limited to approval status, provenance,
and authorization wording; the approved technical design is unchanged. The
resulting synchronized design hash is recorded separately above.

The technical-review recommendation `APPROVE WITH CHANGES` and the preparation
commit message at `180c76738e35e3dd488abcc30e2121138909197d` are supporting
history, not the approval act. The explicit Owner confirmation in conversation
is the approval evidence recorded here.

## 2. Owner-approved decisions

The following 12 decisions correspond to Section 14 of the candidate design
and were explicitly confirmed by the Project Owner.

1. **Formal experiment status.** S15 is a formal public-dataset experiment that
   supplements, and does not replace, the required seeded synthetic core
   acceptance evidence.
2. **Question and scope.** S15 evaluates the primary single-query workload with
   CPU Float32 Raw E2E, CUDA V2 Raw E2E, CUDA V2 Prepared E2E, CUDA V2
   kernel-only, and separate construction costs. V0, V1, V3, V4, and batch
   evaluation are excluded from S15.
3. **Workload matrix and query coverage.** S15 uses
   `N={1024,16384,262144,1000000}`, `D=128`, and the 32 ordinals produced by
   `q[k]=floor(k*9999/31)` for `k in [0,31]`:
   `0, 322, 645, 967, 1290, 1612, 1935, 2257, 2580, 2902, 3225, 3548,
   3870, 4193, 4515, 4838, 5160, 5483, 5805, 6128, 6450, 6773, 7096,
   7418, 7741, 8063, 8386, 8708, 9031, 9353, 9676, 9999`.
4. **V2 block-size mapping.** The fixed mapping is
   `{1024:256,16384:128,262144:512,1000000:256}` for `D=128`. The 1,000,000
   value is a carry-forward rather than an independently tuned S15 result, and
   S15 performs no retuning.
5. **Correctness prerequisite.** Formal performance analysis requires the
   matching clean-revision 128-workload and 384-production-check correctness
   matrix, complete applicable S6 numerical evidence, bitwise CPU/Raw
   V2/Prepared V2 equivalence to the independently verified Float32 result,
   and matching configuration, dataset, build, arithmetic-environment, binary,
   source, query-list, and block-mapping identities.
6. **Timing and statistical methodology.** The four timing lanes, three
   construction distributions, deterministic rotation, 10 warm-ups, 100
   retained samples, `linear_n_minus_1` p5/median/p95, and deterministic
   2,000-resample 95% median bootstrap are fixed as specified by the design.
7. **Performance interpretation.** H1, H2, and H3 remain the predeclared
   universal-all-128 hypotheses. Per-workload results are `supported`,
   `rejected`, or `inconclusive` from the conservative ratio interval. A
   universal hypothesis is supported only when all 128 workloads are supported,
   rejected when at least one workload is rejected, and otherwise inconclusive.
   A rejected universal hypothesis remains a valid completed experiment when
   all evidence gates pass and does not create a new canonical speedup target.
8. **Break-even methodology.** Prepared V2 construction-plus-query point and
   conservative break-even use the formulas in Section 8.3 of the design.
   Persistent-resource destruction is excluded, so results are not complete
   lifetime costs.
9. **Dataset provenance and identity.** Dataset bytes remain external and are
   not redistributed. Mandatory extracted-file identities are:
   `sift_base.fvecs`, 516,000,000 bytes, SHA-256
   `21f66e2975057b5728ba56de1c825bac4f4d89d596609ae985741c6242631816`;
   and `sift_query.fvecs`, 5,160,000 bytes, SHA-256
   `f7fc9be140accdfd64116c2fa2365ecdb69b8f084970c6b0532db5ff79ac8fdc`.
10. **Telemetry and evidence integrity.** S15 reuses the approved S14 controls:
    1,000 ms telemetry cadence, 3,000 ms maximum accepted gap, 900 ms bounded
    query timeout, monotonic attempt/lifecycle evidence, structured invocation,
    preserved prerequisite logs, blocking treatment for malformed or
    identity-mismatched evidence, and environment-inconclusive treatment for
    incomplete or contaminated telemetry. The bounded S15 non-performance
    preflight remains isolated from correctness and formal performance evidence.
11. **Implementation boundary.** A later, separately scoped implementation may
    add the S15 config, harness, runner, analyzer, tests, and backward-compatible
    loader extension. No CUDA production or public API change is included; any
    discovered need for one requires separate Owner authorization.
12. **Formal execution boundary.** Design or implementation approval does not
    authorize the formal S15 benchmark. Formal execution requires separate
    explicit Owner authorization after exact-revision integrity and correctness
    verification.

## 3. Incorporated review clarifications

The approved design incorporates the three requested technical-review changes:

- Evidence Acceptance, Per-workload Performance Outcomes, Per-N Descriptive
  Results, and Universal Hypothesis Outcomes are distinct reporting layers.
- Prepared V2 speedup is steady-state dataset/workspace reuse only. Construction
  distributions and the applicable break-even classification accompany any
  Prepared V2 speedup claim; first-query and complete lifetime claims are not
  implied.
- The non-performance preflight is bounded to at most 28 invocations, changes
  no matrix, query, block size, timing boundary, statistic, hypothesis, or
  tuning decision, and cannot contribute to formal correctness or performance
  evidence.

## 4. Approval binding and governance synchronization

This record binds the Owner confirmation to the exact approved candidate hash
and all 12 decisions in Section 2. The synchronized design hash differs only
because the approved status, approval provenance, and authorization boundary
were written into the design after confirmation.

The approval is recorded as DR-030 in
[DecisionRegister.md](DecisionRegister.md), and the design plus this approval
record are listed in [SourceOfTruth.md](SourceOfTruth.md). The Owner-created
repository revision that later publishes these synchronized records supplies
their durable Git provenance. A commit message alone remains insufficient
approval evidence.

## 5. Authorization boundary

This approval authorizes S15 implementation within the approved design and the
corresponding correctness/integrity verification, including repository-supported
builds, integrity tests, correctness checks, and the bounded non-performance
preflight. It does not authorize dependency or environment changes, dataset
download, scope expansion, CUDA production/public API changes, or Git/GitHub
mutation.

Formal S15 performance benchmark execution is not authorized. It requires a
separate explicit Project Owner approval after the required implementation,
integrity, exact-revision identity, and correctness prerequisites are ready.

The Project Owner retains all branch, worktree, staging, commit, push,
pull-request, approval, merge, tag, and ref operations.
