# Repository Agent Instructions

Scope: the repository root and all descendants unless a more specific `AGENTS.md` adds compatible instructions.

## Mission and sources

Build and evaluate GPU-accelerated exhaustive exact nearest-neighbor search using CUDA. Single query is primary, batch query is secondary, V0-V3 are core, and V4 is optional stretch work.

Use the sources for their assigned responsibilities:

- [FinalProjectSpecification-v1.0.md](docs/specifications/FinalProjectSpecification-v1.0.md), its approved S6 numerical addendum and policy carrier define project behavior and numerical contracts.
- [S6ApprovedImplementationDesign-v1.0.md](docs/design/S6ApprovedImplementationDesign-v1.0.md) and the approval records define the approved implementation design and its conditions.
- [SourceOfTruth.md](docs/governance/SourceOfTruth.md) and [DecisionRegister.md](docs/governance/DecisionRegister.md) define authority, change control, approved decisions, and unresolved decisions.
- [AcceptanceCriteria.md](docs/requirements/AcceptanceCriteria.md) and [RequirementsTraceability.md](docs/requirements/RequirementsTraceability.md) define gates and evidence mappings.
- Repository configuration defines executable build, test, benchmark, and CI mechanics. It does not amend a contract.

Treat implementation or configuration that disagrees with a canonical source as drift. Do not silently resolve an undecided contract or change historical decisions.

## Lean roles

- **Owner:** prepares the worktree and exclusively performs all Git and GitHub mutations, including branch/worktree changes, staging, commits, pushes, pull requests, approvals, merges, tags, and ref cleanup.
- **Codex:** within an Owner-requested scope, inspects, edits, builds, tests, debugs, and repeats that local development loop without asking for approval at every step. The initial scoped change request is the authorization for ordinary repository-local edits and repository-supported build/test commands needed to finish it.
- **ChatGPT:** reviews material correctness or design risks and returns findings. Review is read-only and is required when a change alters an approved algorithm/design boundary, numerical behavior, acceptance logic, public interface, evidence interpretation, or other material contract-sensitive behavior. Routine in-design fixes do not require a separate review stage.

Codex must stop and ask before expanding scope, changing a canonical contract, installing dependencies, changing the environment, taking a destructive action, or performing any Git/GitHub mutation. A failing build or test is not a new approval gate: diagnose and fix it when the fix remains within scope.

## Development loop

For implementation work:

1. Confirm the Owner-prepared worktree, requested scope, applicable contracts, current state, and authoritative commands.
2. Implement the smallest coherent change. Preserve unrelated work.
3. Run the smallest relevant build/test checks, diagnose failures, and iterate within scope.
4. Run broader required gates when available. Correctness must be accepted before performance is measured or claimed.
5. Request or perform a read-only ChatGPT review only for material correctness/design risk. Address findings within the original scope; escalate decisions that would change scope or contracts.
6. Hand the Owner a findings-first report with changed files, exact commands, results, failures/blocks, residual risks, and Git/PR work still to be performed by the Owner.

Use [`cuda-dev-workflow`](.agents/skills/cuda-dev-workflow/SKILL.md) for multi-stage coordination. Specialist skills are focused guidance, not mandatory agent handoffs or independent approval gates. Use only the guidance relevant to the requested outcome.

## Technical invariants

- Search is exhaustive exact nearest neighbor using squared Euclidean distance (L2²); equal computed distances select the smallest index.
- CPU/GPU production computation uses Float32. Float64 is the numerical reference and diagnostic path, not a production substitute or automatic acceptance mechanism.
- Validate argmin index, distance error, and numerical ambiguity separately.
- Reject NaN, infinity, `N = 0`, and `D = 0`; accept signed zero. Keep core squared distances finite in Float32.
- V0-V3 share the approved distance, input, exact-search, output, and tie-breaking contract. V4 is optional and cannot replace core evidence.
- Preserve seeded workloads, required coverage, timing boundaries, statistics, and environment reporting from canonical sources. Disable fast math in core benchmarks.
- Keep single-query/batch-query, kernel-only/end-to-end, Float32/Float64, and core/optional evidence separately identifiable.
- Do not invent a target, tolerance, near-tie threshold, verdict, hardware setting, algorithm, command, measurement, or environment fact.
- Never silently reinterpret or resolve UD-001 through UD-005; preserve their recorded history and current approved status.

## Evidence, blocked work, and Git safety

Discover commands from checked-in configuration or authoritative instructions. Report checks as `planned`, `passed`, `failed`, `skipped`, or `blocked`; only executed qualifying evidence is `passed`. Missing compatible CUDA/GPU support blocks only the affected GPU gate, not valid CPU or documentation work.

When a contract, tool, environment, hardware dependency, or authority prevents valid progress, report the exact condition, inspected evidence, affected gate, work that remains valid, smallest unblock action, and resume point. Do not weaken the criterion.

Never stage, commit, stash, rebase, switch/create/delete branches or worktrees, tag, push, create/update a pull request, approve, merge, or change repository settings. Read-only Git inspection is allowed. Follow [DevelopmentAndMergePolicy.md](docs/governance/DevelopmentAndMergePolicy.md); normal direct pushes to `main` remain prohibited, applicable CI remains required, and the Owner retains the merge decision.
