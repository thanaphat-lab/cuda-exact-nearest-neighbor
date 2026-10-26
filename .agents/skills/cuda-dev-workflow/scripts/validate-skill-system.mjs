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
const originalScenarioIds = [
  "multi-stage-feature",
  "requirements-conflict",
  "design-with-resolved-contracts",
  "kernel-without-approval",
  "kernel-without-design",
  "gpu-verification-unavailable",
  "review-only",
  "docs-audit",
  "performance-before-correctness",
  "experiment-report",
  "push-without-approval",
  "push-with-approval",
  "review-edit-without-authorization",
  "docs-edit-without-authorization",
  "repo-init-without-authorization",
];
const expectedSourceIds = [
  "repository_agent_instructions",
  "approved_project_specification",
  "approved_design_decisions",
  "repository_configuration",
  "current_user_authorization",
];
const phase = process.argv.includes("--orchestrator") ? "orchestrator" : "all";
const emitRuntimeScenarios = process.argv.includes("--emit-runtime-scenarios");
const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const skillsRoot = path.resolve(scriptDir, "..", "..");
const workflowDir = path.join(skillsRoot, "cuda-dev-workflow");
const referencesDir = path.join(workflowDir, "references");
const errors = [];
const checks = [];
const pass = (name) => checks.push({ name, status: "PASS" });
const fail = (name, detail) => {
  checks.push({ name, status: "FAIL", detail });
  errors.push(`${name}: ${detail}`);
};

