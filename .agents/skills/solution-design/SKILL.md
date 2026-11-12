---
name: solution-design
description: Design implementation-ready solutions for the CUDA exact-nearest-neighbor project. Use after requirements are resolved to compare alternatives, preserve variant boundaries, and produce an approval-ready design without writing implementation code.
---

# Solution Design

Turn resolved requirements into an approval-ready design. Do not implement the design.

## Inputs

- Requirements analysis with authoritative sources and acceptance criteria.
- Current architecture and implementation constraints.
- Explicit scope for V0-V3 and, only when requested, optional V4.
- For V5 work, the Owner-approved optional/secondary Batch Optimization scope established by the November 12, 2026 V5-A audit.

Read [project-contracts.md](../cuda-dev-workflow/references/project-contracts.md) before making project-specific decisions. If a design depends on an unresolved numerical or optimization contract, return `blocked` rather than selecting one.

## Workflow

1. Trace every proposed design decision to a confirmed requirement.
2. Compare viable alternatives on correctness, complexity, testability, portability, and expected measurement needs without fabricating performance.
3. Keep single-query exact search primary and batch evaluation secondary.
4. Define interfaces, data flow, ownership, error handling, version boundaries, and verification obligations at the level supported by the evidence.
5. Separate V0-V3 core work from optional V4 and optional/secondary V5 Batch Optimization, without redefining historical V0-V4 or the primary single-query designation.
6. Record risks, open questions, and the approval decision needed before local implementation mutations.

Do not reopen the V5-A audit or require a new design approval merely because concrete APIs, kernel structure, launch organization, memory ownership, or test organization were not prescribed. Those are routine engineering choices when compatible with approved numerical and experimental contracts. They were not thereby historically approved, and no V5 design carrier, approval ID, or signed record may be invented. Escalate only a material scope, numerical, experimental-policy, acceptance, timing, confidence-interval, telemetry, or evidence-interpretation change.

## Mutations

Read-only by default. Creating or changing a design record requires an explicit documentation change request. Design approval does not authorize code changes, dependency changes, or Git/GitHub mutations.

## Output

When a new design decision is actually requested, produce a record with scope, requirement mapping, alternatives, selected approach, variant boundaries, verification plan, risks, unresolved items, and approval status. Use the design artifact requirements in [artifact-requirements.md](../cuda-dev-workflow/references/artifact-requirements.md). Routine V5 implementation does not require creating such a record.
