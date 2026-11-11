#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import process from "node:process";
import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(scriptDir, "..");
const requested = new Set(process.argv.slice(2));
const knownModes = new Set(["--links", "--traceability", "--governance", "--workflow", "--hygiene"]);
const selected = requested.size === 0 ? knownModes : requested;
const results = [];
let failureCount = 0;

for (const mode of selected) {
  if (!knownModes.has(mode)) {
    console.error(`Unknown mode: ${mode}`);
    process.exit(2);
  }
}

function pass(name, detail = "") {
  results.push({ status: "PASS", name, detail });
}

function fail(name, detail) {
  results.push({ status: "FAIL", name, detail });
  failureCount += 1;
}

function read(relativePath) {
  return fs.readFileSync(path.join(root, relativePath), "utf8");
}

function normalize(relativePath) {
  return relativePath.split(path.sep).join("/");
}

function walk(start, predicate = () => true) {
  if (!fs.existsSync(start)) return [];
  const skippedDirectories = new Set([".git", "build", "node_modules", ".workflow", ".cache"]);
  const files = [];
  const stack = [start];
  while (stack.length) {
    const current = stack.pop();
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      if (entry.isDirectory() && skippedDirectories.has(entry.name)) continue;
      const full = path.join(current, entry.name);
      if (entry.isDirectory()) stack.push(full);
      if (entry.isFile() && predicate(full)) files.push(full);
    }
  }
  return files.sort();
}

