---
name: github-flow
description: Inspect, plan, or execute Git and GitHub workflow for this CUDA project. Use for branch, commit, push, pull-request, or repository-status work; do not use for technical implementation or for creating a repository without explicit authorization.
---

# GitHub Flow

Handle repository workflow while keeping technical decisions with the relevant specialist.

## Inputs

- Requested Git or GitHub outcome.
- Current repository, branch, worktree, remotes, and authentication state.
- Exact authorization for local and external mutations.

## Workflow

1. Inspect status, diff, branch, remotes, and applicable repository instructions before proposing action.
2. Preserve unrelated user changes and identify overlapping edits.
3. Use existing repository conventions for branches, commits, and pull requests; do not invent them when absent.
4. Keep planned actions distinct from actions actually executed.
5. Report a missing Git repository as `blocked` for Git/GitHub operations. Do not initialize one unless explicitly authorized.

## Mutation boundaries

- Status, log, diff, and remote inspection are read-only.
- Branch creation, staging, committing, rebasing, stashing, tagging, and other local Git mutations require explicit authorization for that scope.
- Pushes, pull-request creation or updates, issue changes, releases, and other remote mutations require explicit approval immediately before execution.
- Never use destructive history or worktree operations merely to simplify the workflow.

## Output

Return repository evidence, proposed or executed actions, affected refs/files, approval status, failures or blocks, and the next safe action. Do not claim a remote action occurred without a successful tool result.