function readJson(file) {
  try {
    return JSON.parse(fs.readFileSync(file, "utf8"));
  } catch (error) {
    fail(`json:${path.relative(skillsRoot, file)}`, error.message);
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

function validateSkill(name) {
  const skillFile = path.join(skillsRoot, name, "SKILL.md");
  if (!fs.existsSync(skillFile)) return fail(`skill:${name}`, "missing SKILL.md");
  const content = fs.readFileSync(skillFile, "utf8");
  const frontmatter = parseFrontmatter(content);
  if (!frontmatter) return fail(`skill:${name}`, "missing YAML frontmatter");
  if (frontmatter.name !== name) return fail(`skill:${name}`, `frontmatter name is ${frontmatter.name ?? "missing"}`);
  const allowedKeys = new Set(["name", "description", "license", "allowed-tools", "metadata"]);
  const unexpected = Object.keys(frontmatter).filter((key) => !allowedKeys.has(key));
  if (unexpected.length) return fail(`skill:${name}`, `unexpected frontmatter keys: ${unexpected.join(", ")}`);
  if (!frontmatter.description || frontmatter.description.length < 20) return fail(`skill:${name}`, "description is missing or not discriminating");
  if (frontmatter.description.length > 1024 || /[<>]/.test(frontmatter.description)) return fail(`skill:${name}`, "description violates official length or character constraints");
  if (!/^[a-z0-9-]{1,64}$/.test(name) || name.startsWith("-") || name.endsWith("-") || name.includes("--")) return fail(`skill:${name}`, "invalid skill name");
  if (/TODO|TBD|\[TODO|placeholder/i.test(content)) return fail(`skill:${name}`, "unfinished scaffold marker found");
  pass(`skill:${name}`);
}

function validateInventory() {
  const actual = fs.readdirSync(skillsRoot, { withFileTypes: true }).filter((entry) => entry.isDirectory()).map((entry) => entry.name).sort();
  const expected = [...expectedSkills].sort();
  JSON.stringify(actual) === JSON.stringify(expected)
    ? pass("inventory:ten-skill-architecture")
    : fail("inventory:ten-skill-architecture", `expected ${expected.join(", ")}; got ${actual.join(", ")}`);

  const disallowed = [];
  const stack = [skillsRoot];
  while (stack.length) {
    const current = stack.pop();
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      const full = path.join(current, entry.name);
      if (entry.isDirectory()) stack.push(full);
      if (entry.isFile() && /\.(cu|cuh|cpp|cc|cxx|c)$/i.test(entry.name)) disallowed.push(path.relative(skillsRoot, full));
    }
  }
  disallowed.length
    ? fail("scope:no-cuda-or-cpu-implementation", disallowed.join(", "))
    : pass("scope:no-cuda-or-cpu-implementation");
}

function validateMarkdownLinks(root) {
  const stack = [root];
  const broken = [];
  let count = 0;
  while (stack.length) {
    const current = stack.pop();
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      const full = path.join(current, entry.name);
      if (entry.isDirectory()) stack.push(full);
      if (!entry.isFile() || !entry.name.endsWith(".md")) continue;
      const content = fs.readFileSync(full, "utf8");
      for (const match of content.matchAll(/\[[^\]]+\]\(([^)]+)\)/g)) {
        const target = match[1];
        if (/^(https?:|#)/.test(target)) continue;
        count += 1;
        if (!fs.existsSync(path.resolve(path.dirname(full), target.split("#")[0]))) broken.push(`${path.relative(skillsRoot, full)} -> ${target}`);
      }
    }
  }
  broken.length ? fail("cross-references", broken.join("; ")) : pass(`cross-references:${count}`);
}

function validateGraph(workflow) {
  const states = new Set(workflow.states);
  const terminals = new Set(workflow.terminal_states);
  const allTransitions = [...workflow.transitions, ...(workflow.blocked_transitions ?? [])];
  const ids = new Set();
  const structuralErrors = [];

  if (!states.has(workflow.initial_state)) structuralErrors.push("initial state is unknown");
  for (const terminal of terminals) if (!states.has(terminal)) structuralErrors.push(`terminal state ${terminal} is unknown`);
  for (const transition of allTransitions) {
    if (!transition.id || ids.has(transition.id)) structuralErrors.push(`missing or duplicate transition id ${transition.id ?? "<missing>"}`);
    ids.add(transition.id);
    if (!states.has(transition.from) || !states.has(transition.to)) structuralErrors.push(`${transition.id} uses an unknown state`);
    if (!expectedSkills.includes(transition.route)) structuralErrors.push(`${transition.id} uses unknown route ${transition.route}`);
    if (!Array.isArray(transition.requires) || !transition.requires.length) structuralErrors.push(`${transition.id} has no requirements`);
    if (!Array.isArray(transition.produces) || !transition.produces.length) structuralErrors.push(`${transition.id} has no outputs`);
  }
  for (const transition of workflow.transitions) if (terminals.has(transition.from)) structuralErrors.push(`${transition.id} exits terminal state ${transition.from}`);
  structuralErrors.length ? fail("workflow:transition-integrity", structuralErrors.join("; ")) : pass("workflow:transition-integrity");

  const reachable = new Set([workflow.initial_state]);
  let changed = true;
  while (changed) {
    changed = false;
    for (const transition of workflow.transitions) {
      if (reachable.has(transition.from) && !reachable.has(transition.to)) {
        reachable.add(transition.to);
        changed = true;
      }
    }
  }
  const unreachable = workflow.states.filter((state) => state !== "blocked" && !reachable.has(state));
  unreachable.length ? fail("workflow:state-reachability", unreachable.join(", ")) : pass("workflow:state-reachability");

  const canComplete = new Set(["complete"]);
  changed = true;
  while (changed) {
    changed = false;
    for (const transition of workflow.transitions) {
      if (canComplete.has(transition.to) && !canComplete.has(transition.from)) {
        canComplete.add(transition.from);
        changed = true;
      }
    }
  }
  const deadEnds = workflow.states.filter((state) => !terminals.has(state) && !canComplete.has(state));
  deadEnds.length ? fail("workflow:completion-paths", deadEnds.join(", ")) : pass("workflow:completion-paths");
}

function transitionById(workflow, id) {
  return [...workflow.transitions, ...(workflow.blocked_transitions ?? [])].find((transition) => transition.id === id);
}

function requireTransition(workflow, id, from, to, route, requiredTokens = []) {
  const transition = transitionById(workflow, id);
  if (!transition) return `${id} missing`;
  if (transition.from !== from || transition.to !== to || transition.route !== route) return `${id} has incorrect endpoints or route`;
  const missing = requiredTokens.filter((token) => !transition.requires.includes(token));
  return missing.length ? `${id} missing ${missing.join(", ")}` : null;
}

function validateWorkflowPolicies(workflow) {
  const gateErrors = [];
  gateErrors.push(requireTransition(workflow, "authorize-implementation", "design_approved", "implementation_authorized", "cuda-dev-workflow", ["explicit_local_mutation_authorization"]));
  gateErrors.push(requireTransition(workflow, "complete-implementation", "implementation_authorized", "implementation_completed", "cuda-kernel-engineering", ["approved_design_record", "authorization_record"]));
  gateErrors.push(requireTransition(workflow, "complete-verification", "implementation_completed", "verification_completed", "verification", ["correctness_accepted"]));
  const filteredGateErrors = gateErrors.filter(Boolean);
  filteredGateErrors.length ? fail("workflow:implementation-and-correctness-gates", filteredGateErrors.join("; ")) : pass("workflow:implementation-and-correctness-gates");

  const benchmarkErrors = [];
  benchmarkErrors.push(requireTransition(workflow, "complete-performance-benchmark", "verification_completed", "performance_benchmarked", "cuda-performance", ["correctness_accepted", "performance_benchmark_in_scope", "required_benchmark_hardware_available", "revision_bound_evidence", "executed_benchmark_evidence", "performance_hypothesis_outcome_recorded"]));
  const benchmarkTransitions = workflow.transitions.filter((transition) => transition.to === "performance_benchmarked");
  if (benchmarkTransitions.some((transition) => !transition.requires.includes("correctness_accepted"))) benchmarkErrors.push("a performance transition bypasses correctness acceptance");
  benchmarkErrors.push(requireTransition(workflow, "review-without-performance", "verification_completed", "review_completed", "change-review", ["performance_benchmark_out_of_scope", "verification_report"]));
  benchmarkErrors.push(requireTransition(workflow, "required-benchmark-hardware-unavailable", "verification_completed", "blocked", "cuda-performance", ["performance_benchmark_in_scope", "required_benchmark_hardware_unavailable"]));
  const hardwareBlock = transitionById(workflow, "required-benchmark-hardware-unavailable");
  if (hardwareBlock?.resume_to !== "verification_completed") benchmarkErrors.push("hardware block does not resume from verified state");
  if (workflow.performance_policy?.unavailable_required_hardware_status !== "blocked") benchmarkErrors.push("hardware-unavailable policy is not blocked");
  const filteredBenchmarkErrors = benchmarkErrors.filter(Boolean);
  filteredBenchmarkErrors.length ? fail("workflow:performance-branches", filteredBenchmarkErrors.join("; ")) : pass("workflow:performance-branches");

  const expectedOutcomes = ["supported", "rejected", "inconclusive"];
  const outcomeTransitions = workflow.transitions.filter((transition) => transition.from === "performance_benchmarked" && transition.to === "experiment_reported");
  const actualOutcomes = outcomeTransitions.map((transition) => transition.condition?.performance_hypothesis_outcome).filter(Boolean);
  const outcomeErrors = [];
  if (!sameSet(workflow.performance_policy?.hypothesis_outcomes ?? [], expectedOutcomes)) outcomeErrors.push("performance policy outcomes differ from required set");
  if (!sameSet(actualOutcomes, expectedOutcomes)) outcomeErrors.push("experiment transitions do not cover supported/rejected/inconclusive");
  if (outcomeTransitions.some((transition) => !transition.requires.includes("correctness_accepted") || !transition.requires.includes("revision_bound_evidence"))) outcomeErrors.push("an experiment outcome bypasses correctness or revision evidence");
  if (workflow.performance_policy?.rejected_hypothesis_is_valid_experiment_outcome !== true) outcomeErrors.push("rejected hypothesis is not marked valid");
  outcomeErrors.length ? fail("workflow:hypothesis-outcomes", outcomeErrors.join("; ")) : pass("workflow:hypothesis-outcomes");

  const narrowErrors = [];
  const narrowPaths = [
    ["complete-requirements-only", "requirements_analyzed", "complete", "cuda-dev-workflow", ["requirements_only_scope"]],
    ["approve-design-only-scope", "intake", "design_approved", "solution-design", ["design_only_scope", "resolved_required_contracts"]],
    ["complete-design-only", "design_approved", "complete", "cuda-dev-workflow", ["design_only_scope"]],
    ["complete-documentation-only-work", "intake", "documentation_completed", "docs-knowledge", ["documentation_only_scope", "documentation_authorization_satisfied"]],
    ["close-documentation-only-work", "documentation_completed", "complete", "cuda-dev-workflow", ["documentation_only_scope"]],
    ["complete-review-only-work", "intake", "review_completed", "change-review", ["review_only_scope", "no_mutations_performed"]],
    ["close-review-only-work", "review_completed", "complete", "cuda-dev-workflow", ["review_only_scope"]],
    ["complete-github-only-work", "intake", "github_work_completed", "github-flow", ["github_only_scope", "git_github_authorization_satisfied"]],
    ["close-github-only-work", "github_work_completed", "complete", "cuda-dev-workflow", ["github_only_scope"]],
  ];
  for (const [id, from, to, route, tokens] of narrowPaths) narrowErrors.push(requireTransition(workflow, id, from, to, route, tokens));
  const implementationTokens = new Set(["approved_design_record", "authorization_record", "change_record", "implementation_completed"]);
  for (const [id] of narrowPaths) {
    const transition = transitionById(workflow, id);
    if (transition?.requires.some((token) => implementationTokens.has(token))) narrowErrors.push(`${id} incorrectly requires implementation evidence`);
  }
  const filteredNarrowErrors = narrowErrors.filter(Boolean);
  filteredNarrowErrors.length ? fail("workflow:non-implementation-paths", filteredNarrowErrors.join("; ")) : pass("workflow:non-implementation-paths");

  const blockReasons = workflow.blocked_transition?.requires_any ?? [];
  ["missing_or_conflicting_contract", "missing_authorization", "required_gpu_unavailable", "required_benchmark_hardware_unavailable"].every((reason) => blockReasons.includes(reason))
    ? pass("workflow:blocked-reasons")
    : fail("workflow:blocked-reasons", "required blocked reason is missing");
}

function validateSourceAuthority(workflow) {
  const responsibilities = workflow.source_authority?.responsibilities ?? [];
  const ids = responsibilities.map((entry) => entry.id);
  const errorsFound = [];
  if (!sameSet(ids, expectedSourceIds) || ids.length !== new Set(ids).size) errorsFound.push("machine-readable source IDs are missing, extra, or duplicated");
  if (workflow.source_authority?.material_conflict_action !== "blocked") errorsFound.push("material conflicts are not blocked");
  const contractSources = responsibilities.filter((entry) => entry.defines_approved_project_contracts).map((entry) => entry.id);
  if (!sameSet(contractSources, ["approved_project_specification"])) errorsFound.push("approved specification is not the sole project-contract authority");
  if (responsibilities.some((entry) => entry.may_redefine_approved_contracts !== false)) errorsFound.push("a lower-level source may redefine approved contracts");

  const contractsDoc = fs.readFileSync(path.join(referencesDir, "project-contracts.md"), "utf8");
  const documentedIds = [...contractsDoc.matchAll(/^\| `([^`]+)` \|/gm)].map((match) => match[1]);
  if (!sameSet(documentedIds, expectedSourceIds)) errorsFound.push("documented source IDs contradict the machine policy");
  if (!/material[\s\S]{0,120}conflict[\s\S]{0,120}`blocked`/i.test(contractsDoc)) errorsFound.push("documentation does not require material conflicts to be blocked");

  errorsFound.length ? fail("source-authority:consistency", errorsFound.join("; ")) : pass("source-authority:consistency");
}

function validateDocumentedWorkflow(workflow) {
  const errorsFound = [];
  const skillDoc = fs.readFileSync(path.join(workflowDir, "SKILL.md"), "utf8");
  const orderMatch = skillDoc.match(/`([a-z-]+(?: -> [a-z-]+)+)`/);
  const documentedOrder = orderMatch ? orderMatch[1].split(" -> ") : [];
  if (JSON.stringify(documentedOrder) !== JSON.stringify(workflow.full_workflow_order)) errorsFound.push("documented full workflow order differs from machine policy");

  const routingDoc = fs.readFileSync(path.join(referencesDir, "routing-matrix.md"), "utf8");
  const edges = new Set(workflow.transitions.map((transition) => `${transition.from}->${transition.to}`));
  for (const match of routingDoc.matchAll(/`([a-z_]+(?: -> [a-z_]+)+)`/g)) {
    const states = match[1].split(" -> ");
    for (let index = 0; index < states.length - 1; index += 1) {
      const edge = `${states[index]}->${states[index + 1]}`;
      if (!edges.has(edge)) errorsFound.push(`documented edge ${edge} is absent from machine policy`);
    }
  }
  for (const outcome of workflow.performance_policy.hypothesis_outcomes) {
    if (!routingDoc.includes(`\`${outcome}\``)) errorsFound.push(`routing documentation omits ${outcome} outcome`);
  }
  if (!skillDoc.includes("runtime-routing-scenarios.json")) errorsFound.push("orchestrator does not disclose runtime scenarios");

  errorsFound.length ? fail("documentation:workflow-consistency", errorsFound.join("; ")) : pass("documentation:workflow-consistency");
}

