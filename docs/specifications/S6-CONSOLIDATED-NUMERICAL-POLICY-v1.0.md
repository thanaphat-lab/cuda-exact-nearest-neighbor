# S6 Consolidated Numerical Policy v1.0 — Consolidated owner-approved decisions (checksum ratification pending)

**Policy ID:** `S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0`  
**Status:** Source decisions approved in conversation; this newly assembled immutable carrier awaits owner confirmation of exact file checksum; canonical synchronization pending; runtime correctness not verified  
**Approval date:** 2026-10-28 (Asia/Bangkok)  
**Approving role:** Project Owner  
**Baseline commit:** `ad6487ef58343a78a54039fc95fd4e577f189e34`

**Approval scope:** Owner completed four explicit reviews in the conversation on 2026-10-28. This standalone file consolidates the reviewed numerical formulae and the final proposed AC replacement clauses copied into the approval-candidate report. It is a new normalized approved-text carrier, not a byte-authenticated copy of historical R3 material. Its SHA-256 is recorded in the companion provenance record, but the owner has not yet specifically ratified the hash of this newly assembled file. This is NOT authorization to change repository files, run tools on the target machine, or implement code.

## 1. N01–N03 — Arithmetic graphs and environment

Inputs `x_ij` and `q_j` are finite binary32 values. For each candidate `i`, traverse dimensions `j = 1, …, D` in strictly ascending order.

**Production CPU Float32 / CUDA V0–V3:**

\[
s_{ij}^{32}=\operatorname{RN}_{32}(x_{ij}-q_j),\qquad a_{i0}^{32}=+0,
\]
\[
a_{ij}^{32}=\operatorname{RN}_{32}\left(\operatorname{fma}(s_{ij}^{32},s_{ij}^{32},a_{i,j-1}^{32})\right),\qquad d_i^{32}=a_{iD}^{32}.
\]

**Independent Float64 diagnostic reference:** binary32 input operands are promoted *exactly* to binary64 before subtraction.

\[
s_{ij}^{64}=\operatorname{RN}_{64}(x_{ij}-q_j),\qquad a_{i0}^{64}=+0,
\]
\[
a_{ij}^{64}=\operatorname{RN}_{64}\left(\operatorname{fma}(s_{ij}^{64},s_{ij}^{64},a_{i,j-1}^{64})\right),\qquad d_i^{64}=a_{iD}^{64}.
\]

Both graphs require round-to-nearest ties-to-even (RNE), correctly rounded fused FMA, gradual underflow, FTZ/DAZ disabled for relevant nodes, no reassociation, excess precision, non-fused substitution, or order changes; all required intermediates finite. `1 ≤ D < 2^24`. A different graph fails conformance even if its output happens to fall within the distance error bound. Runtime/compiler verification is mandatory *later*, before correctness acceptance.

## 2. N04–N05 / UD-001 — Derived distance error bound

Define

\[
u_{32}=2^{-24},\quad u_{64}=2^{-53},\quad \eta_{32}=2^{-150},\quad
\gamma_k^{(p)}=\frac{k u_p}{1-k u_p}.
\]
\[
E_{32}(D)=(1+u_{32})^2(1+\gamma_D^{(32)})-1,
\quad E_{64}(D)=(1+u_{64})^2(1+\gamma_D^{(64)})-1.
\]
\[
R(D)=\frac{E_{32}(D)+E_{64}(D)}{1-E_{64}(D)},
\qquad A(D)=D\eta_{32}(1+\gamma_D^{(32)}).
\]

**Every candidate** in every required correctness case must satisfy

\[
\boxed{\left|d_i^{32}-d_i^{64}\right|\le A(D)+R(D)d_i^{64}.}
\]

Set `Q_i = sum_j (x_ij - q_j)^2` in exact real/dyadic arithmetic. Normal binary32 subtraction uses a relative unit-roundoff model. Differences of distinct finite binary32 operands are exact multiples of `2^-149`, with nonzero magnitude at least `2^-149`; the nonzero-subnormal-difference path is not assigned an unjustified relative underflow model. For each Float32 FMA node write

\[
a_{ij}^{32}=\left(a_{i,j-1}^{32}+(s_{ij}^{32})^2\right)(1+\delta_{ij})+\rho_{ij}.
\]

