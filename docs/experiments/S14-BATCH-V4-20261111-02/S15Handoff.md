# S15 Public SIFT1M Benchmark Handoff

Status: **planning handoff only; S15 implementation has not started**.

## Starting point

- S14 is ready for Owner acceptance with correctness and environment gates
  passed and both predeclared universal performance hypotheses rejected.
- S15 returns to the primary single-query workload on the public INRIA TexMex
  ANN_SIFT1M dataset. It must remain separate from S14 optional batch evidence.
- Existing S10 assets provide a useful starting inventory:
  `benchmarks/s10_sift1m*`, `benchmarks/config/s10-sift1m.json`,
  `scripts/run-s10-sift1m.ps1`, `scripts/analyze-s10-sift1m.mjs`, loader tests,
  and the S10 exploratory report.
- S10 is `EXPLORATORY / VALID-UNREVIEWED`; its results must not be relabeled as
  S15 acceptance evidence or silently rebound to a later revision.

## Preserved dataset identity

The S10 record identifies `sift_base.fvecs` as 516,000,000 bytes with SHA-256
`21f66e2975057b5728ba56de1c825bac4f4d89d596609ae985741c6242631816`
and `sift_query.fvecs` as 5,160,000 bytes with SHA-256
`f7fc9be140accdfd64116c2fa2365ecdb69b8f084970c6b0532db5ff79ac8fdc`.
Dataset bytes remain external and must not be committed or redistributed.

## Decisions required before implementation

1. Approve the S15 experiment question and whether it is a formal acceptance
   run or another exploratory study.
2. Approve query coverage, candidate prefixes, variants, timing boundaries,
   hypotheses, and aggregate acceptance rule. Do not inherit S10 choices merely
   because code already exists.
3. Define matching-revision correctness, environment/telemetry, provenance,
   and evidence-retention requirements using the current S14 integrity controls
   where applicable.
4. Decide whether any V4/batch work is excluded. Primary single-query evidence
   and optional batch evidence must remain separately identifiable.

## Resume point

After Owner approval, prepare a clean exact-revision worktree, verify the
external dataset path and hashes without changing the environment, then audit
the S10 loader/orchestrator/analyzer against the approved S15 design before any
implementation or benchmark execution. No S15 command is authorized or claimed
by this handoff.