function evaluateScenario(scenario) {
  const routes = {
    requirements: "requirement-analysis",
    design: "solution-design",
    github: "github-flow",
    verification: "verification",
    review: "change-review",
    docs: "docs-knowledge",
    kernel: "cuda-kernel-engineering",
    performance: "cuda-performance",
    report: "cuda-experiment-reporter",
  };
  const route = scenario.multi_stage ? "cuda-dev-workflow" : routes[scenario.intent];
  let status = "ready";
  if (!scenario.contract_available && ["requirements", "design", "kernel", "performance"].includes(scenario.intent)) status = "blocked";
  if (scenario.mutation_requested && !scenario.local_mutation_authorized) status = "blocked";
  if (scenario.intent === "kernel" && !scenario.approved_design) status = "blocked";
  if (scenario.gpu_required && !scenario.gpu_available) status = "blocked";
  if (scenario.intent === "performance" && !scenario.correctness_accepted) status = "blocked";
  if (scenario.external_mutation && !scenario.external_approval) status = "blocked";
  return { route, status };
}

function validateRoutingScenarios() {
  const data = readJson(path.join(referencesDir, "routing-scenarios.json"));
  if (!data) return;
  const ids = new Set();
  for (const scenario of data.scenarios ?? []) {
    if (ids.has(scenario.id)) fail(`scenario:${scenario.id}`, "duplicate id");
    ids.add(scenario.id);
    const actual = evaluateScenario(scenario);
    if (actual.route !== scenario.expected_route || actual.status !== scenario.expected_status) {
      fail(`scenario:${scenario.id}`, `expected ${scenario.expected_route}/${scenario.expected_status}, got ${actual.route}/${actual.status}`);
    } else pass(`scenario:${scenario.id}`);
  }
  const missingOriginal = originalScenarioIds.filter((id) => !ids.has(id));
  missingOriginal.length ? fail("scenario:preserved-v1.1-cases", missingOriginal.join(", ")) : pass("scenario:preserved-v1.1-cases");
}

