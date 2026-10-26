# Artifact requirements

Artifacts may be files or clearly labeled response sections. Use existing repository formats when present.

| Stage | Required artifact | Minimum evidence |
|---|---|---|
| Intake | Scope and state record | Request, repository state, instructions, contract locations, authorization boundary |
| Requirements | Requirements analysis | Source citations, invariants, acceptance criteria, conflicts, unresolved decisions |
| Design | Design decision record | Requirements mapping, alternatives, selected scope, risks, approval status |
| Authorization | Approval record | Exact authorized mutation scope and excluded actions |
| Implementation | Change record | Changed files, design mapping, deviations, unimplemented scope |
| Verification | Verification report | Exact commands, environment, GPU status, per-check outcome, retained evidence |
| Review | Review report | Reviewed diff or artifact, findings, contract references, residual risks |
| Documentation | Documentation record | Sources synchronized, changed files, unresolved knowledge gaps |
| Performance | Performance evidence | Correctness prerequisite, revision, environment, workload, required-hardware status, methodology, raw or traceable results, hypothesis outcome |
| Experiment reporting | Experiment report | Revision-bound inputs, commands, results, `supported`/`rejected`/`inconclusive` outcome, comparison limits, failures and blocks |
| Completion | Handoff | Passed, failed, blocked, skipped, risks, next-milestone readiness |

## Evidence rules

- `passed` requires executed evidence meeting an approved criterion.
- `failed` requires executed evidence violating a criterion or a concrete review finding.
- `blocked` means the check could not be validly executed or judged.
- `skipped` requires an explicit out-of-scope reason and never equals `passed`.
- GPU-required validation without an available compatible GPU is `blocked`.
- Required benchmark hardware unavailable while benchmarking is in scope is `blocked`; benchmarking out of scope is recorded and bypasses the performance stage without implying a pass.
- Performance acceptance requires prior correctness evidence for the same relevant revision and configuration.
- A rejected performance hypothesis is a valid experiment outcome when backed by executed evidence; it is not a failed or blocked workflow by itself.
- Planned commands and expected results must be labeled as plans, never execution evidence.
