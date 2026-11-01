---
name: github-flow
description: Inspect Git/GitHub state and prepare handoff guidance for the Owner, who exclusively performs all branch, worktree, commit, push, pull-request, approval, and merge mutations.
---

# GitHub Flow

Inspect repository workflow while keeping every Git and GitHub mutation with the Owner.

## Inputs

- Requested Git or GitHub outcome.
- Current repository, branch, worktree, remotes, and authentication state.
- The Owner's requested read-only inspection or handoff outcome.

## Workflow

1. Inspect status, diff, branch, remotes, and applicable repository instructions before proposing action.
2. Preserve unrelated user changes and identify overlapping edits.
3. Use existing repository conventions to prepare exact Owner instructions; do not invent them when absent.
4. Keep recommended Owner actions distinct from read-only actions actually executed.
5. Report a missing Git repository as `blocked` for Git/GitHub operations and hand initialization to the Owner.

## Mutation boundaries

- Status, log, diff, show, and remote inspection are read-only and allowed within scope.
- Codex and ChatGPT never create, switch, or delete branches/worktrees; stage, commit, rebase, stash, or tag; push; create/update/approve/merge pull requests; change issues/releases/settings; or mutate refs.
- The Owner performs those actions under [DevelopmentAndMergePolicy.md](../../../docs/governance/DevelopmentAndMergePolicy.md).
- Never recommend destructive history or worktree operations merely to simplify the workflow.

## Output

Return repository evidence, affected refs/files, risks, and an Owner action list. Do not claim a mutation occurred; waiting for Owner Git work after successful local development is `ready_for_owner`, not `blocked`.
