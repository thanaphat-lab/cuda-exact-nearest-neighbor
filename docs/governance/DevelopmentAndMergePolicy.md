# Development and Merge Policy

Status: Approved repository governance policy\
Milestone: S5.5-B\
Effective date: 2026-10-27 (Asia/Bangkok)

## 1. Purpose and control boundaries

This policy governs normal development and integration for `thanaphat-lab/cuda-exact-nearest-neighbor`. It does not change the numerical, algorithmic, dataset, optimization, or acceptance contracts in the [final project specification](../specifications/FinalProjectSpecification-v1.0.md), and it does not resolve UD-001 through UD-005.

The repository uses three distinct control layers:

- **Documented policy:** the human and agent obligations in this file. These rules apply even when GitHub cannot enforce them.
- **Automated CI checks:** the checks implemented by the [Governance workflow](../../.github/workflows/governance.yml). A passing run is required where this policy says applicable, but CI does not replace review.
- **Server-enforced controls:** GitHub repository settings, branch protection, and rulesets. The private repository's current plan does not make branch protection or rulesets available, so `main` remains unprotected and the documented controls require human compliance.

## 2. Roles and normal change flow

The Owner exclusively performs Git and GitHub mutations: creating or changing branches and worktrees, staging, committing, pushing, opening or updating pull requests, approving, merging, tagging, and deleting refs. Codex and ChatGPT may use read-only Git commands to inspect state and diffs but must hand all mutations back to the Owner.

1. The Owner prepares a focused worktree from the current approved `main` revision. Use `feature/<topic>` for product or implementation work, `fix/<topic>` for corrections, and `docs/<topic>` for documentation or governance work.
2. The Owner gives Codex a bounded change request. That request authorizes the repository-local edit, build, test, and debug loop within scope; individual file edits, build attempts, test reruns, and in-scope fixes do not need repeated approval.
3. Codex inspects the applicable contracts, implements the smallest coherent change, runs authoritative checks, fixes in-scope failures, and reports exact evidence. It stops for scope expansion, contract changes, dependency or environment changes, destructive actions, or any Git/GitHub mutation.
4. ChatGPT performs read-only review when a change presents material correctness or design risk. Material risks include changes to numerical behavior, approved algorithm/design boundaries, acceptance logic, public interfaces, or evidence interpretation. Routine changes already covered by the approved design do not require a separate ChatGPT handoff.
5. The Owner reviews the resulting diff, evidence, risks, and any ChatGPT findings; stages and commits only the intended files; pushes the focused branch; and opens a pull request against `main`.
6. Normal direct pushes to `main` are prohibited. All applicable CI checks must pass on the exact pull-request revision to be merged.
7. A human who accepts responsibility for the merge decision must explicitly approve. Agent review and automation are evidence, not substitutes for that decision.
8. The Owner merges only with GitHub's squash option. Merge commits and rebase merges remain disabled, and branch cleanup remains a separate deliberate action.

## 3. Review responsibilities

Codex must provide the Owner a clear purpose, exact changed-file list, risk assessment, linked requirements or decisions, and reproducible validation evidence. It must distinguish executed checks from planned, skipped, failed, or blocked checks. The Owner is responsible for the final commit and pull-request presentation.

The human reviewer must:

- confirm that the change stays within its approved scope and does not silently alter canonical contracts;
- review the actual diff and material generated or configuration effects;
- verify that concerns are resolved or explicitly dispositioned;
- check that required correctness evidence precedes any performance claim;
- confirm that applicable CI passed for the intended head revision; and
- make the final approve/request-changes decision personally.

The merger must verify that the approved head revision and successful CI evidence still match, use squash merge, and avoid modifying or deleting unrelated refs.

## 4. Correctness and performance evidence

Correctness is required before performance optimization is accepted. When implementation exists, pull requests must report relevant CPU Float32, GPU Float32, and Float64-reference evidence separately and must keep argmin, distance-error, and numerical-ambiguity results distinct. Unresolved numerical decisions remain blocked rather than receiving a local default.

