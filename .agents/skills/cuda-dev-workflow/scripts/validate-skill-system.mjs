#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const expectedSkills = [
  "cuda-dev-workflow",
  "requirement-analysis",
  "solution-design",
  "github-flow",
  "verification",
  "change-review",
  "docs-knowledge",
  "cuda-kernel-engineering",
  "cuda-performance",
  "cuda-experiment-reporter",
];
const expectedSourceIds = [
  "repository_agent_instructions",
  "approved_project_specification",
  "approved_design_decisions",
  "repository_configuration",
  "current_user_authorization",
];
const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const skillsRoot = path.resolve(scriptDir, "..", "..");
const workflowDir = path.join(skillsRoot, "cuda-dev-workflow");
const referencesDir = path.join(workflowDir, "references");
const root = path.resolve(skillsRoot, "..", "..");
const phase = process.argv.includes("--orchestrator") ? "orchestrator" : "all";
const emitRuntimeScenarios = process.argv.includes("--emit-runtime-scenarios");
const checks = [];
let failureCount = 0;

function pass(name, detail = "") {
  checks.push({ status: "PASS", name, detail });
}

function fail(name, detail) {
  checks.push({ status: "FAIL", name, detail });
  failureCount += 1;
}

function read(relativePath) {
  return fs.readFileSync(path.join(root, relativePath), "utf8");
}

function readJson(relativePath) {
  try {
    return JSON.parse(read(relativePath));
  } catch (error) {
    fail(`json:${relativePath}`, error.message);
    return null;
  }
}

function sameSet(left, right) {
  return JSON.stringify([...new Set(left)].sort()) === JSON.stringify([...new Set(right)].sort());
}

