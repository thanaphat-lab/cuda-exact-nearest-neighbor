# Routing matrix

Route by requested outcome, not incidental keywords. A narrow workflow completes through its own explicit state path and does not inherit CUDA implementation stages.

| Request outcome | Primary skill | State path | Must not silently do |
|---|---|---|---|
| Requirements-only analysis | `$requirement-analysis` | `intake -> requirements_analyzed -> complete` | Resolve an unapproved contract or continue into design |
| Design-only work | `$solution-design` | `intake -> design_approved -> complete` | Implement or redefine specification contracts |
| Documentation-only change | `$docs-knowledge` | `intake -> documentation_completed -> complete` | Enter implementation stages or edit without authorization |
| Review-only request | `$change-review` | `intake -> review_completed -> complete` | Modify reviewed files or enter implementation stages |
| Git/GitHub-only work | `$github-flow` | `intake -> github_work_completed -> complete` | Initialize a repo or mutate local/remote state without the applicable approval |
| Create or change CUDA kernels | `$cuda-kernel-engineering` | Approved design through implementation and verification | Expand beyond approved V0-V4 scope |
| Run correctness, build, or test checks | `$verification` | `implementation_completed -> verification_completed` | Treat missing required GPU as pass |
| Profile or benchmark CUDA behavior | `$cuda-performance` | `verification_completed -> performance_benchmarked` | Benchmark before correctness acceptance or without required hardware |
| Report a supported, rejected, or inconclusive hypothesis | `$cuda-experiment-reporter` | `performance_benchmarked -> experiment_reported` | Treat a rejected hypothesis as workflow failure or invent missing evidence |
| Review verified work when benchmarking is out of scope | `$change-review` | `verification_completed -> review_completed` | Imply performance was measured |
| Coordinate several lifecycle stages | `$cuda-dev-workflow` | Use [workflow-state.json](workflow-state.json) | Skip gates or collapse blocked into passed |

## Performance branches

- Benchmarking in scope: correctness accepted, required hardware available, benchmark executed, then record `supported`, `rejected`, or `inconclusive` before experiment reporting.
- Benchmarking out of scope: record `performance_benchmark_out_of_scope` and continue from verification to review without a performance claim.
- Required benchmark hardware unavailable: enter `blocked`, preserve the verification evidence, and resume from `verification_completed` when the hardware condition is resolved.

## Overlap rules

- Requirements define what must hold; design chooses how to satisfy it without redefining the specification.
- Kernel engineering implements only an approved design; performance evaluates rather than invents it.
- Verification establishes correctness status; change review assesses the change and evidence.
- Documentation records approved truth; it does not decide unresolved engineering policy.
- Experiment reporting packages supported, rejected, and inconclusive outcomes; it does not create benchmark evidence.