Performance evidence is required only when a performance claim or optimization is in scope. It must follow the canonical benchmark protocol, identify the tested revision and working-tree state, record the environment and seeded workload, and preserve raw or traceable results. Single-query versus batch-query and kernel-only versus end-to-end results must remain separately identifiable. A missing benchmark is never described as a pass.

## 5. Secrets, generated artifacts, and repository hygiene

- Never commit credentials, tokens, private keys, local authentication files, or real secret values.
- Do not commit generated binaries, compiler intermediates, generated datasets, or raw profiler output that belongs in ignored paths.
- Keep full-SHA action pinning and least-privilege workflow permissions.
- Review staged paths explicitly before every commit and run the repository hygiene and whitespace checks.
- If sensitive material is exposed, stop normal processing, notify the repository owner, revoke or rotate the credential outside the repository, and follow an owner-approved remediation plan. Deleting a later commit does not remove a secret from history.

The Governance workflow applies this whitespace comparison policy: a pull request checks every change from the merge base of its target-base commit through its head commit; a push to `main` checks the event's before-to-after range; and a manual dispatch checks the complete tracked tree at the dispatched revision against Git's empty tree. The job fails rather than skipping validation when required commits are unavailable or checkout history is shallow.

## 6. Failed CI and overrides

Failed, cancelled, absent, or pending required CI blocks merge. Diagnose and repair the branch through a new, reviewed change; do not bypass a check, rewrite evidence, force push, or push directly to `main` to obtain a green state.

An override is exceptional and requires explicit repository-owner authorization recorded in the pull request before merge. The record must identify the failed or unavailable check, risk, compensating evidence, approver, and follow-up action. An override cannot waive the canonical project specification or resolve UD-001 through UD-005.

## 7. Emergency hotfix exception

The repository owner is the decision owner for an emergency hotfix exception. Direct modification of `main` is allowed only when the owner explicitly declares an emergency and authorizes the exact change because the normal pull-request path cannot address an urgent repository-impacting failure.

The change record must state the emergency, scope, approver, commands or actions performed, validation evidence, and any controls bypassed. As soon as practical, the owner must require a retrospective human review and a follow-up pull request or governance record that reconciles documentation, tests, and branch state. The exception does not authorize force pushes, secret exposure, contract changes, or unrelated work.

## 8. Lean agent authorization boundaries

An Owner-requested change in an Owner-prepared worktree authorizes Codex to make the in-scope repository edits and to run checked-in build, test, validation, benchmark, and debug commands needed for that task. This authorization persists through ordinary failure/fix/rerun iterations; it does not need to be renewed at each workflow stage.

Codex must preserve unrelated work and stop for a material scope expansion, canonical contract change, dependency installation, environment or repository-infrastructure change, destructive action, unavailable required gate, or missing external dependency. ChatGPT review is read-only. Neither Codex nor ChatGPT may stage, commit, stash, rebase, switch or create branches/worktrees, tag, push, create or update pull requests, approve, merge, delete refs, publish releases, or change repository settings. Those mutations belong exclusively to the Owner.

The Owner's Git responsibility does not weaken technical gates: applicable CI, human review, squash-only integration, correctness-before-performance, and canonical S6/S7 contracts remain mandatory.

## 9. Manual-control limitation

GitHub currently reports branch protection and repository rulesets as unavailable for this private repository under the current plan. Consequently, the prohibition on normal direct pushes to `main`, mandatory human review, and required passing CI are policy controls rather than server-enforced gates. Squash-only merge availability is server configured, but an administrator or sufficiently privileged account may still alter repository settings or write directly to an unprotected branch.

Until server enforcement becomes available and is separately approved, every contributor and automation operator must follow this policy, and the human merger must re-check the pull request revision, CI conclusion, merge method, repository visibility, and merge settings immediately before merge.