function validateLinks() {
  const broken = [];
  let checked = 0;
  // Archived evidence retains its original paths and bytes; its integrity is
  // verified by PACKET_MANIFEST.sha256 rather than live-document link checks.
  const immutableS14Evidence = path.join(root, "docs", "experiments", "S14-BATCH-V4-20261111-02", "evidence");
  for (const file of walk(root, (candidate) =>
    candidate.endsWith(".md") && !candidate.startsWith(`${immutableS14Evidence}${path.sep}`))) {
    const content = fs.readFileSync(file, "utf8");
    for (const match of content.matchAll(/!?(?:\[[^\]]*\])\(([^)]+)\)/g)) {
      let target = match[1].trim();
      if (target.startsWith("<") && target.includes(">")) target = target.slice(1, target.indexOf(">"));
      else target = target.split(/\s+["']/)[0];
      if (!target || /^(?:[a-z][a-z0-9+.-]*:|#)/i.test(target)) continue;
      const filePart = target.split("#", 1)[0];
      if (!filePart) continue;
      checked += 1;
      let decoded;
      try {
        decoded = decodeURIComponent(filePart);
      } catch {
        broken.push(`${normalize(path.relative(root, file))} -> ${target} (invalid URI encoding)`);
        continue;
      }
      const resolved = path.resolve(path.dirname(file), decoded);
      if (!fs.existsSync(resolved)) broken.push(`${normalize(path.relative(root, file))} -> ${target}`);
    }
  }
  broken.length ? fail("markdown-relative-links", broken.join("; ")) : pass("markdown-relative-links", `${checked} checked`);
}

function ids(content, expression) {
  return new Set([...content.matchAll(expression)].map((match) => match[0]));
}

function validateTraceability() {
  const specification = read("docs/specifications/FinalProjectSpecification-v1.0.md");
  const acceptance = read("docs/requirements/AcceptanceCriteria.md");
  const traceability = read("docs/requirements/RequirementsTraceability.md");
  const definedRequirements = ids(specification, /\bREQ-[A-Z]-\d{3}\b/g);
  const definedGates = ids(acceptance, /\bAC-\d{3}\b/g);
  const rows = traceability.split(/\r?\n/).filter((line) => /^\| REQ-[A-Z]-\d{3}\b/.test(line));
  const rowMap = new Map();
  const errors = [];

  for (const row of rows) {
    const columns = row.split("|").slice(1, -1).map((column) => column.trim());
    const requirement = columns[0]?.match(/REQ-[A-Z]-\d{3}/)?.[0];
    if (!requirement) continue;
    if (rowMap.has(requirement)) errors.push(`${requirement} has duplicate traceability rows`);
    rowMap.set(requirement, columns);
    const gateCell = columns[2] ?? "";
    const gates = [...gateCell.matchAll(/AC-\d{3}/g)].map((match) => match[0]);
    if (!gates.length && !/^All\b/i.test(gateCell)) errors.push(`${requirement} has no acceptance mapping`);
    for (const gate of gates) if (!definedGates.has(gate)) errors.push(`${requirement} references undefined ${gate}`);
  }

  for (const requirement of definedRequirements) {
    if (!rowMap.has(requirement)) errors.push(`${requirement} is missing from RequirementsTraceability.md`);
  }
  for (const requirement of rowMap.keys()) {
    if (!definedRequirements.has(requirement)) errors.push(`${requirement} is not defined by the specification`);
  }

  errors.length
    ? fail("requirement-to-acceptance-traceability", errors.join("; "))
    : pass("requirement-to-acceptance-traceability", `${definedRequirements.size} requirements and ${definedGates.size} gates checked`);
}

function validateGovernance() {
  const agentInstructions = read("AGENTS.md");
  const specification = read("docs/specifications/FinalProjectSpecification-v1.0.md");
  const acceptance = read("docs/requirements/AcceptanceCriteria.md");
  const decisions = read("docs/governance/DecisionRegister.md");
  const traceability = read("docs/requirements/RequirementsTraceability.md");
  const sourceOfTruth = read("docs/governance/SourceOfTruth.md");
  const developmentPolicy = read("docs/governance/DevelopmentAndMergePolicy.md");
  const workflowState = JSON.parse(read(".agents/skills/cuda-dev-workflow/references/workflow-state.json"));
  const definedRequirements = ids(specification, /\bREQ-[A-Z]-\d{3}\b/g);
  const definedGates = ids(acceptance, /\bAC-\d{3}\b/g);
  const definedDecisions = ids(decisions, /\b(?:DR|UD)-\d{3}\b/g);
  const errors = [];

  for (const reference of ids(traceability, /\b(?:DR|UD)-\d{3}\b/g)) {
    if (!definedDecisions.has(reference)) errors.push(`traceability references undefined ${reference}`);
  }
  for (const file of walk(path.join(root, "docs"), (candidate) => candidate.endsWith(".md"))) {
    const content = fs.readFileSync(file, "utf8");
    for (const reference of ids(content, /\bREQ-[A-Z]-\d{3}\b/g)) {
      if (!definedRequirements.has(reference)) errors.push(`${normalize(path.relative(root, file))} references undefined ${reference}`);
    }
    for (const reference of ids(content, /\bAC-\d{3}\b/g)) {
      if (!definedGates.has(reference)) errors.push(`${normalize(path.relative(root, file))} references undefined ${reference}`);
    }
    for (const reference of ids(content, /\b(?:DR|UD)-\d{3}\b/g)) {
      if (!definedDecisions.has(reference)) errors.push(`${normalize(path.relative(root, file))} references undefined ${reference}`);
    }
  }

  for (const id of ["UD-001", "UD-002", "UD-003", "UD-004", "UD-005"]) {
    if (!new RegExp(`^\\| ${id} \\|`, "m").test(decisions)) errors.push(`${id} is not present in the unresolved-decision table`);
  }
  const requiredSources = [
    "FinalProjectSpecification-v1.0.md",
    "DecisionRegister.md",
    "AcceptanceCriteria.md",
    "RequirementsTraceability.md",
    "Architecture.md",
    "CorrectnessPlan.md",
    "BenchmarkProtocol.md",
    "ExperimentTemplate.md",
    "DevelopmentAndMergePolicy.md",
  ];
  for (const source of requiredSources) if (!sourceOfTruth.includes(source)) errors.push(`SourceOfTruth.md omits ${source}`);
  if (!/material conflict[\s\S]{0,160}`blocked`/i.test(sourceOfTruth)) errors.push("SourceOfTruth.md does not retain the material-conflict blocked rule");
  if (!/^\| DR-018 \|/m.test(decisions)) errors.push("DecisionRegister.md omits approved repository governance decision DR-018");
  for (const phrase of [
    "Normal direct pushes to `main` are prohibited",
    "human reviewer",
    "squash",
    "branch protection",
    "rulesets",
    "Emergency hotfix",
    "Lean agent authorization boundaries",
  ]) {
    if (!developmentPolicy.toLowerCase().includes(phrase.toLowerCase())) errors.push(`DevelopmentAndMergePolicy.md omits required policy: ${phrase}`);
  }

  if (!/Owner[^\n]{0,120}exclusively[^\n]{0,160}Git and GitHub mutations/i.test(developmentPolicy)) {
    errors.push("DevelopmentAndMergePolicy.md does not reserve Git/GitHub mutations to the Owner");
  }
  if (!/Codex[^\n]{0,200}(build|implementation)[^\n]{0,200}(test|debug)[^\n]{0,200}(without|do not need)[^\n]{0,120}(repeated|renewed|new) approval/i.test(developmentPolicy)) {
    errors.push("DevelopmentAndMergePolicy.md does not authorize the continuous scoped Codex loop");
  }
  if (!/ChatGPT[^\n]{0,160}read-only review[^\n]{0,160}material correctness or design risk/i.test(developmentPolicy)) {
    errors.push("DevelopmentAndMergePolicy.md does not retain material ChatGPT review");
  }
  if (!/Owner[^\n]{0,100}exclusively[^\n]{0,200}Git and GitHub mutations/i.test(agentInstructions)) {
    errors.push("AGENTS.md does not reserve Git/GitHub mutations to the Owner");
  }
  if (workflowState.development_policy?.git_and_github_mutation_actor !== "owner") {
    errors.push("workflow-state.json does not name Owner as the Git/GitHub mutation actor");
  }
  for (const actor of ["codex", "chatgpt"]) {
    const forbidden = workflowState.roles?.[actor]?.forbidden ?? [];
    if (!forbidden.includes("git_mutations") || !forbidden.includes("github_mutations")) {
      errors.push(`workflow-state.json does not prohibit ${actor} Git/GitHub mutations`);
    }
  }
  if (workflowState.performance_policy?.requires_correctness_acceptance !== true) {
    errors.push("workflow-state.json weakens correctness-before-performance");
  }

  const numericalCarrier = fs.readFileSync(path.join(root, "docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md"));
  const carrierSha = createHash("sha256").update(numericalCarrier).digest("hex");
  const approvedCarrierSha = "503aa6b2c97567d814a9316fb0f2f9890d5472aea2bcf1f928ac505c29d1aa39";
  if (carrierSha !== approvedCarrierSha) errors.push(`S6 numerical policy carrier hash changed: ${carrierSha}`);
  if (!sourceOfTruth.includes("S6ApprovedImplementationDesign-v1.0.md") || !sourceOfTruth.includes("S6DesignApprovalRecord.md")) {
    errors.push("SourceOfTruth.md omits the approved S6 design or its approval record");
  }

  errors.length ? fail("governance-reference-consistency", errors.join("; ")) : pass("governance-reference-consistency", `${definedDecisions.size} decisions checked`);
}

function validateWorkflow() {
  const workflowDirectory = path.join(root, ".github", "workflows");
  const workflows = walk(workflowDirectory, (candidate) => /\.ya?ml$/i.test(candidate));
  const yamlFiles = walk(path.join(root, ".github"), (candidate) => /\.ya?ml$/i.test(candidate));
  const errors = [];
  if (!workflows.length) errors.push("no workflow files found");

  for (const file of yamlFiles) {
    const relative = normalize(path.relative(root, file));
    const content = fs.readFileSync(file, "utf8");
    const lines = content.split(/\r?\n/);
    for (let index = 0; index < lines.length; index += 1) {
      const line = lines[index];
      if (line.includes("\t")) errors.push(`${relative}:${index + 1} contains a tab`);
      const indentation = line.match(/^ */)?.[0].length ?? 0;
      if (line.trim() && indentation % 2 !== 0) errors.push(`${relative}:${index + 1} uses odd indentation`);
    }
    if (!content.endsWith("\n")) errors.push(`${relative} does not end with a newline`);
  }

  for (const file of workflows) {
    const relative = normalize(path.relative(root, file));
    const content = fs.readFileSync(file, "utf8");
    if (/\bpull_request_target\s*:/.test(content)) errors.push(`${relative} uses pull_request_target`);
    if (/permissions\s*:\s*write-all/.test(content)) errors.push(`${relative} grants write-all`);
    for (const match of content.matchAll(/^\s*-?\s*uses:\s*([^\s#]+)\s*(?:#.*)?$/gm)) {
      const action = match[1];
      const revision = action.split("@").pop();
      if (!/^[0-9a-f]{40}$/.test(revision)) errors.push(`${relative} has an unpinned action reference: ${action}`);
    }
  }

  const governancePath = path.join(workflowDirectory, "governance.yml");
  if (fs.existsSync(governancePath)) {
    const content = fs.readFileSync(governancePath, "utf8");
    const requiredFragments = [
      "permissions:\n  contents: read",
      "persist-credentials: false",
      "fetch-depth: 0",
      "node-version: \"24\"",
      "node --check .agents/skills/cuda-dev-workflow/scripts/validate-skill-system.mjs",
      "validate-skill-system.mjs --all",
      "validate-governance.mjs --links",
      "validate-governance.mjs --traceability",
      "validate-governance.mjs --governance",
      "validate-governance.mjs --workflow --hygiene",
      "- name: Validate Git whitespace",
      "git rev-parse --is-shallow-repository",
      "git diff --check \"${PR_BASE_SHA}...${PR_HEAD_SHA}\"",
      "git diff --check \"$PUSH_BEFORE_SHA\" \"$GITHUB_SHA\"",
      "Manual-run policy: checking the complete tracked tree",
    ];
    for (const fragment of requiredFragments) if (!content.includes(fragment)) errors.push(`governance.yml is missing: ${fragment.replaceAll("\n", " / ")}`);
  } else errors.push(".github/workflows/governance.yml is missing");

  const issueForms = walk(path.join(root, ".github", "ISSUE_TEMPLATE"), (candidate) => /\.ya?ml$/i.test(candidate));
  for (const file of issueForms) {
    const relative = normalize(path.relative(root, file));
    const content = fs.readFileSync(file, "utf8");
    for (const key of ["name", "description", "title", "labels", "body"]) {
      if (!new RegExp(`^${key}:`, "m").test(content)) errors.push(`${relative} is missing top-level ${key}`);
    }
    const formIds = [...content.matchAll(/^\s+id:\s*([a-z0-9_-]+)\s*$/gim)].map((match) => match[1]);
    if (formIds.length !== new Set(formIds).size) errors.push(`${relative} contains duplicate form ids`);
    if (!/^body:\s*$/m.test(content) || !/^\s+- type:\s*(?:markdown|textarea|input|dropdown|checkboxes)\s*$/m.test(content)) {
      errors.push(`${relative} does not contain a recognizable issue-form body`);
    }
  }

  errors.length
    ? fail("github-yaml-and-workflow-structure", errors.join("; "))
    : pass("github-yaml-and-workflow-structure", `${yamlFiles.length} YAML files and ${workflows.length} workflow checked`);
}

function candidateFiles() {
  const git = spawnSync("git", ["-C", root, "ls-files", "-z"], { encoding: "utf8", windowsHide: true });
  if (git.status === 0) {
    return { scope: "tracked", files: git.stdout.split("\0").filter(Boolean).map((file) => path.join(root, file)) };
  }
  return { scope: "workspace", files: walk(root) };
}

function validateHygiene() {
  const { scope, files } = candidateFiles();
  const generatedExtensions = new Set([".a", ".cubin", ".dll", ".dylib", ".exe", ".fatbin", ".ilk", ".lib", ".ncu-rep", ".nsys-rep", ".nvvp", ".o", ".obj", ".pdb", ".ptx", ".sass", ".so"]);
  const secretNames = /(^|\/)(?:\.env(?:\..+)?|id_(?:rsa|dsa|ecdsa|ed25519)|credentials[^/]*\.json|secrets?[^/]*\.json|[^/]+\.(?:p12|pfx|pem|key))$/i;
  const allowedSecretExamples = /(^|\/)\.env\.(?:example|template)$/i;
  const contentPatterns = [
    { name: "private key", expression: new RegExp("-----BEGIN " + "(?:RSA |EC |OPENSSH )?" + "PRIVATE KEY-----") },
    { name: "GitHub token", expression: /\bgh[oprsu]_[A-Za-z0-9]{30,}\b/ },
    { name: "AWS access key", expression: /\b(?:AKIA|ASIA)[A-Z0-9]{16}\b/ },
    { name: "OpenAI API key", expression: /\bsk-[A-Za-z0-9_-]{32,}\b/ },
    { name: "Slack token", expression: /\bxox[baprs]-[A-Za-z0-9-]{20,}\b/ },
  ];
  const errors = [];

  for (const file of files) {
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) continue;
    const relative = normalize(path.relative(root, file));
    const lower = relative.toLowerCase();
    const extension = [...generatedExtensions].find((candidate) => lower.endsWith(candidate) || lower.includes(`${candidate}.`));
    if (extension) errors.push(`${relative} looks like generated binary/intermediate output`);
    if (secretNames.test(relative) && !allowedSecretExamples.test(relative)) errors.push(`${relative} looks like a credential or private-key file`);
    if (fs.statSync(file).size > 2 * 1024 * 1024) continue;
    const buffer = fs.readFileSync(file);
    if (buffer.includes(0)) continue;
    const content = buffer.toString("utf8");
    for (const pattern of contentPatterns) if (pattern.expression.test(content)) errors.push(`${relative} contains a possible ${pattern.name}`);
  }

  errors.length ? fail("generated-binary-and-secret-hygiene", errors.join("; ")) : pass("generated-binary-and-secret-hygiene", `${files.length} ${scope} files scanned`);
}

if (selected.has("--links")) validateLinks();
if (selected.has("--traceability")) validateTraceability();
if (selected.has("--governance")) validateGovernance();
if (selected.has("--workflow")) validateWorkflow();
if (selected.has("--hygiene")) validateHygiene();

for (const result of results) console.log(`${result.status} ${result.name}${result.detail ? ` - ${result.detail}` : ""}`);
console.log(`SUMMARY passed=${results.filter((result) => result.status === "PASS").length} failed=${failureCount}`);
process.exitCode = failureCount ? 1 : 0;
