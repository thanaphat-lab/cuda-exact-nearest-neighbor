# S6 Design Approval Record

Status: Approved design-decision provenance; runtime unverified
Approval ID: `S6-DESIGN-OWNER-APPROVAL-001`
Approving role: Project Owner
Approval date: Not captured in supplied approval evidence
Approved design carrier:
[S6ApprovedImplementationDesign-v1.0.md](../design/S6ApprovedImplementationDesign-v1.0.md)
Synchronization baseline: `e848668e48a3ea119bd9d032c0be3c1ed37dfb71`

## 1. Approved sources

The Owner approval identifies:

1. `S6 Final Implementation Design Review Package v1.0`;
2. `S6 Final Design Refinement & Owner Decision Package v1.1`;
3. COND-01 — V3 Warp Participation;
4. COND-02 — Nonfinite Valid Candidates;
5. COND-03 — Independent Tuning.

The reports were approved by title and version. No source checksum was supplied
or is claimed. Both reports state baseline
`e848668e48a3ea119bd9d032c0be3c1ed37dfb71`.

## 2. Approval scope

The approval covers:

- the UD-004 tuning method and candidates `{128,256,512}`;
- correctness-gated independent tuning and frozen final configuration;
- resource-calculation and future evidence obligations;
- the UD-005 API, typed outcome and ownership model;
- CPU Float32 and independent Float64 designs;
- AoS V0 and SoA V1–V3;
- initial host-side AoS-to-SoA conversion;
- V0/V1 serial-device-reducer architecture;
- V2 shared-memory hierarchy;
- V3 safe-source full-physical-warp hierarchy;
- distinct handling of logical tail records and real nonfinite candidates;
- raw one-shot, prepared-reuse and kernel-only timing boundaries;
- independent validator, oracle, reduction and production-path evidence;
- deferral of optional V4;
- Owner-only Squash & Merge into `main`.

## 3. Controlling conditions

COND-01 requires full physical warps, common participation masks, active logical
tail lanes with `valid=false`, no early exits, active shuffle sources,
full-width pair information and smallest-index ties throughout the hierarchy.

COND-02 requires two logically independent associative operations:

1. optional finite lexicographic argmin over completed finite distances;
2. aggregation of nonfinite/domain-failure status across every real candidate.

A real nonfinite candidate remains logically present, contributes failure
status through every reduction stage and never enters the ordinary floating
minimum comparator. Only missing tail records are logically invalid.
Finite-core nonfinite arithmetic is a correctness failure; outside-domain
evidence remains distinct. Applicable failure status prevents reporting a
successful accepted NN result.

The approval does not establish a mandatory failing-candidate index-selection
rule. OR-aggregated presence is status evidence only and does not replace
independent exhaustive candidate-coverage verification.

COND-03 requires predeclared candidates, independent tuning/final seeds and
samples, correctness qualification, kernel-only median selection, smaller block
on exact measured ties, frozen configuration, correctness re-execution and no
retrospective tuning from final measurements.

## 4. Source-binding strategy

This record does not claim exact-file ratification of the conversational
reports. The normalized design carrier is accepted into the repository only
through a separately authorized Owner-reviewed documentation PR.

The durable canonical binding is:

- this approval ID and source manifest;
- the reviewed carrier path and content;
- the Project Owner's Squash & Merge action;
- the resulting observed `main` squash-commit SHA.

Future implementation authorization must cite that post-merge SHA.

## 5. Decision status

- UD-004: design method approved; hardware-specific values unverified.
- UD-005: V0–V3 architecture approved; runtime evidence unverified.
- V4: deferred and optional.

## 6. Authorization boundary

Design approval and this record do not mark any runtime gate passed and do not
authorize repository or runtime mutation.

Documentation editing, branch creation, staging, commit, push, PR creation,
implementation, dependency/environment change, build, test, hardware probe,
profiling and benchmark execution each require separate applicable
authorization.

The Project Owner alone performs Squash & Merge into `main`. Codex may not
merge, enable auto-merge or submit the PR to an automatic merge mechanism.
Branch deletion remains an Owner decision.
