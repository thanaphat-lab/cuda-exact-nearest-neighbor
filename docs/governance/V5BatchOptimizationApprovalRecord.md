# V5 Batch Optimization Approval Record

Status: Owner approval recorded; implementation and the recovered frozen V5-A
experiment requirements below are authorized; explicitly prospective policy
choices remain proposed and inactive
Approval date: 2026-11-12 (Asia/Bangkok)
Approving role: Project Owner
Prepared base revision: `844c11d70075813064f904172670c83b156f760d`
Approval ID: none assigned
Hash-bound V5 design carrier: none created or claimed

## 1. Provenance and authority

The Project Owner reported completion and approval of the V5-A audit in the
conversation and authorized V5-B implementation on 2026-11-12. This record
preserves the approval facts supplied in that authorization. It does not invent
a signed record, an approval identifier, a design-carrier checksum, or
historical approval of concrete engineering details that the approval left to
Codex.

The canonical project specification and S6 numerical policy continue to
control numerical behavior and acceptance. V5 is an optional/secondary Batch
Optimization experiment. It does not redefine historical V0-V4, replace V0-V3
core evidence, alter existing CPU Batch behavior, or change the primary Single
Query designation.

## 2. Approved V5 constraints recovered in the authorization

- V5 provides an isolated synchronous Raw Batch API plus reusable Prepared
  Dataset and Workspace APIs.
- Supported query tiles are exactly `{1,4,8}` and supported block sizes are
  exactly `{128,256,512}`.
- Search remains exhaustive exact L2-squared over every candidate, using the
  approved ascending-dimension Float32 fused-FMA graph and deterministic
  smallest-index tie-breaking.
- Existing numerical validation, invalid-input behavior, finite-core-domain
  treatment, and failure semantics remain applicable.
- The V5 diagnostic/benchmark workflow must retain its approved frozen
  12-workload matrix, deterministic query selection, statistical protocol,
  and separately identified timing boundaries.
- Telemetry applies a 900 ms limit from subprocess start request through normal
  completion. Timeout cleanup is recorded separately and is not folded into a
  falsely successful normal-completion duration.
- CPU Batch and historical V4 implementation/evidence remain unchanged.
- Codex may execute static and CPU-only checks, but GPU diagnostics, GPU tests,
  preflight, and formal benchmarks are Owner-executed actions requiring the
  applicable separate authorization.

The Owner's V5-B infrastructure request supplied the recovered frozen V5-A
requirements on 2026-11-12. They are recorded here without creating a design
carrier, approval ID, or signed record:

- public SIFT1M, `D=128`, with `N={1024,16384,262144,1000000}` and
  `B={8,32,128}` (12 mandatory workloads);
- one ordered buffer of 128 deterministic distinct queries selected by
  `q[k]=floor(k*9999/127)`, `k=0..127`; batch sizes 8 and 32 use prefixes;
- the unchanged `cpu_search_batch_float32` CPU baseline;
- the Cartesian V5 candidate space query tile `{1,4,8}` by CUDA block size
  `{128,256,512}`;
- 10 warm-ups, 100 retained samples, 2000 deterministic bootstrap resamples,
  and `linear_n_minus_1` percentiles;
- primary speedup `CPU Batch Raw median / GPU V5 Raw median` with conservative
  95% interval `[CPU_ci_low/GPU_ci_high, CPU_ci_high/GPU_ci_low]`;
- primary PASS only when the lower interval bound is greater than 1.0 for all
  12 workloads and all mandatory correctness/evidence gates are accepted;
- Prepared, Kernel, Construction, Amortization, and Break-even remain separate
  secondary results;
- diagnostic selection uses the lowest valid Prepared E2E median per `(N,B)`,
  breaking ties by smaller query tile and then smaller block size, and the
  selection is frozen before formal measurement; and
- telemetry uses a 900 ms subprocess deadline, 1000 ms cadence, and maximum
  3000 ms start-time gap.

The bootstrap PRNG seed, exact candidate/lane rotation, and certain evidence
schema details were not historically established. The isolated configuration
records deterministic proposals for those items as `PROPOSED / NOT
OWNER-APPROVED`. Diagnostic and formal execution remain inactive until an Owner
approval file binds the exact configuration and proposed-policy hashes.

## 3. Newly selected implementation details

The following are V5-B engineering choices made under the Owner's delegated
implementation authority. They are compatible with the approved constraints
but were not historically approved by V5-A:

- a separate `s6/cuda_v5.hpp` public surface and separate CUDA translation unit;
- query-major Raw Batch input;
- a Prepared Dataset stored in dimension-major (SoA) device layout;
- a Workspace that owns reusable query, partial-record, and final-record device
  buffers for one fixed block-size/query-tile configuration;
- a two-stage reduction: tiled query/candidate partial reduction followed by
  one deterministic lexicographic final reduction per query;
- synchronous API completion through stream synchronization before host result
  return; and
- workspace single-use guarding plus disabling after an undrainable backend
  synchronization failure.

These choices may be reviewed and revised without claiming a new project
requirement, provided the approved numerical and experimental contracts remain
unchanged.

## 4. Authorization boundary and runtime status

V5-B repository-local implementation and permitted correctness/integrity work
are authorized. No V5 correctness, GPU build, GPU test, diagnostic, preflight,
or performance result is established by this approval record. Formal evidence
must remain revision-bound and is accepted only after the applicable complete
correctness gate passes for the same revision and configuration.
