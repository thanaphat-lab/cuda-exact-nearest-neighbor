# Artifact requirements

Keep one compact evidence packet for a scoped change; do not manufacture a separate artifact for every skill consulted.

## Owner handoff packet

| Section | Minimum evidence |
|---|---|
| Scope | Owner request, worktree/branch observed read-only, in-scope and excluded work, applicable contracts |
| Change | Exact files changed, behavior/design mapping, preserved invariants, deviations or unimplemented scope |
| Checks | Exact commands, environment/configuration needed to interpret them, and per-check `passed`/`failed`/`skipped`/`blocked` status |
| Correctness | Relevant CPU Float32, GPU Float32, Float64-reference, argmin, distance-error, and ambiguity evidence kept distinct |
| Performance, when in scope | Prior correctness status, revision/worktree identity, hardware/workload/method, traceable results, and `supported`/`rejected`/`inconclusive` hypothesis outcome |
| Review, when material | ChatGPT findings, dispositions, unresolved material risks, and evidence rerun after fixes |
| Handoff | Residual risks, failures/blocks, files for Owner review, and Owner-only Git/PR/CI actions remaining |

Read-only requirements, design, documentation, or review tasks may return only the relevant sections.

## Evidence rules

- `passed` requires executed evidence meeting an approved criterion.
- `failed` requires executed evidence violating a criterion or a concrete review finding.
- `blocked` means the check could not be validly executed or judged; state the smallest unblock action.
- `skipped` requires an explicit out-of-scope reason and never equals `passed`.
- A failed build/test normally returns to the in-scope Codex debug loop; it is not a new approval gate.
- GPU-required validation without a compatible GPU and required benchmarking without required hardware are blocked only for the affected gate.
- Performance acceptance requires prior correctness evidence for the same relevant revision and configuration.
- A rejected performance hypothesis is a valid experiment outcome when backed by executed evidence.
- Planned commands and expected results are plans, never execution evidence.