- Normal-rounded node: `|δ_ij| ≤ u32`, `ρ_ij = 0`.
- **Subnormal-or-zero underflow-rounded node:** `δ_ij = 0`, `|ρ_ij| ≤ 2^-150` including a *positive exact tiny value rounded to +0*.
- Count `S_i` includes such zero-underflow-rounded nodes. Conservatively `S_i ≤ D`; no tolerance escalation is allowed.

Consequently the approved proof obligation is

\[
|d_i^{32}-Q_i|\le E_{32}(D)Q_i+D\eta_{32}(1+\gamma_D^{(32)}).
\]

The smallest positive nonzero binary32 difference squared is `2^-298`, normal in binary64, permitting

\[
|d_i^{64}-Q_i|\le E_{64}(D)Q_i,
\qquad Q_i\le\frac{d_i^{64}}{1-E_{64}(D)}.
\]

The boxed candidate bound follows by triangle inequality. Assumptions must be verified for actual implementations. For an exact input match, including signed-zero numeric equality, `Q_i = 0` implies `d_i^32 = d_i^64 = +0` exactly. Bound comparisons require exact dyadic rational evaluation or certified outward-safe interval evaluation; inconclusive evidence is blocked, not passed. All-candidate bound compliance does not replace bit/graph conformance.

## 3. N06–N07 / UD-002 — Ranking intervals and conservative boundary

Use the separately versioned ranking policy with the same proved numerical radius:

\[
B_i^{rank}=A(D)+R(D)d_i^{64},\quad L_i=d_i^{64}-B_i^{rank},\quad U_i=d_i^{64}+B_i^{rank},
\]
\[
U_*=\min_k U_k,\qquad\boxed{C=\{i:L_i\le U_*\}}.
\]

All `B,L,U,d` quantities have **squared-distance units**; `u, gamma, E, R` are dimensionless. Boundary equality is included (`≤`). If `m` is the Float32 lexicographic minimizer and `k` minimizes `U`, then, given candidate containment,

\[
L_m\le d_m^{32}\le d_k^{32}\le U_k=U_*,
\]

so `m ∈ C`.

A certified outward evaluator must enclose endpoints:

\[
\underline L_i\le L_i\le\overline L_i,\quad
\underline U_i\le U_i\le\overline U_i,
\]
\[
\underline U_*=\min_i\underline U_i,\qquad
\overline U_*=\min_i\overline U_i.
\]

Define

\[
\boxed{\widehat C=\{i:\underline L_i\le\overline U_*\}},\qquad C\subseteq\widehat C.
\]

- **Confirmed contender:** `upper(L_i) ≤ lower(U_*)`.
- **Confirmed non-contender:** `lower(L_i) > upper(U_*)`.
- **Boundary-uncertain:** all other cases; retain in `Ĉ` when its conservative predicate holds.

Outward intervals must be sound for *all arithmetic operations and endpoint comparisons*, not just individually rounded stored values. Conservative set inclusion is NOT proof of exact contender membership or exact ambiguity.

**Binding Owner Policy A — Conservative Qualified Acceptance:** A returned index in verified `Ĉ` may receive `accepted-ambiguous` while exact boundary membership remains unresolved **only if every separate correctness gate has passed**. Record `contender_provenance=conservative-outward`, `exact_ambiguity=unknown`, and boundary uncertainty. A known exact singleton disagreement is a failure, not an ambiguous pass. This is an intentional Acceptance Contract Amendment.

## 4. N08–N10 / UD-003 — Deterministic verdict semantics

**Four NN verdict tokens:** `accepted-unambiguous`, `accepted-ambiguous`, `failed`, `blocked/unverified`.

For *contract-invalid* inputs (NaN, infinity, `N=0`, `D=0`), report `input_outcome=expected-rejection-pass|expected-rejection-fail|expected-rejection-blocked` as appropriate, and **`nn_verdict=not-applicable`**. Signed zero is valid input.

For contract-valid inputs, apply the precedence **demonstrated failure > missing/blocked mandatory evidence > accepted classification**:

| Order | Condition | NN verdict / outcome |
|---:|---|---|
| 1 | Graph, arithmetic-environment, finite-core, unsafe-interval, or UD-001 numerical defect demonstrated | `failed` |
| 2 | Candidate omitted, wrong Float32 lexicographic argmin, smallest-index tie, reduction or production path defect demonstrated | `failed` |
| 3 | Exact `C` known and returned index outside it, or proven exact singleton disagreeing with returned index | `failed` |
| 4 | Conservative path used but returned index outside valid `Ĉ` | `failed` |
| 5 | No demonstrated defect but domain eligibility or mandatory evidence absent, unresolved or invalid | `blocked/unverified` |
| 6 | Proven valid input outside approved numerical core domain | `blocked/unverified` with separate domain status; not expected invalid-input rejection |
| 7 | All gates pass, exact `C` singleton, returned index agrees with Float64 reference, no unresolved boundary | `accepted-unambiguous` |
| 8 | All gates pass, exact `C` contains multiple candidates, returned index belongs to exact `C` | `accepted-ambiguous` with exact provenance |
| 9 | All gates pass, exact membership unresolved, verified outward `Ĉ` includes returned index | `accepted-ambiguous` with `contender_provenance=conservative-outward`, `exact_ambiguity=unknown`, boundary uncertainty |

Every accepted result requires independently verified Float32 arithmetic graph for every candidate, all-candidate UD-001 bound compliance, exhaustive candidate coverage, true Float32 lexicographic minimum `(computed squared distance, index)`, tie/reduction fixtures and matching production-path evidence. Float64 argmin index agreement or mere contender membership alone never grants acceptance. Track mathematical exact, Float64 computed, and Float32 computed argmin/tie classifications independently.

Domain outcomes: valid input outside approved numerical domain is `valid-outside-approved-domain`; a certified required finite-core arithmetic overflow is `failed`; unknown core eligibility is `blocked/unverified`; valid outside-domain overflow may be reported diagnostically as `overflow-observed-outside-core-contract` without inventing an expected rejection requirement.

## 5. Verification, reports, and benchmark boundaries

- Fast independent Float32 graph/argmin validator checks every candidate in the required 12 N×D matrix: `N={1024,16384,262144,1048576}`, `D={8,32,128}`.
- Exact-dyadic stress oracle handles mathematical ties, subnormal/zero underflow, boundary-sensitive cases and proof checks; it need not process every candidate in every large core case.
- Verify CPU production baseline and each CUDA V0–V3 actual production path, plus independent direct reduction fixtures, tie cases and diagnostic modes; do not substitute diagnostic-only evidence for production verification.
- Minimum evidence: `input_contract_outcome`, `domain_status`, `arithmetic_status`, graph/bound/ranking/verdict version IDs, all-candidate error checks, exact-zero assertions, exact and conservative contender indices, boundary-uncertain indices, provenance, exact ambiguity, Float32 lexicographic status, Float64 index agreement, separate mathematical/F64/F32 tie labels, final ordered verdict and revision/build/environment/configuration references.
- Do not include validator, stress-oracle, report-generation or diagnostic overhead in production kernel-only or end-to-end benchmark timings.
- Correctness gate completion precedes performance acceptance for the **entire applicable matrix** and matching revision, build, working tree, arithmetic environment, workload and variant. `accepted-ambiguous` may satisfy AC-012 only under approved full-evidence Policy A.

## 6. Canonical Acceptance Contract Amendment — reviewed replacement clauses, awaiting new-file checksum ratification

The four proposed **After** clauses below are the replacement wording reviewed and approved in Owner Review 3/4, later included in the full-policy Owner Review 4/4. These are **normative replacement proposals** and not yet edits to the repository.

### AC-005 — Argmin validation

**Acceptance:** “For every contract-valid case in the approved numerical domain, the returned implementation pair is independently verified as the lexicographic minimum of `(computed Float32 squared distance, candidate index)` over every candidate. The implementation index is compared separately with the Float64 reference index and classified against the approved exact contender set `C`, or against the verified conservative set `Ĉ` only when exact boundary membership remains unresolved. Float64 index agreement alone is not acceptance. A disagreement when the exact contender set is a singleton fails.”

**Required evidence:** “Per-case implementation index and distance; independent all-candidate Float32 graph and lexicographic verification; Float64 reference index and relevant distances; Float32, Float64, and mathematical tie fields; exact contender set or unresolved marker; conservative contender set when applicable; contender provenance; boundary-uncertain indices; UD-001 status; and independent NN verdict.”

**Dependencies/status:** “Depends on AC-004, AC-006, AC-007, and AC-008 and the approved UD-001–UD-003 policy versions. Invalid-input outcomes remain governed by AC-001 and receive no NN verdict.”

### AC-007 — Numerical ambiguity classification

