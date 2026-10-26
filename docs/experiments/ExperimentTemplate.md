# Experiment Template

Status: Reusable template; contains no results\
Benchmark method: [BenchmarkProtocol.md](../performance/BenchmarkProtocol.md)\
Correctness method: [CorrectnessPlan.md](../verification/CorrectnessPlan.md)

> Replace every angle-bracket placeholder with observed evidence, `not applicable`, `unknown`, `unresolved`, or `blocked`. Never delete a required field merely because evidence is unavailable.

## 1. Identity and question

| Field | Entry |
|---|---|
| Experiment ID | `<unique ID>` |
| Title | `<short descriptive title>` |
| Date/time/timezone | `<observed>` |
| Author/operator | `<observed>` |
| Question | `<what is being tested>` |
| Predeclared hypothesis | `<specific hypothesis>` |
| Outcome interpretation | `<criteria for supported/rejected/inconclusive; do not invent an unapproved acceptance threshold>` |
| Scope | `<correctness / benchmark / profiling; single query primary or batch secondary>` |
| Variants | `<cpu-f32 / v0 / v1 / v2 / v3 / v4-optional>` |

## 2. Revision and working state

| Field | Entry |
|---|---|
| Repository | `<identifier/location>` |
| Revision | `<commit or approved immutable revision identifier>` |
| Branch/tag | `<observed or not applicable>` |
| Working-tree state | `<clean or exact description of changes>` |
| Relevant artifact/config hashes | `<observed>` |

If results cannot be tied to a revision and working state, mark reproducibility and any performance acceptance `blocked`.

## 3. Environment

| Field | Entry |
|---|---|
| Operating system/environment | `<observed>` |
| CPU / host memory | `<observed>` |
| GPU / memory | `<observed>` |
| GPU driver | `<observed>` |
| CUDA Toolkit | `<observed>` |
| Compiler(s) | `<observed>` |
| Build type and flags | `<observed>` |
| Fast math | `<disabled for core benchmark>` |
| GPU/runtime settings | `<observed; include clocks/power/concurrency if known>` |
| Timer/profiler | `<observed>` |

## 4. Workload and data

| Field | Entry |
|---|---|
| Workload class | `<single-query primary / batch-query secondary>` |
| `N` | `<value>` |
| `D` | `<value>` |
| Batch size | `<value or not applicable>` |
| Seed | `<value>` |
| Generator/configuration | `<exact reference>` |
| Edge-case IDs | `<IDs or not applicable>` |
| Float32 finite-distance check | `<evidence/status>` |
| Input artifact references | `<paths/hashes>` |

## 5. Correctness prerequisite

| Gate/observation | Status | Evidence |
|---|---|---|
| Input-domain behavior (AC-001) | `<passed/failed/blocked/skipped>` | `<reference>` |
| Distance/search/tie behavior (AC-002–AC-004) | `<status>` | `<reference>` |
| Argmin observation (AC-005) | `<status and observed indices>` | `<reference>` |
| Distance-error observation (AC-006) | `<status, metric, value, policy ID>` | `<reference>` |
| Ambiguity classification (AC-007) | `<status, rule ID, diagnostic>` | `<reference>` |
| Ambiguous-case treatment (AC-008) | `<status and policy ID>` | `<reference>` |
| Float32 finiteness (AC-009) | `<status>` | `<reference>` |
| V0–V3 common contract / coverage (AC-010–AC-011) | `<status>` | `<reference>` |
| Overall correctness | `<accepted/failed/blocked>` | `<report reference>` |

Float64 near-tie diagnostics are not automatic acceptance. If correctness is not accepted for the matching revision/configuration, state that performance acceptance is blocked.

## 6. Method and commands

### 6.1 Exact executed commands

```text
<command 1>
<command 2>
```

### 6.2 Configuration

```text
variant: <value>
build_configuration: <value>
launch_or_runtime_configuration: <observed value; not a universal requirement>
timing_class: <kernel-only / end-to-end / not applicable>
timing_boundary: <exact inclusion/exclusion statement>
transfer_treatment: <included/excluded and where>
preprocessing_treatment: <included/excluded and where>
allocation_and_reuse_treatment: <exact statement>
warmups: <10 for benchmark>
measured_repetitions: <100 for benchmark>
percentile_method: <observed method>
run_order: <observed>
```

## 7. Results

### 7.1 Correctness results

| Case/configuration | Variant | Argmin observation | Distance error | Ambiguity diagnostic | Gate status | Evidence |
|---|---|---|---|---|---|---|
| `<entry>` | `<entry>` | `<entry>` | `<entry>` | `<entry>` | `<entry>` | `<reference>` |

### 7.2 Performance results — single query (primary)

| Configuration | Variant | Timing class | Median | p5 | p95 | Units | Raw samples |
|---|---|---|---:|---:|---:|---|---|
| `<entry>` | `<entry>` | `<entry>` | `<measured>` | `<measured>` | `<measured>` | `<unit>` | `<reference>` |

### 7.3 Performance results — batch query (secondary)

| Configuration / batch size | Variant | Timing class | Median | p5 | p95 | Units | Raw samples |
|---|---|---|---:|---:|---:|---|---|
| `<entry or not evaluated>` | `<entry>` | `<entry>` | `<measured>` | `<measured>` | `<measured>` | `<unit>` | `<reference>` |

### 7.4 Optional V4

| Configuration | Timing class | Result | Evidence | Core-completion relevance |
|---|---|---|---|---|
| `<entry or not evaluated>` | `<entry>` | `<entry>` | `<reference>` | `optional; does not replace V0–V3` |

## 8. Analysis

- Comparable configurations: `<state why comparisons are valid>`
- Confounders/differences: `<list all observed>`
- Correctness relationship: `<confirm matching accepted evidence or state block>`
- Single-query conclusion: `<evidence-bounded interpretation>`
- Batch-query conclusion: `<separate evidence-bounded interpretation or not evaluated>`
- V4 conclusion: `<optional and separate, or not evaluated>`
- Unexpected results/anomalies: `<retain and explain; do not silently discard>`

## 9. Outcome and status

| Item | Entry |
|---|---|
| Hypothesis outcome | `<supported / rejected / inconclusive>` |
| Correctness status | `<accepted / failed / blocked>` |
| Performance acceptance status | `<accepted / failed / blocked / not in scope>` |
| Failed checks | `<list or none>` |
| Blocked checks | `<list exact missing conditions or none>` |
| Skipped checks | `<list and reason or none>` |
| Limitations | `<list>` |

A `rejected` hypothesis is a valid experiment outcome when evidence is complete. It does not mean the experiment failed.

## 10. Reproduction and evidence index

| Artifact | Location/reference | Integrity/hash if available |
|---|---|---|
| Source revision | `<reference>` | `<value>` |
| Environment manifest | `<reference>` | `<value>` |
| Build log/configuration | `<reference>` | `<value>` |
| Commands/log | `<reference>` | `<value>` |
| Dataset manifest | `<reference>` | `<value>` |
| Correctness report | `<reference>` | `<value>` |
| Raw benchmark samples | `<reference>` | `<value>` |
| Profiler output | `<reference or not applicable>` | `<value>` |

## 11. Next actions

- `<evidence-based next action>`
- `<smallest unblock action for each blocked item>`
- `<follow-up hypothesis, if any; requires its own predeclared interpretation>`