function parseFrontmatter(content) {
  const match = content.match(/^---\r?\n([\s\S]*?)\r?\n---\r?\n/);
  if (!match) return null;
  const values = {};
  for (const line of match[1].split(/\r?\n/)) {
    const field = line.match(/^([a-zA-Z0-9_-]+):\s*(.+)$/);
    if (field) values[field[1]] = field[2].replace(/^['"]|['"]$/g, "").trim();
  }
  return values;
}

function walk(start, predicate = () => true) {
  const files = [];
  const stack = [start];
  while (stack.length) {
    const current = stack.pop();
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      const full = path.join(current, entry.name);
      if (entry.isDirectory()) stack.push(full);
      if (entry.isFile() && predicate(full)) files.push(full);
    }
  }
  return files.sort();
}

function validateSkills() {
  const actual = fs.readdirSync(skillsRoot, { withFileTypes: true })
    .filter((entry) => entry.isDirectory())
    .map((entry) => entry.name);
  if (!sameSet(actual, expectedSkills) || actual.length !== expectedSkills.length) {
    fail("inventory:focused-guidance", `expected ${expectedSkills.join(", ")}; got ${actual.join(", ")}`);
  } else {
    pass("inventory:focused-guidance", `${actual.length} skills`);
  }

  const errors = [];
  for (const name of expectedSkills) {
    const skillFile = path.join(skillsRoot, name, "SKILL.md");
    if (!fs.existsSync(skillFile)) {
      errors.push(`${name}: missing SKILL.md`);
      continue;
    }
    const content = fs.readFileSync(skillFile, "utf8");
    const frontmatter = parseFrontmatter(content);
    if (!frontmatter || frontmatter.name !== name) errors.push(`${name}: invalid frontmatter name`);
    if (!frontmatter?.description || frontmatter.description.length < 20 || frontmatter.description.length > 1024) {
      errors.push(`${name}: invalid description`);
    }
    if (/TODO|TBD|placeholder/i.test(content)) errors.push(`${name}: unfinished marker`);
  }
  errors.length ? fail("skills:manifests", errors.join("; ")) : pass("skills:manifests", `${expectedSkills.length} checked`);

  const implementationFiles = walk(skillsRoot, (file) => /\.(?:cu|cuh|cpp|cc|cxx|c)$/i.test(file));
  implementationFiles.length
    ? fail("skills:no-implementation", implementationFiles.map((file) => path.relative(root, file)).join(", "))
    : pass("skills:no-implementation");
}

function validateLinks() {
  const broken = [];
  let count = 0;
  for (const file of walk(skillsRoot, (candidate) => candidate.endsWith(".md"))) {
    const content = fs.readFileSync(file, "utf8");
    for (const match of content.matchAll(/\[[^\]]+\]\(([^)]+)\)/g)) {
      const target = match[1].split("#", 1)[0];
      if (!target || /^(?:https?:|#)/.test(target)) continue;
      count += 1;
      if (!fs.existsSync(path.resolve(path.dirname(file), target))) {
        broken.push(`${path.relative(skillsRoot, file)} -> ${target}`);
      }
    }
  }
  broken.length ? fail("skills:links", broken.join("; ")) : pass("skills:links", `${count} checked`);
}

function transition(workflow, id) {
  return workflow.transitions.find((item) => item.id === id);
}

function validateGraph(workflow) {
  const states = new Set(workflow.states);
  const terminals = new Set(workflow.terminal_states);
  const ids = new Set();
  const errors = [];
  if (!states.has(workflow.initial_state)) errors.push("unknown initial state");
  for (const terminal of terminals) if (!states.has(terminal)) errors.push(`unknown terminal ${terminal}`);
  for (const item of workflow.transitions) {
    if (!item.id || ids.has(item.id)) errors.push(`duplicate/missing transition id ${item.id ?? "<missing>"}`);
    ids.add(item.id);
    if (!states.has(item.from) || !states.has(item.to)) errors.push(`${item.id}: unknown state`);
    if (!item.actor || !item.route) errors.push(`${item.id}: missing actor or route`);
    if (!Array.isArray(item.requires) || !item.requires.length) errors.push(`${item.id}: no requirements`);
    if (!Array.isArray(item.produces) || !item.produces.length) errors.push(`${item.id}: no output`);
    if (terminals.has(item.from)) errors.push(`${item.id}: exits terminal state`);
  }

  const reachable = new Set([workflow.initial_state]);
  let changed = true;
  while (changed) {
    changed = false;
    for (const item of workflow.transitions) {
      if (reachable.has(item.from) && !reachable.has(item.to)) {
        reachable.add(item.to);
        changed = true;
      }
    }
  }
  for (const state of workflow.states) {
    if (state !== "blocked" && !reachable.has(state)) errors.push(`unreachable state ${state}`);
  }
  errors.length ? fail("workflow:graph", errors.join("; ")) : pass("workflow:graph", `${workflow.states.length} states, ${workflow.transitions.length} transitions`);
}

function validateLeanPolicy(workflow) {
  const errors = [];
  const ownerOwns = workflow.roles?.owner?.owns ?? [];
  const codexOwns = workflow.roles?.codex?.owns ?? [];
  const codexForbidden = workflow.roles?.codex?.forbidden ?? [];
  const chatgptForbidden = workflow.roles?.chatgpt?.forbidden ?? [];
  if (!ownerOwns.includes("git_mutations") || !ownerOwns.includes("github_mutations")) errors.push("Owner does not own Git/GitHub mutations");
  if (!codexForbidden.includes("git_mutations") || !codexForbidden.includes("github_mutations")) errors.push("Codex Git/GitHub prohibition missing");
  if (!chatgptForbidden.includes("git_mutations") || !chatgptForbidden.includes("repository_edits")) errors.push("ChatGPT read-only boundary missing");
  for (const capability of ["scoped_implementation", "build", "test", "debug_iteration"]) {
    if (!codexOwns.includes(capability)) errors.push(`Codex missing ${capability}`);
  }
  if (workflow.development_policy?.git_and_github_mutation_actor !== "owner") errors.push("exclusive mutation actor is not Owner");
  if (workflow.development_policy?.material_review_actor !== "chatgpt") errors.push("material review actor is not ChatGPT");
  for (const action of ["in_scope_edit", "build_retry", "test_rerun", "in_scope_failure_fix"]) {
    if (!workflow.development_policy?.repeat_approval_not_required_for?.includes(action)) errors.push(`repeat-approval exemption missing ${action}`);
  }

  const expectedTransitions = [
    ["start-scoped-work", "intake", "working", "codex"],
    ["iterate-in-scope", "working", "working", "codex"],
    ["validate-work", "working", "validated", "codex"],
    ["fix-validation-failure", "validated", "working", "codex"],
    ["request-material-review", "validated", "review_required", "chatgpt"],
    ["address-review-findings", "review_required", "working", "codex"],
    ["handoff-reviewed-work", "review_required", "ready_for_owner", "codex"],
    ["handoff-routine-work", "validated", "ready_for_owner", "codex"],
  ];
  for (const [id, from, to, actor] of expectedTransitions) {
    const item = transition(workflow, id);
    if (!item || item.from !== from || item.to !== to || item.actor !== actor) errors.push(`${id} missing or incorrect`);
  }
  const start = transition(workflow, "start-scoped-work");
  if (!start?.requires.includes("scoped_change_request") || start.requires.includes("explicit_local_mutation_authorization")) {
    errors.push("scoped request is not the single local authorization");
  }
  const routine = transition(workflow, "handoff-routine-work");
  if (routine?.condition?.material_correctness_or_design_risk !== false) errors.push("routine review bypass missing");
  if (!routine?.requires.includes("required_checks_acceptable")) errors.push("routine handoff can bypass unacceptable checks");
  const review = transition(workflow, "request-material-review");
  if (review?.condition?.material_correctness_or_design_risk !== true) errors.push("material review condition missing");
  if (!transition(workflow, "handoff-reviewed-work")?.requires.includes("required_checks_acceptable")) {
    errors.push("reviewed handoff can bypass unacceptable checks");
  }
  if (workflow.performance_policy?.requires_correctness_acceptance !== true) errors.push("correctness-before-performance missing");
  if (!sameSet(workflow.performance_policy?.hypothesis_outcomes ?? [], ["supported", "rejected", "inconclusive"])) {
    errors.push("performance outcomes changed");
  }
  for (const reason of ["missing_or_conflicting_contract", "scope_expansion_required", "canonical_contract_change_required", "required_gpu_unavailable", "required_benchmark_hardware_unavailable"]) {
    if (!workflow.blocked_transition?.requires_any?.includes(reason)) errors.push(`blocked reason missing ${reason}`);
  }
  errors.length ? fail("workflow:lean-role-policy", errors.join("; ")) : pass("workflow:lean-role-policy");
}

function validateSourceAuthority(workflow) {
  const responsibilities = workflow.source_authority?.responsibilities ?? [];
  const errors = [];
  const ids = responsibilities.map((entry) => entry.id);
  if (!sameSet(ids, expectedSourceIds) || ids.length !== new Set(ids).size) errors.push("source IDs differ");
  if (workflow.source_authority?.material_conflict_action !== "blocked") errors.push("material conflicts do not block");
  const contractAuthorities = responsibilities.filter((entry) => entry.defines_approved_project_contracts).map((entry) => entry.id);
  if (!sameSet(contractAuthorities, ["approved_project_specification"])) errors.push("specification is not sole contract authority");
  if (responsibilities.some((entry) => entry.may_redefine_approved_contracts !== false)) errors.push("a lower source may redefine contracts");
  const contracts = read(".agents/skills/cuda-dev-workflow/references/project-contracts.md");
  const documentedIds = [...contracts.matchAll(/^\| `([^`]+)` \|/gm)].map((match) => match[1]);
  if (!sameSet(documentedIds, expectedSourceIds)) errors.push("project-contract source IDs differ");
  errors.length ? fail("workflow:source-authority", errors.join("; ")) : pass("workflow:source-authority");
}

function scenarioStatus(scenario) {
  if (scenario.action === "git_commit") return scenario.actor === "owner" ? "ready" : "blocked";
  if (scenario.action === "read_only") return "ready";
  if (scenario.contracts_resolved === false) return "blocked";
  if (scenario.action === "expand_scope") return "blocked";
  if (scenario.action === "material_review") return scenario.actor === "chatgpt" && scenario.material_risk ? "ready" : "blocked";
  if (scenario.action === "handoff") {
    return scenario.checks_recorded && scenario.required_checks_acceptable ? "ready_for_owner" : "blocked";
  }
  if (scenario.action === "benchmark") {
    return scenario.correctness_accepted && scenario.required_hardware_available ? "ready" : "blocked";
  }
  if (["implement", "fix_and_rerun"].includes(scenario.action)) {
    const scopeOkay = scenario.action !== "fix_and_rerun" || scenario.within_scope === true;
    return scenario.actor === "codex" && scenario.owner_prepared_worktree && scenario.scoped_change_request && scopeOkay ? "ready" : "blocked";
  }
  return "blocked";
}

function validateScenarios(scenariosDocument) {
  const scenarios = scenariosDocument?.scenarios ?? [];
  const requiredIds = [
    "codex-scoped-implementation",
    "codex-debug-iteration",
    "codex-scope-expansion",
    "codex-contract-conflict",
    "codex-git-commit",
    "chatgpt-git-commit",
    "owner-git-commit",
    "chatgpt-material-review",
    "codex-routine-handoff",
    "codex-failed-check-handoff",
    "performance-before-correctness",
    "performance-hardware-unavailable",
    "performance-ready",
  ];
  const errors = [];
  const ids = scenarios.map((scenario) => scenario.id);
  for (const id of requiredIds) if (!ids.includes(id)) errors.push(`missing ${id}`);
  if (ids.length !== new Set(ids).size) errors.push("duplicate scenario ID");
  for (const scenario of scenarios) {
    const actual = scenarioStatus(scenario);
    if (actual !== scenario.expected_status) errors.push(`${scenario.id}: expected ${scenario.expected_status}, policy gives ${actual}`);
  }
  errors.length ? fail("workflow:policy-scenarios", errors.join("; ")) : pass("workflow:policy-scenarios", `${scenarios.length} checked`);
}

function validateRuntimeScenarios(runtime) {
  const scenarios = runtime?.scenarios ?? [];
  const errors = [];
  for (const scenario of scenarios) {
    if (!scenario.id || !scenario.prompt || !scenario.expected_role || !scenario.expected_status) errors.push(`${scenario.id ?? "<missing>"}: incomplete`);
    if (!Array.isArray(scenario.forbidden_actions) || !scenario.forbidden_actions.length) errors.push(`${scenario.id}: no forbidden actions`);
    if (scenario.expected_role !== "owner" && !scenario.forbidden_actions.includes("git_mutation")) errors.push(`${scenario.id}: agent Git boundary missing`);
  }
  errors.length ? fail("runtime-scenarios:structure", errors.join("; ")) : pass("runtime-scenarios:structure", `${scenarios.length} defined`);
}

function validateDocumentation() {
  const agent = read("AGENTS.md");
  const policy = read("docs/governance/DevelopmentAndMergePolicy.md");
  const workflowSkill = read(".agents/skills/cuda-dev-workflow/SKILL.md");
  const githubSkill = read(".agents/skills/github-flow/SKILL.md");
  const combined = [agent, policy, workflowSkill, githubSkill].join("\n");
  const required = [
    ["Owner-exclusive Git", /Owner[^\n]{0,100}exclusively[^\n]{0,160}Git/i],
    ["Codex continuous loop", /Codex[^\n]{0,160}(build|implementation)[^\n]{0,160}(test|debug)/i],
    ["no repeated approval", /(without (?:asking for|a )?(?:new |renewed )?approval|do not need repeated approval|does not need to be renewed)/i],
    ["ChatGPT material review", /ChatGPT[^\n]{0,160}material[^\n]{0,160}(correctness|design)/i],
    ["correctness before performance", /Correctness[^\n]{0,100}before performance/i],
    ["direct main push prohibited", /direct pushes? to `main`[^\n]{0,80}prohibited/i],
    ["CI retained", /CI[^\n]{0,100}(pass|required)/i],
  ];
  const errors = required.filter(([, expression]) => !expression.test(combined)).map(([name]) => name);
  for (const stale of ["implementation_authorized", "explicit_local_mutation_authorization"]) {
    if (combined.includes(stale)) errors.push(`stale gate ${stale}`);
  }
  errors.length ? fail("documentation:lean-consistency", errors.join("; ")) : pass("documentation:lean-consistency");
}

const workflow = readJson(".agents/skills/cuda-dev-workflow/references/workflow-state.json");
const scenarios = readJson(".agents/skills/cuda-dev-workflow/references/routing-scenarios.json");
const runtime = readJson(".agents/skills/cuda-dev-workflow/references/runtime-routing-scenarios.json");

if (workflow) {
  validateGraph(workflow);
  validateLeanPolicy(workflow);
  validateSourceAuthority(workflow);
}
validateScenarios(scenarios);
validateRuntimeScenarios(runtime);
validateDocumentation();
if (phase === "all") {
  validateSkills();
  validateLinks();
}

if (emitRuntimeScenarios && runtime) {
  console.log(JSON.stringify(runtime, null, 2));
} else {
  for (const check of checks) console.log(`${check.status} ${check.name}${check.detail ? ` - ${check.detail}` : ""}`);
  console.log(`SUMMARY phase=${phase} passed=${checks.filter((check) => check.status === "PASS").length} failed=${failureCount}`);
}
process.exitCode = failureCount ? 1 : 0;
