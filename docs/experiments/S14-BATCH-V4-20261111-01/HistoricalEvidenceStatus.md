# S14-BATCH-V4-20261111-01 Historical Evidence Status

Status: `historical-audit-limited`\
Experiment revision: `6d27104bee7328589cefce31aa1d5a8ac23d1f51`\
Experiment ID: `S14-BATCH-V4-20261111-01`\
Evidence location: `build-s14-clean-6d27104-sm86-x64/s14-final-02`\
Audit date: 2026-11-11 (Asia/Bangkok)\

## Valid historical evidence

- The correctness gate reports all 36 batch configurations passed for CPU
  batch, sequential Prepared V2, V4 batch, bitwise Float32 equivalence, and the
  Float64 reference; 384 Float64 query checks are reported.
- The 21,600 retained raw samples are complete and uniquely identified as 216
  distributions with 100 iterations each.
- Independent recalculation matched every reported median, percentile,
  deterministic bootstrap interval, speedup, outcome classification,
  throughput transform, and point-estimate break-even calculation.
- The experiment configuration, numerical-policy carrier, and frozen V2
  configuration hashes match their captured copies.

## Acceptance limitations

- Required one-second telemetry contains six gaps above two seconds, including
  a 140,987.805 ms gap from `2026-11-11T12:50:53.5153899+07:00` to
  `2026-11-11T12:53:14.5031949+07:00`.
- The historical status logic did not validate sampling continuity or complete
  interval coverage before writing `accepted-for-analysis`.
- The top-level `orchestration-command.txt` contains no reconstructable command.
- The prerequisite CTest command is recorded, but its output log, JUnit result,
  and standalone exit-status record were not preserved.
- No contemporaneous durable S14-specific design approval was bound to an
  exact design document.

The missing interval prevents conclusions about GPU processes, thermal state,
or other telemetry conditions during that interval. The evidence does not
identify the cause of the gap.

## Interpretation

The original artifacts remain unchanged and retain their historical schema and
values. Their numerical and statistical content may be analyzed with the
limitations above, but they do not support unconditional environment or
performance acceptance.

The prospective
[S14 batch benchmark design decision](../../design/S14BatchBenchmarkDesignDecision-v1.0.md)
does not retroactively approve or rewrite this experiment. A replacement run
requires separate Owner authorization after clean-revision verification.
