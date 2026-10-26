---
name: solution-design
description: Design implementation-ready solutions for the CUDA exact-nearest-neighbor project. Use after requirements are resolved to compare alternatives, define V0-V4 boundaries, and produce an approval-ready design without writing implementation code.
---

# Solution Design

Turn resolved requirements into an approval-ready design. Do not implement the design.

## Inputs

- Requirements analysis with authoritative sources and acceptance criteria.
- Current architecture and implementation constraints.
- Explicit scope for V0-V3 and, only when requested, optional V4.

Read [project-contracts.md](../cuda-dev-workflow/references/project-contracts.md) before making project-specific decisions. If a design depends on an unresolved numerical or optimization contract, return `blocked` rather than selecting one.

## Workflow

1. Trace every proposed design decision to a confirmed requirement.
2. Compare viable alternatives on correctness, complexity, testability, portability, and expected measurement needs without fabricating performance.
3. Keep single-query exact search primary and batch evaluation secondary.
4. Define interfaces, data flow, ownership, error handling, version boundaries, and verification obligations at the level supported by the evidence.
5. Separate V0-V3 core work from optional V4 and state exclusions.
6. Record risks, open questions, and the approval decision needed before local implementation mutations.

## Mutations

Read-only by default. Creating or changing a design record requires an explicit documentation change request. Design approval does not authorize code changes, dependency changes, or Git/GitHub mutations.

## Output

Produce a design decision record with scope, requirement mapping, alternatives, selected approach, V0-V4 boundaries, verification plan, risks, unresolved items, and approval status. Use the design artifact requirements in [artifact-requirements.md](../cuda-dev-workflow/references/artifact-requirements.md).
