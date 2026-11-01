# Routing matrix

Route by outcome. Skills are guidance within one task, not approvals or required agent handoffs.

| Outcome | Guidance | Lean path | Boundary |
|---|---|---|---|
| Scoped code or CUDA change | `$cuda-dev-workflow` with `$cuda-kernel-engineering` and `$verification` guidance | `intake -> working -> validated -> ready_for_owner` | Codex iterates implementation/build/test/debug without renewed approval |
| Material correctness/design risk | `$change-review` | `validated -> review_required -> ready_for_owner`, or back to `working` for findings | ChatGPT review is read-only and conditional |
| Requirements or contract analysis | `$requirement-analysis` | `intake -> complete` | Never resolve an unapproved contract |
| Design analysis | `$solution-design` | `intake -> complete` | Review material decisions; routine in-design work needs no new design artifact |
| Documentation change | `$docs-knowledge` | `intake -> working -> validated -> ready_for_owner` | The scoped request authorizes the edit; preserve approved meaning |
| Read-only review or audit | Relevant specialist | `intake -> complete` | No mutations |
| Performance work | `$cuda-performance` and optionally `$cuda-experiment-reporter` | Within `working`, after correctness; then `validated` | No performance acceptance before correctness |
| Git, GitHub, or merge work | `$github-flow` | Owner-only, outside the Codex state machine | Codex/ChatGPT may inspect but never mutate |

## Material review triggers

Require ChatGPT review when work changes numerical behavior, an approved algorithm or design boundary, acceptance logic, a public interface, evidence interpretation, or another contract-sensitive behavior. Review is not required solely because files were edited, tests failed and were fixed, or more than one specialist's guidance was consulted.

## Blocked versus handoff

- Missing or conflicting contracts, required unavailable tooling/hardware, scope expansion, environment mutation, and external dependencies can block the affected work.
- A failed check normally returns to `working` for an in-scope fix.
- Completed local work waiting for Owner Git/PR actions is `ready_for_owner`, not `blocked`.
