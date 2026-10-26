---
name: docs-knowledge
description: Discover, explain, or synchronize project documentation for the CUDA exact-nearest-neighbor repository. Use when approved requirements, designs, commands, results, or knowledge need documentation without silently changing project contracts.
---

# Documentation and Knowledge

Keep documentation aligned with approved truth and observable repository state.

## Inputs

- Requested documentation outcome and authorized files.
- Canonical specifications, approved design records, implementation evidence, and verified results.
- Current documentation structure and repository instructions.

Use [project-contracts.md](../cuda-dev-workflow/references/project-contracts.md) to resolve sources. Link to canonical facts instead of duplicating changing commands, versions, results, or hardware state across skills and documents.

## Workflow

1. Identify the authoritative source for each statement being explained or synchronized.
2. Separate approved facts, current observed state, historical results, and unresolved decisions.
3. Update downstream documentation only when explicitly requested and only after the source decision is approved.
4. Keep primary single-query and secondary batch-query claims distinct, and preserve Float32 computation versus Float64 reference roles.
5. Cite revisions and evidence for experiment or performance claims.

## Mutations

Read-only for lookup, explanation, audit, and documentation review. Editing documentation requires an explicit request. Never amend a canonical project specification, create repository infrastructure, or backfill missing decisions without authorization.

## Output

Return sources consulted, facts established, files changed or proposed, synchronization gaps, unresolved decisions, and status. Do not present unexecuted commands or unverified results as factual project knowledge.
