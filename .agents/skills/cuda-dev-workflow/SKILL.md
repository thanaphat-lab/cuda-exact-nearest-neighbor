---
name: cuda-dev-workflow
description: Coordinate a lean scoped development loop for the CUDA exact-nearest-neighbor project while preserving canonical contracts, correctness-first evidence, material review, and Owner-only Git operations.
---

# CUDA Development Workflow

Coordinate one continuous local development loop. Specialist skills provide focused guidance; they are not mandatory handoffs or separate approval gates.

## Intake

1. Confirm the Owner-prepared worktree and use read-only Git inspection to identify state.
2. Read applicable instructions, canonical contracts, approved design decisions, current implementation, and checked-in commands.
3. Record the requested scope, exclusions, acceptance evidence, and material-risk triggers.
4. Enter `blocked` if a required contract conflicts or is missing. Do not invent a decision.

The Owner's scoped change request authorizes ordinary repository-local edits plus checked-in build, test, validation, benchmark, and debug commands needed to complete that scope. It does not authorize scope expansion, contract changes, dependency installation, environment changes, destructive actions, or Git/GitHub mutations.

For the optional/secondary V5 Batch Optimization experiment, the November 12, 2026 V5-A audit and Owner approval already authorize implementation within the approved scope. Do not repeat the audit or seek renewed approval only because concrete APIs, kernel structure, or other routine engineering details were not prescribed. Codex may select and document such details when compatible with approved numerical and experimental contracts; this does not make them historically approved. Do not invent a V5 design carrier, approval ID, or signed record. V5 does not redefine V0-V4 or the primary single-query designation.

## Lean development loop

Use [workflow-state.json](references/workflow-state.json) as the compact policy model:

`intake -> working -> validated -> ready_for_owner`

Codex owns `working`: implementation, build, test, diagnosis, and in-scope fixes may repeat without a new approval or specialist handoff. A failing check routes back to the same working loop. Preserve exact commands and results.

Correctness acceptance for the same relevant revision and configuration is required before performance benchmarking or a performance claim. Required unavailable hardware blocks only the affected gate. A hypothesis may be `supported`, `rejected`, or `inconclusive`.

After validation:

- Route material correctness or design risk to a read-only ChatGPT review. Findings return to the same working loop when the fix is in scope.
- Bypass a separate review stage for routine changes already covered by approved contracts and design.
- Hand the final diff and evidence to the Owner. Codex and ChatGPT never perform Git or GitHub mutations.

For V5, Codex may implement and debug the approved work, including non-performance correctness and integrity test code and permitted non-GPU execution. Codex must not execute GPU diagnostics, GPU tests, preflight, or formal benchmarks; the Owner must authorize and execute those separately. Changes to material scope, numerical behavior, experimental policy, acceptance, timing boundaries, confidence intervals, telemetry, or evidence interpretation require Owner review. Preserve all applicable CPU baselines and correctness gates.

Use [routing-matrix.md](references/routing-matrix.md) for bounded tasks and [project-contracts.md](references/project-contracts.md) for source responsibilities. Use [artifact-requirements.md](references/artifact-requirements.md) for the final evidence packet.

## Stop conditions

Stop and report `blocked` when valid progress requires a missing/conflicting contract, a material scope expansion, a new dependency or environment mutation, a destructive action, an unavailable required tool/hardware gate, or an external dependency. State the exact condition, valid completed work, smallest unblock action, and resume state.

Do not treat Git handoff as blocked. `ready_for_owner` is the successful Codex terminal state; the Owner then decides and performs branch, commit, pull-request, CI, and merge actions under repository policy.