function validateRuntimeScenarios() {
  const data = readJson(path.join(referencesDir, "runtime-routing-scenarios.json"));
  if (!data) return null;
  const errorsFound = [];
  const ids = new Set();
  if (data.execution?.fresh_session_per_scenario !== true) errorsFound.push("fresh session is not required per scenario");
  if (data.execution?.sandbox !== "read-only" || data.execution?.approval_policy !== "never") errorsFound.push("runtime probes are not constrained to read-only/no-approval execution");
  for (const scenario of data.scenarios ?? []) {
    if (!scenario.id || ids.has(scenario.id)) errorsFound.push(`missing or duplicate id ${scenario.id ?? "<missing>"}`);
    ids.add(scenario.id);
    if (!expectedSkills.includes(scenario.expected_primary_skill)) errorsFound.push(`${scenario.id} expects unknown skill`);
    if (!scenario.prompt || scenario.prompt.includes(scenario.expected_primary_skill)) errorsFound.push(`${scenario.id} is missing a prompt or leaks the expected skill name`);
    if (!/read-only (routing|route-only) probe/i.test(scenario.prompt ?? "")) errorsFound.push(`${scenario.id} is not explicitly read-only`);
    if (!Array.isArray(scenario.forbidden_actions) || !scenario.forbidden_actions.length) errorsFound.push(`${scenario.id} lacks forbidden actions`);
    if (!["ready", "blocked"].includes(scenario.expected_status)) errorsFound.push(`${scenario.id} has invalid expected status`);
  }
  errorsFound.length ? fail("runtime-scenarios:structure", errorsFound.join("; ")) : pass(`runtime-scenarios:structure:${ids.size}`);
  return data;
}

