# Skill inventory

| Skill | Responsibility | Primary artifact | Mutation default |
|---|---|---|---|
| `cuda-dev-workflow` | State, gates, routing, handoff | Workflow status and completion report | Read-only coordination |
| `requirement-analysis` | Requirements and conflicts | Requirements analysis | Read-only |
| `solution-design` | Approved implementation approach | Design decision record | Read-only |
| `github-flow` | Git and GitHub workflow | Repository action report | Read-only; explicit approval for mutations |
| `verification` | Correctness/build/test evidence | Verification report | Read-only except disposable test output |
| `change-review` | Diff and artifact review | Findings-first review | Read-only |
| `docs-knowledge` | Documentation discovery and sync | Documentation record | Read-only; edits by explicit request |
| `cuda-kernel-engineering` | Authorized CUDA kernel changes | Change record | Explicitly authorized files only |
| `cuda-performance` | Profiling and benchmarking | Performance evidence | Read-only except disposable measurement output |
| `cuda-experiment-reporter` | Evidence synthesis | Revision-bound experiment report | Read-only; report edits by explicit request |