**Acceptance:** “For every applicable candidate, compute the approved ranking radius `B_i^rank`, endpoints `L_i` and `U_i`, `U_* = min_i U_i`, and the exact contender predicate `L_i ≤ U_*`. Boundary equality is included. Exact arithmetic shall be used where practical; otherwise verified outward intervals shall classify candidates as confirmed contenders, confirmed non-contenders, or boundary-uncertain and shall construct `Ĉ = {i : lower(L_i) ≤ upper(U_*)}`. Conservative inclusion or boundary uncertainty shall not be reported as proven exact membership or exact ambiguity.”

**Required evidence:** “Policy identifier and version; `D`; each relevant `d_i^64`, radius, endpoint, and comparison; exact or outward-evaluation method; exact contender set when resolved; conservative set; boundary-uncertain indices; `exact_ambiguity=true|false|unknown`; and traceable comparison results.”

**Dependencies/status:** “Uses the independently versioned UD-002 ranking policy and requires valid UD-001 per-candidate containment and safe arithmetic. An unsafe implemented interval is failed; an otherwise valid but unresolved outward comparison follows AC-008 Policy A or is blocked when its mandatory evidence is incomplete.”

### AC-008 — Ambiguous-case acceptance

**Acceptance:** “Apply the approved ordered UD-003 verdict policy. `accepted-unambiguous` requires every applicable correctness gate to pass, an exact singleton contender set, agreement with the Float64 reference index, and no unresolved boundary condition. `accepted-ambiguous` requires every applicable correctness gate to pass and either (a) an exact multi-candidate contender set containing the returned index, or (b) verified outward intervals whose conservative set contains the returned index while exact boundary membership remains unresolved. Case (b) shall record conservative provenance, `exact_ambiguity=unknown`, and boundary uncertainty. Exact or conservative contender membership alone never grants acceptance. Demonstrated defects are `failed`; absent mandatory evidence without a demonstrated defect is `blocked/unverified`.”

**Required evidence:** “UD-003 policy identifier; input/domain outcome; returned index; Float32 lexicographic correctness; Float64 index agreement; UD-001 verdict; exact and conservative contender fields; exact ambiguity; outward-boundary uncertainty; exhaustive-candidate, tie, graph, oracle, reduction, and production-path statuses; and an ordered decision trace.”

**Dependencies/status:** “Depends on AC-003–AC-007, AC-009–AC-011, and matching revision/configuration evidence. Invalid-input rejection is reported separately under AC-001 with `nn_verdict=not-applicable`.”

### AC-012 — Correctness-before-performance

**Acceptance:** “Performance acceptance is evaluated only after the complete applicable correctness matrix is accepted for the same revision and working-tree state, build configuration, workload, arithmetic environment, and implementation variant. Both `accepted-unambiguous` and qualified `accepted-ambiguous` cases may satisfy this prerequisite only when issued under the approved UD-003 policy with all mandatory evidence complete and no required gate failed or blocked. One accepted case does not satisfy the matrix-level prerequisite.”

**Required evidence:** “Linked revision-bound correctness report identifying every applicable case and variant; per-case NN verdict and contender provenance; gate matrix with no required failed or blocked entries; exact revision, working-tree state, build configuration, workload, and arithmetic-environment identifiers.”

**Dependencies/status:** “Requires approved and synchronized UD-001–UD-003 policies and accepted AC-001–AC-011 evidence as applicable. Exploratory measurements remain non-accepted unless separately authorized and labeled.”

## 7. Policy version manifest and governance boundary

Top-level approved policy: `S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0`. Subpolicy identifiers for documentation planning (not independently source-hashed): `S6-NUM-GRAPH-v1.0`, `S6-NUM-ENV-v1.0`, `S6-UD001-DIST-BOUND-v1.0`, `S6-UD002-RANK-INTERVAL-v1.0`, `S6-NUM-TIE-LABELS-v1.0`, `S6-UD003-QCA-v1.0`, `S6-NUM-EVIDENCE-v1.0`.

UD-001, UD-002 and UD-003 have owner-approved *policy decisions* but still require canonical synchronization; no correctness tests have passed. UD-004 is open. UD-005 is partially resolved **only for arithmetic accumulation graph**; API, data layout, ownership, launch configuration and reduction architecture remain open. Future repository documentation synchronization requires a separate explicit owner authorization **after** read-only proposed-diff review. Runtime probes, builds, benchmarks, tests, Git/GitHub operations and CPU/CUDA implementation remain unauthorized.
