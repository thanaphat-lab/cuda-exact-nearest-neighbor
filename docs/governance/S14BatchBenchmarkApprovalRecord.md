# S14 Batch Benchmark Design Approval Record

Status: Approved prospective evidence-integrity design binding\
Approval ID: `S14-EVIDENCE-REMEDIATION-OWNER-APPROVAL-001`\
Approval date: 2026-11-11 (Asia/Bangkok)\
Approving role: Project Owner\
Approved design: [S14BatchBenchmarkDesignDecision-v1.0.md](../design/S14BatchBenchmarkDesignDecision-v1.0.md)\
Approved SHA-256: `ff75b8f7462ff6177536ccc24b014300b66d31e97485c475dc0515e76b518664`\

## 1. Approval binding

The Project Owner approved the S14 evidence-remediation approach on
2026-11-11 and selected these controlling decisions:

- 1,000 ms telemetry sampling interval and 3,000 ms maximum accepted gap;
- a bounded 900 ms timeout for each `nvidia-smi` query;
- missing coverage, excessive gaps, query failure/timeout, or early collector
  termination retain measurements but make performance hypotheses and
  supported break-even claims inconclusive;
- malformed or identity-mismatched evidence blocks acceptance;
- structured effective top-level invocation evidence and preserved
  prerequisite CTest logs/results;
- prospective design provenance with no retrospective approval claim; and
- preservation of the previous run as `historical-audit-limited`.

The SHA-256 above binds the finalized design bytes implementing those
decisions. No historical S14 artifact is modified or retrospectively approved.

## 2. Scope

This approval covers S14 evidence capture, continuity validation, lifecycle
records, command provenance, prerequisite logs, regression tests, and the
interpretation of incomplete environment evidence.

It does not change CUDA production code, public APIs, workload or timing
boundaries, sampling counts, numerical policy, statistical formulas, V0–V3
acceptance, or the optional status of V4.

## 3. Runtime and authorization boundary

Design approval and local integrity-test results do not constitute a clean
replacement performance run. Such a run requires separate Owner authorization
after clean-revision verification. This record authorizes no Git/GitHub action;
the Project Owner retains all branch, commit, push, pull-request, and merge
operations.
