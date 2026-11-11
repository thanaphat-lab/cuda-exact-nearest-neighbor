# S14 Batch Benchmark Design Decision v1.0

Status: Owner-approved prospective design; historical S14 evidence remains audit-limited\
Design approval: `S14-EVIDENCE-REMEDIATION-OWNER-APPROVAL-001`\
Approving role: Project Owner\
Approval date: 2026-11-11 (Asia/Bangkok)\
Approval scope: S14 batch benchmark evidence integrity and provenance\
Numerical policy: `S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0`\
Numerical-policy SHA-256: `503aa6b2c97567d814a9316fb0f2f9890d5472aea2bcf1f928ac505c29d1aa39`\

## 1. Decision and authority

The Project Owner approved this design prospectively on 2026-11-11 after the
independent audit of the S14 final evidence. The approval does not backdate an
S14 design approval to the historical experiment and does not convert that run
into unqualified accepted performance evidence.

This decision specializes evidence-capture and acceptance mechanics for the
optional V4 batch experiment. It does not amend the canonical numerical policy,
CUDA production implementation, public APIs, timing boundaries, workload,
sampling, statistical formulas, or V0–V3 core acceptance.

The separate approval record binds this finalized document by SHA-256. The
Owner-created repository revision that first publishes the document supplies
its Git provenance.

## 2. Preserved S14 experiment design

- Workload class is secondary batch-query evaluation; single-query remains the
  primary project workload.
- V4 is optional and cannot replace V0–V3 evidence.
- The workload matrix remains the 36 combinations of `N` in
  `{1024,16384,262144,1048576}`, `D` in `{8,32,128}`, and batch size in
  `{1,8,32}`.
- The final seed, distinct-query generator, frozen V2 block configurations,
  ten warm-ups, 100 retained repetitions, run-order rotation, timing
  boundaries, medians, percentiles, bootstrap intervals, ratio rule,
  throughput transforms, and break-even formulas remain unchanged.
- Correctness acceptance for the matching clean revision and configuration is
  required before performance analysis.
- Persistent-resource destruction remains excluded from construction and
  prepared-search costs; break-even results are not complete lifetime costs.

## 3. Telemetry continuity policy

The approved telemetry interval is 1,000 ms. The maximum accepted start-time
gap between consecutive metric samples and between consecutive process-sample
slots is 3,000 ms.

Each `nvidia-smi` query has a 900 ms timeout. This is below the sampling
interval, prevents either query from monopolizing a complete interval, and
leaves approximately 1,200 ms of the 3,000 ms gap budget for the other query,
process scheduling, parsing, and evidence writes. A timeout is recorded and
never treated as successful telemetry.

Cadence decisions use monotonic elapsed time. Wall-clock timestamps are also
recorded for correlation with the benchmark interval.

The S14-local collector records:

- contiguous attempt sequence numbers;
- scheduled, start, and finish timing;
- metric and process query durations, exit status, failures, and timeouts;
- metric and compute-process observations tied to the attempt sequence; and
- collector start, stop, process identity, and aggregate attempt counts.

The runner must receive one complete ready sample before launching the final
benchmark. It records benchmark and collector PIDs, start/end times, exit
codes, early termination, forced termination, interval, timeout, and maximum
gap policy.

## 4. Evidence classification

The analyzer independently derives environment status from the captured
configuration, Owner environment approval, GPU identity, benchmark lifecycle,
collector attempts, metrics, and process observations. A runner-produced
status is not authoritative input to this decision.

The following retain raw measurements but set every performance comparison
and aggregate hypothesis to `inconclusive`; a supported break-even result is
downgraded to an environment-inconclusive point estimate:

- missing metric or process coverage over any part of the benchmark interval;
- a metric or process sampling gap greater than 3,000 ms;
- any query timeout, query failure, capture error, or missing attempt;
- collector exit before benchmark completion or before the requested stop;
- nonzero collector exit or forced collector termination;
- the benchmark process not being observed by process telemetry;
- an unauthorized GPU process; or
- observed hardware thermal slowdown.

Malformed JSON/JSONL, noncontiguous or duplicate identities, invalid
timestamps, contradictory lifecycle evidence, revision/configuration
contradictions, or GPU UUID mismatches block acceptance rather than producing
an ordinary inconclusive result.

## 5. Command and prerequisite evidence

The runner records a structured effective top-level invocation containing the
PowerShell executable, runner path, effective argument array, working
directory, resolved paths, and runner/config/environment-approval hashes. A
human-readable command is derived from this record; `$MyInvocation.Line` is
not evidence.

The prerequisite CTest invocation preserves both a text output log and JUnit
report. Command start/end times, exit status, artifact length, and SHA-256 are
recorded. Missing or empty prerequisite logs block final measurement launch.

## 6. Verification obligations

CPU-only integrity tests must cover accepted telemetry, excessive gaps,
missing leading/trailing coverage, failed attempts, early termination,
malformed sequences, GPU identity mismatch, unauthorized processes, thermal
slowdown, environment-driven comparison/break-even downgrades, structured
invocation capture, and prerequisite-log preservation.

No performance benchmark is required to verify the evidence-integrity code.
A replacement performance run requires separate Owner authorization after
clean-revision correctness and integrity verification.

## 7. Historical S14 evidence

Experiment `S14-BATCH-V4-20261111-01` at revision
`6d27104bee7328589cefce31aa1d5a8ac23d1f51` remains immutable historical
evidence. Its numerical results and statistical derivations passed audit, but
the 140.99-second telemetry gap, blank top-level command record, missing
standalone prerequisite CTest logs, and absent contemporaneous design-approval
binding prevent unconditional performance acceptance.

Its status is `historical-audit-limited`. Neither this design nor its approval
record rewrites or reclassifies the original artifacts in place.

## 8. Exclusions and authorization boundary

This approval authorizes the scoped evidence-integrity implementation and
documentation. It does not authorize a performance run, CUDA or public-API
change, numerical-policy change, retrospective tuning, dependency or
environment change, or any Git/GitHub mutation. The Project Owner retains all
Git operations and any future replacement-run authorization.