const workflow = readJson(path.join(referencesDir, "workflow-state.json"));
const runtimeData = validateRuntimeScenarios();

if (emitRuntimeScenarios) {
  if (errors.length) {
    for (const check of checks) console.error(`${check.status} ${check.name}${check.detail ? ` - ${check.detail}` : ""}`);
    process.exitCode = 1;
  } else {
    for (const scenario of runtimeData.scenarios) console.log(JSON.stringify(scenario));
  }
} else {
  for (const name of phase === "orchestrator" ? ["cuda-dev-workflow"] : expectedSkills) validateSkill(name);
  if (phase === "all") validateInventory();
  validateMarkdownLinks(phase === "orchestrator" ? workflowDir : skillsRoot);
  if (workflow) {
    validateGraph(workflow);
    validateWorkflowPolicies(workflow);
    validateSourceAuthority(workflow);
    validateDocumentedWorkflow(workflow);
  }
  if (phase === "all") validateRoutingScenarios();
  for (const check of checks) console.log(`${check.status} ${check.name}${check.detail ? ` - ${check.detail}` : ""}`);
  console.log(`SUMMARY phase=${phase} passed=${checks.filter((check) => check.status === "PASS").length} failed=${errors.length}`);
  process.exitCode = errors.length ? 1 : 0;
}
