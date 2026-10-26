# Project contracts

Use current repository evidence rather than memory. Approved project specifications remain the authority for numerical, optimization, and acceptance contracts; this Skill System neither replaces nor amends them.

## Responsibility boundaries

The sources below have different responsibilities rather than one interchangeable precedence list. Their backticked IDs must match `source_authority.responsibilities` in [workflow-state.json](workflow-state.json).

| Source ID | Responsibility | May establish | Must not do |
|---|---|---|---|
| `repository_agent_instructions` | Operational constraints, safety requirements, workflow triggers, and repository-local working conventions | How work is performed within its instruction scope | Redefine approved numerical, optimization, or acceptance contracts |
| `approved_project_specification` | Canonical project behavior and acceptance policy, including approved numerical and optimization contracts | What the project must do | Be changed implicitly by code, configuration, design notes, or task execution |
| `approved_design_decisions` | Implementation choices that satisfy the approved specification | How approved requirements will be implemented | Override or weaken the approved project specification |
| `repository_configuration` | Current executable build, test, benchmark, and tooling mechanics | Which checked-in commands and settings are actually available | Convert observed implementation state into a new requirement or silently resolve specification drift |
| `current_user_authorization` | Current task scope and permission for local or external mutations | Which otherwise-valid actions may be performed now | Redefine project contracts merely by authorizing work |

Safety and higher-priority agent instructions always govern what actions may be taken. Within project semantics, only the approved project specification defines the approved numerical, optimization, and acceptance contracts. Approved design decisions may specialize implementation choices only when consistent with that specification.

## Resolution rules

1. Identify the source responsible for each question; do not let a lower-level workflow document answer a project-contract question.
2. Use repository instructions for operational constraints, the approved specification for project contracts, approved design decisions for consistent implementation choices, repository configuration for executable mechanics, and the current request for authorization.
3. Treat observed code or configuration that disagrees with the approved specification as implementation drift, not as an automatic specification update.
4. Report every material cross-source conflict with the conflicting evidence and affected workflow gate. Enter `blocked` when the conflict prevents a valid in-scope decision.
5. Resume only after an authorized owner resolves the conflict in the responsible source. Do not silently choose the more convenient source.

Do not copy changing repository facts into a skill. Discover paths, commands, versions, branches, hardware, and results at execution time.

## Bootstrap invariants

These invariants come from the approved Skill System Specification V1.1 bootstrap request. They are routing and gate constraints, not a replacement for the detailed project specification:

- Primary workload: single-query exact nearest-neighbor search.
- Secondary workload: batch-query evaluation.
- CPU and GPU computation use Float32.
- The numerical reference uses Float64.
- V0 through V3 are the core optimization scope.
- V4 is optional stretch scope.
- Correctness must pass before performance is accepted.
- Experiments must be reproducible and tied to code revisions.

Do not infer unresolved numerical behavior, distance semantics, tie behavior, data constraints, optimization contents, or acceptance thresholds from these labels. Obtain those contracts from an approved project specification or record the dependent work as blocked.

## Missing contract behavior

When a required canonical file is missing, report the searched paths, preserve only confirmed bootstrap invariants, identify the decision needed for the current stage, avoid dependent implementation or acceptance claims, and provide the smallest unblock request. Absence is not permission for a design note, repository configuration, or user authorization to invent the missing contract.
