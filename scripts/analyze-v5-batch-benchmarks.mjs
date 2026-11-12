#!/usr/bin/env node
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import {createHash} from "node:crypto";
import {fileURLToPath} from "node:url";

function fail(message) { throw new Error(message); }
function sha256(file) { return createHash("sha256").update(fs.readFileSync(file)).digest("hex"); }
function objectSha(value) { return createHash("sha256").update(JSON.stringify(value)).digest("hex"); }
function json(file) { return JSON.parse(fs.readFileSync(file, "utf8")); }
function jsonl(file) {
  return fs.readFileSync(file, "utf8").split(/\r?\n/).filter(Boolean).map((line, i) => {
    try { return JSON.parse(line); } catch { fail(`invalid JSONL ${file}:${i + 1}`); }
  });
}
function csv(file) {
  const lines = fs.readFileSync(file, "utf8").trim().split(/\r?\n/);
  if (lines.length < 2) fail(`empty CSV: ${file}`);
  const header = lines.shift().split(",");
  return lines.filter(Boolean).map((line, i) => {
    const values = line.split(",");
    if (values.length !== header.length) fail(`CSV field mismatch ${file}:${i + 2}`);
    return Object.fromEntries(header.map((key, column) => [key, values[column]]));
  });
}
function writeCsv(file, rows, columns) {
  fs.writeFileSync(file, columns.join(",") + "\n" + rows.map(row =>
    columns.map(column => row[column]).join(",")).join("\n") + "\n");
}
function percentile(values, p) {
  if (!values.length) fail("empty distribution");
  const sorted = [...values].sort((a, b) => a - b);
  const position = p * (sorted.length - 1);
  const lower = Math.floor(position), upper = Math.ceil(position);
  return sorted[lower] + (sorted[upper] - sorted[lower]) * (position - lower);
}
function identityHash(identity) {
  let state = 2166136261 >>> 0;
  for (const byte of Buffer.from(identity)) {
    state ^= byte; state = Math.imul(state, 16777619) >>> 0;
  }
  return state || 1;
}
function bootstrapMedian(values, identity) {
  let state = identityHash(identity);
  const medians = [];
  for (let replicate = 0; replicate < 2000; ++replicate) {
    const sample = [];
    for (let i = 0; i < values.length; ++i) {
      state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
      sample.push(values[Math.floor((state / 0x100000000) * values.length)]);
    }
    medians.push(percentile(sample, 0.5));
  }
  return [percentile(medians, 0.025), percentile(medians, 0.975)];
}

const Ns = [1024, 16384, 262144, 1000000];
const Bs = [8, 32, 128];
const tiles = [1, 4, 8];
const blocks = [128, 256, 512];
const diagnosticBoundary = "V5-BATCH-PREPARED";
const formalBoundaries = ["CPU-BATCH-RAW", "V5-BATCH-RAW",
  "V5-BATCH-PREPARED", "V5-BATCH-KERNEL"];
const constructionBoundaries = ["V5-PREPARED-DATASET-CONSTRUCTION",
  "V5-WORKSPACE-CONSTRUCTION", "V5-PREPARED-READY-CONSTRUCTION"];
const boundaryIdentity = new Map([
  ["CPU-BATCH-RAW", ["steady_clock", "dataset-and-query-validation-included"]],
  ["V5-BATCH-RAW", ["steady_clock", "call-owned-transient-resources-and-cleanup-included"]],
  ["V5-BATCH-PREPARED", ["steady_clock", "prepared-resources-reused-destruction-excluded"]],
  ["V5-BATCH-KERNEL", ["cuda_event", "device-resident-complete-kernel-sequence"]],
  ["V5-PREPARED-DATASET-CONSTRUCTION", ["steady_clock", "persistent-destruction-excluded"]],
  ["V5-WORKSPACE-CONSTRUCTION", ["steady_clock", "persistent-destruction-excluded"]],
  ["V5-PREPARED-READY-CONSTRUCTION", ["steady_clock", "persistent-destruction-excluded-not-lifetime-cost"]]
]);

function validateConfig(config) {
  const proposal = config.proposed_policy ?? {};
  if (config.schema_version !== "V5-BATCH-OPTIMIZATION-CONFIG-v1-PROPOSED" ||
      config.experiment_policy_status !== "PROPOSED / NOT OWNER-APPROVED" ||
      JSON.stringify(config.workload.candidate_counts) !== JSON.stringify(Ns) ||
      config.workload.dimension !== 128 ||
      JSON.stringify(config.workload.batch_sizes) !== JSON.stringify(Bs) ||
      config.workload.mandatory_workloads !== 12 ||
      config.workload.query_buffer_count !== 128 ||
      JSON.stringify(config.candidate_space.query_tiles) !== JSON.stringify(tiles) ||
      JSON.stringify(config.candidate_space.block_sizes) !== JSON.stringify(blocks) ||
      config.sampling.warmups !== 10 || config.sampling.measured_repetitions !== 100 ||
      config.sampling.bootstrap_resamples !== 2000 ||
      config.sampling.percentile_method !== "linear_n_minus_1" ||
      config.environment_policy.telemetry_query_timeout_ms !== 900 ||
      config.environment_policy.telemetry_interval_ms !== 1000 ||
      config.environment_policy.maximum_accepted_start_gap_ms !== 3000 ||
      config.fast_math !== "disabled" ||
      proposal.status !== "PROPOSED / NOT OWNER-APPROVED" ||
      !proposal.bootstrap_prng?.includes("2166136261") ||
      !proposal.diagnostic_candidate_order?.includes("(w+i) mod 9") ||
      !proposal.formal_lane_order?.includes("(w+i) mod 4")) {
    fail("V5 experiment configuration contract mismatch");
  }
  const ordinals = Array.from({length: 128}, (_, k) => Math.floor(k * 9999 / 127));
  if (ordinals[0] !== 0 || ordinals[127] !== 9999 || new Set(ordinals).size !== 128) {
    fail("V5 query formula is not 128 deterministic distinct ordinals");
  }
}

function requireFile(file) { if (!fs.existsSync(file) || fs.statSync(file).size === 0) fail(`missing/empty evidence: ${file}`); }
function validateCommon(directory, expectedPhase) {
  const required = ["manifest.json", "experiment-config.json", "policy-approval.json",
    "correctness-gate.json", "environment.json", "benchmark-interval.json", "raw_samples.csv",
    "telemetry/gpu-telemetry.jsonl",
    "telemetry/telemetry-attempts.jsonl", "telemetry/gpu-processes.jsonl",
    "telemetry/telemetry-errors.log", "telemetry/telemetry-session.json"];
  if (expectedPhase === "formal") required.push("diagnostic-summary.json", "tuning-freeze.csv");
  for (const relative of required) {
    const file = path.join(directory, relative);
    if (relative.endsWith("telemetry-errors.log")) {
      if (!fs.existsSync(file)) fail(`missing evidence: ${file}`);
    } else requireFile(file);
  }
  const manifest = json(path.join(directory, "manifest.json"));
  const config = json(path.join(directory, "experiment-config.json"));
  const approval = json(path.join(directory, "policy-approval.json"));
  const correctness = json(path.join(directory, "correctness-gate.json"));
  const environment = json(path.join(directory, "environment.json"));
  validateConfig(config);
  if (manifest.schema_version !== "V5-BATCH-MANIFEST-v1" ||
      manifest.phase !== expectedPhase || manifest.working_tree_state !== "clean" ||
      !/^[0-9a-f]{40}$/.test(manifest.revision) ||
      manifest.experiment_config_sha256 !== sha256(path.join(directory, "experiment-config.json")) ||
      manifest.policy_approval_sha256 !== sha256(path.join(directory, "policy-approval.json")) ||
      manifest.correctness_gate_sha256 !== sha256(path.join(directory, "correctness-gate.json"))) {
    fail("V5 manifest/phase/revision/artifact identity mismatch");
  }
  if (approval.schema_version !== "V5-EXPERIMENT-POLICY-APPROVAL-v1" ||
      approval.status !== "OWNER-APPROVED" ||
      approval.approved_by_role !== "Project Owner" ||
      approval.experiment_config_sha256 !== manifest.experiment_config_sha256 ||
      approval.approved_proposed_policy_sha256 !== objectSha(config.proposed_policy)) {
    fail("V5 proposed experiment policy is not Owner-approved for this exact config");
  }
  if (correctness.schema_version !== "V5-CORRECTNESS-GATE-v1" ||
      correctness.status !== "passed-clean-candidate" ||
      correctness.performance_authorized !== true ||
      correctness.revision !== manifest.revision ||
      correctness.experiment_config_sha256 !== manifest.experiment_config_sha256 ||
      correctness.cpu_float32_float64_cases !== 512 ||
      correctness.v5_candidate_batch_cases !== 216 ||
      correctness.mandatory_workloads !== 12) {
    fail("V5 correctness gate is missing, incomplete, or identity-incompatible");
  }
  if (environment.schema_version !== "V5-ENVIRONMENT-v1" ||
      environment.status !== "accepted" || environment.revision !== manifest.revision ||
      environment.experiment_config_sha256 !== manifest.experiment_config_sha256 ||
      environment.fast_math !== "disabled" || !environment.cuda_architecture ||
      !environment.build_flags || !environment.gpu_uuid || !environment.gpu_name) {
    fail("V5 environment gate is missing, incomplete, or not accepted");
  }
  validateTelemetry(directory, config, environment, expectedPhase);
  return {manifest, config, approval, correctness, environment};
}

function validateTelemetry(directory, config, environment, expectedPhase) {
  const telemetry = path.join(directory, "telemetry");
  const attempts = jsonl(path.join(telemetry, "telemetry-attempts.jsonl"));
  const session = json(path.join(telemetry, "telemetry-session.json"));
  const interval = json(path.join(directory, "benchmark-interval.json"));
  const sessionStart = Date.parse(session.started_at), sessionEnd = Date.parse(session.ended_at);
  const benchmarkStart = Date.parse(interval.started_at), benchmarkEnd = Date.parse(interval.ended_at);
  if (attempts.length < 2 || session.interval_ms !== 1000 || session.query_timeout_ms !== 900 ||
      session.failed_samples !== 0 || session.completed_samples !== attempts.length ||
      interval.schema_version !== "V5-BENCHMARK-INTERVAL-v1" ||
      interval.phase !== expectedPhase ||
      !Number.isFinite(sessionStart) || !Number.isFinite(sessionEnd) ||
      !Number.isFinite(benchmarkStart) || !Number.isFinite(benchmarkEnd) ||
      sessionStart > benchmarkStart || sessionEnd < benchmarkEnd || benchmarkStart >= benchmarkEnd ||
      fs.readFileSync(path.join(telemetry, "telemetry-errors.log"), "utf8").trim()) {
    fail("V5 telemetry completeness/session gate failed");
  }
  let prior = null;
  for (const row of attempts) {
    const start = Number(row.attempt_started_elapsed_ms);
    if (row.status !== "completed" || row.metric_status !== "completed" ||
        row.process_status !== "completed" ||
        Number(row.metric_normal_completion_duration_ms) > 900 ||
        Number(row.process_normal_completion_duration_ms) > 900 ||
        row.metric_cleanup_status !== "not-required" ||
        row.process_cleanup_status !== "not-required" ||
        !Number.isFinite(start) || start < 0 ||
        (prior === null ? start > 3000 : start - prior > 3000)) {
      fail("V5 telemetry deadline/cadence/start-gap gate failed");
    }
    prior = start;
  }
  const allowed = new Set(config.environment_policy.experiment_process_names.map(x => x.toLowerCase()));
  const metrics = jsonl(path.join(telemetry, "gpu-telemetry.jsonl"));
  if (metrics.length !== attempts.length) fail("V5 telemetry metric/attempt completeness mismatch");
  for (const row of metrics) {
    if (String(row.uuid).trim() !== String(environment.gpu_uuid).trim() ||
        String(row.hardware_thermal_slowdown).trim().toLowerCase() === "active" ||
        Number(row.query_duration_ms) > 900) {
      fail("V5 telemetry reported thermal slowdown or a late metric query");
    }
  }
  for (const row of jsonl(path.join(telemetry, "gpu-processes.jsonl"))) {
    if (row.no_processes === true) continue;
    const name = path.basename(String(row.process_name ?? "")).toLowerCase();
    if (!allowed.has(name)) fail(`unauthorized GPU process in V5 telemetry: ${name}`);
  }
}

function validateRows(rows, phase, freeze = null, expectedRevision = null) {
  const expectedBoundaries = phase === "diagnostic" ? [diagnosticBoundary] :
    [...formalBoundaries, ...constructionBoundaries];
  const groups = new Map(), runOrders = new Set();
  for (const row of rows) {
    const N = Number(row.N), B = Number(row.B), tile = Number(row.query_tile);
    const block = Number(row.block_size), iteration = Number(row.iteration);
    const elapsed = Number(row.elapsed_ns), order = Number(row.run_order);
    if (row.schema_version !== "V5-BATCH-RAW-v1" || row.phase !== phase ||
        (expectedRevision !== null && row.revision !== expectedRevision) ||
        row.tree_state !== "clean" || row.workload !== "optional-secondary-batch-optimization" ||
        row.D !== "128" || row.warmup !== "false" || row.status !== "completed" ||
        !Ns.includes(N) || !Bs.includes(B) || !tiles.includes(tile) || !blocks.includes(block) ||
        !expectedBoundaries.includes(row.boundary) ||
        !Number.isSafeInteger(iteration) || iteration < 0 || iteration >= 100 ||
        !Number.isSafeInteger(elapsed) || elapsed <= 0 ||
        !Number.isSafeInteger(order) || order < 0 || runOrders.has(order)) {
      fail("invalid V5 raw sample identity/value/order");
    }
    const expectedIdentity = boundaryIdentity.get(row.boundary);
    if (!expectedIdentity || row.timer !== expectedIdentity[0] ||
        row.resource_scope !== expectedIdentity[1]) {
      fail("V5 timing-boundary timer/resource-scope identity mismatch");
    }
    if (freeze) {
      const selected = freeze.get(`${N}|${B}`);
      if (!selected || selected.tile !== tile || selected.block !== block) {
        fail(`formal sample differs from frozen selection N=${N} B=${B}`);
      }
    }
    runOrders.add(order);
    const key = [row.boundary, N, B, tile, block].join("|");
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push({elapsed, iteration});
  }
  if (runOrders.size !== rows.length || Math.min(...runOrders) !== 0 ||
      Math.max(...runOrders) !== rows.length - 1) fail("V5 run_order is not complete contiguous identity");
  const expectedGroups = phase === "diagnostic" ? 12 * 9 : 12 * 7;
  if (groups.size !== expectedGroups || rows.length !== expectedGroups * 100) {
    fail(`V5 ${phase} completeness mismatch`);
  }
  for (const [key, values] of groups) {
    if (values.length !== 100 || new Set(values.map(x => x.iteration)).size !== 100) {
      fail(`V5 retained-sample completeness mismatch: ${key}`);
    }
  }
  return groups;
}

function summarize(groups) {
  return [...groups].map(([key, samples]) => {
    const [boundary, N, B, query_tile, block_size] = key.split("|").map((x, i) => i ? Number(x) : x);
    const values = samples.map(sample => sample.elapsed);
    const [ci_low_ns, ci_high_ns] = bootstrapMedian(values, key);
    return {boundary, N, B, query_tile, block_size, sample_count: values.length,
      percentile_method: "linear_n_minus_1", median_ns: percentile(values, 0.5),
      p5_ns: percentile(values, 0.05), p95_ns: percentile(values, 0.95),
      ci_low_ns, ci_high_ns};
  }).sort((a, b) => a.N - b.N || a.B - b.B ||
    a.boundary.localeCompare(b.boundary) || a.query_tile - b.query_tile || a.block_size - b.block_size);
}

function diagnostic(directory) {
  const common = validateCommon(directory, "diagnostic");
  const groups = validateRows(csv(path.join(directory, "raw_samples.csv")),
    "diagnostic", null, common.manifest.revision);
  const summaries = summarize(groups), selected = [];
  for (const N of Ns) for (const B of Bs) {
    const rows = summaries.filter(row => row.N === N && row.B === B &&
      row.boundary === diagnosticBoundary).sort((a, b) =>
        a.median_ns - b.median_ns || a.query_tile - b.query_tile || a.block_size - b.block_size);
    if (rows.length !== 9 || !Number.isFinite(rows[0].median_ns)) fail(`invalid V5 diagnostic candidates N=${N} B=${B}`);
    selected.push({N, B, query_tile: rows[0].query_tile, block_size: rows[0].block_size,
      diagnostic_median_ns: rows[0].median_ns});
  }
  writeCsv(path.join(directory, "diagnostic-summary.csv"), summaries,
    ["boundary","N","B","query_tile","block_size","sample_count","percentile_method",
      "median_ns","p5_ns","p95_ns","ci_low_ns","ci_high_ns"]);
  fs.writeFileSync(path.join(directory, "diagnostic-summary.json"), JSON.stringify({
    schema_version: "V5-DIAGNOSTIC-SUMMARY-v1", policy_status: "OWNER-APPROVED-BY-BOUND-POLICY-FILE",
    revision: common.manifest.revision,
    experiment_config_sha256: common.manifest.experiment_config_sha256,
    policy_approval_sha256: common.manifest.policy_approval_sha256,
    correctness_gate_sha256: common.manifest.correctness_gate_sha256,
    selection_rule: "lowest valid Prepared E2E median; tie smaller query tile, then block size",
    summaries, selected}, null, 2) + "\n");
  writeCsv(path.join(directory, "tuning-freeze.csv"), selected,
    ["N","B","query_tile","block_size","diagnostic_median_ns"]);
  fs.writeFileSync(path.join(directory, "tuning-freeze.json"), JSON.stringify({
    schema_version: "V5-TUNING-FREEZE-v1", selected}, null, 2) + "\n");
  writePacketManifest(directory, ["diagnostic-summary.csv","diagnostic-summary.json",
    "tuning-freeze.csv","tuning-freeze.json"]);
}

function loadFreeze(directory, manifest) {
  const file = path.join(directory, "tuning-freeze.csv");
  requireFile(file);
  if (manifest.freeze_sha256 !== sha256(file)) fail("formal tuning-freeze SHA-256 mismatch");
  const rows = csv(file), result = new Map();
  for (const row of rows) {
    const N = Number(row.N), B = Number(row.B), tile = Number(row.query_tile), block = Number(row.block_size);
    if (!Ns.includes(N) || !Bs.includes(B) || !tiles.includes(tile) || !blocks.includes(block) ||
        result.has(`${N}|${B}`) || !(Number(row.diagnostic_median_ns) > 0)) fail("invalid tuning-freeze row");
    result.set(`${N}|${B}`, {tile, block});
  }
  if (result.size !== 12) fail("formal tuning-freeze completeness mismatch");
  const diagnostic = json(path.join(directory, "diagnostic-summary.json"));
  if (diagnostic.schema_version !== "V5-DIAGNOSTIC-SUMMARY-v1" ||
      diagnostic.revision !== manifest.revision ||
      diagnostic.experiment_config_sha256 !== manifest.experiment_config_sha256 ||
      diagnostic.policy_approval_sha256 !== manifest.policy_approval_sha256 ||
      diagnostic.correctness_gate_sha256 !== manifest.correctness_gate_sha256 ||
      !Array.isArray(diagnostic.selected) || diagnostic.selected.length !== 12) {
    fail("formal diagnostic-selection provenance identity mismatch");
  }
  for (const selected of diagnostic.selected) {
    const frozen = result.get(`${Number(selected.N)}|${Number(selected.B)}`);
    if (!frozen || frozen.tile !== Number(selected.query_tile) ||
        frozen.block !== Number(selected.block_size) || !(Number(selected.diagnostic_median_ns) > 0)) {
      fail("formal freeze differs from analyzer-selected diagnostic winner");
    }
  }
  return result;
}

function findSummary(summaries, boundary, N, B) {
  const rows = summaries.filter(row => row.boundary === boundary && row.N === N && row.B === B);
  if (rows.length !== 1) fail(`missing/duplicate summary ${boundary} N=${N} B=${B}`);
  return rows[0];
}
function breakEven(cpu, prepared, ready) {
  const pointDelta = cpu.median_ns - prepared.median_ns;
  const conservativeDelta = cpu.ci_low_ns - prepared.ci_high_ns;
  const count = (cost, delta) => delta > 0 ? Math.floor(cost / delta) + 1 : null;
  const point = count(ready.median_ns, pointDelta);
  const conservative = count(ready.ci_high_ns, conservativeDelta);
  return {point_batches: point, point_queries: point === null ? null : point * cpu.B,
    conservative_batches: conservative,
    conservative_queries: conservative === null ? null : conservative * cpu.B,
    persistent_resource_destruction: "excluded; not a complete lifetime cost"};
}

function formal(directory) {
  const common = validateCommon(directory, "formal");
  const freeze = loadFreeze(directory, common.manifest);
  const groups = validateRows(csv(path.join(directory, "raw_samples.csv")),
    "formal", freeze, common.manifest.revision);
  const summaries = summarize(groups), comparisons = [], secondary = [];
  for (const N of Ns) for (const B of Bs) {
    const cpu = findSummary(summaries, "CPU-BATCH-RAW", N, B);
    const raw = findSummary(summaries, "V5-BATCH-RAW", N, B);
    const prepared = findSummary(summaries, "V5-BATCH-PREPARED", N, B);
    const kernel = findSummary(summaries, "V5-BATCH-KERNEL", N, B);
    const datasetConstruction = findSummary(summaries, "V5-PREPARED-DATASET-CONSTRUCTION", N, B);
    const workspaceConstruction = findSummary(summaries, "V5-WORKSPACE-CONSTRUCTION", N, B);
    const ready = findSummary(summaries, "V5-PREPARED-READY-CONSTRUCTION", N, B);
    const speedup = cpu.median_ns / raw.median_ns;
    const ciLow = cpu.ci_low_ns / raw.ci_high_ns;
    const ciHigh = cpu.ci_high_ns / raw.ci_low_ns;
    comparisons.push({N, B, query_tile: raw.query_tile, block_size: raw.block_size,
      cpu_raw_median_ns: cpu.median_ns, gpu_v5_raw_median_ns: raw.median_ns,
      speedup, speedup_ci_low: ciLow, speedup_ci_high: ciHigh,
      outcome: ciLow > 1.0 ? "PASS" : "FAIL"});
    secondary.push({N, B, query_tile: raw.query_tile, block_size: raw.block_size,
      prepared, kernel, dataset_construction: datasetConstruction,
      workspace_construction: workspaceConstruction, prepared_ready_construction: ready,
      amortization_and_break_even: breakEven(cpu, prepared, ready)});
  }
  const pass = comparisons.length === 12 && comparisons.every(row => row.outcome === "PASS");
  writeCsv(path.join(directory, "summary.csv"), summaries,
    ["boundary","N","B","query_tile","block_size","sample_count","percentile_method",
      "median_ns","p5_ns","p95_ns","ci_low_ns","ci_high_ns"]);
  fs.writeFileSync(path.join(directory, "summary.json"), JSON.stringify({
    schema_version: "V5-FORMAL-SUMMARY-v1", uncertainty:
      "deterministic 2000-resample percentile bootstrap; linear_n_minus_1", summaries}, null, 2) + "\n");
  fs.writeFileSync(path.join(directory, "primary-comparisons.json"), JSON.stringify({
    schema_version: "V5-PRIMARY-COMPARISONS-v1",
    definition: "CPU Batch Raw median / GPU V5 Raw median",
    conservative_95_ci: "[CPU_ci_low/GPU_ci_high,CPU_ci_high/GPU_ci_low]",
    workloads: comparisons}, null, 2) + "\n");
  fs.writeFileSync(path.join(directory, "secondary-results.json"), JSON.stringify({
    schema_version: "V5-SECONDARY-RESULTS-v1", primary_acceptance_independent: true,
    results: secondary}, null, 2) + "\n");
  fs.writeFileSync(path.join(directory, "acceptance.json"), JSON.stringify({
    schema_version: "V5-ACCEPTANCE-v1", completeness_gate: "accepted",
    identity_gate: "accepted", correctness_gate: "accepted", telemetry_gate: "accepted",
    environment_gate: "accepted", mandatory_workload_count: 12,
    primary_rule: "lower 95% speedup CI > 1.0 for all 12 workloads",
    primary_outcome: pass ? "PASS" : "FAIL"}, null, 2) + "\n");
  writePacketManifest(directory, ["summary.csv","summary.json","primary-comparisons.json",
    "secondary-results.json","acceptance.json"]);
}

function writePacketManifest(directory, generated) {
  const files = ["manifest.json","experiment-config.json","policy-approval.json",
    "correctness-gate.json","environment.json","benchmark-interval.json",
    "raw_samples.csv","telemetry/telemetry-attempts.jsonl",
    "telemetry/gpu-telemetry.jsonl","telemetry/gpu-processes.jsonl",
    "telemetry/telemetry-errors.log","telemetry/telemetry-session.json", ...generated];
  if (fs.existsSync(path.join(directory, "diagnostic-summary.json")) &&
      !files.includes("diagnostic-summary.json")) files.push("diagnostic-summary.json");
  if (fs.existsSync(path.join(directory, "tuning-freeze.csv")) &&
      !files.includes("tuning-freeze.csv")) files.push("tuning-freeze.csv");
  const rows = files.map(relative => ({path: relative, sha256: sha256(path.join(directory, relative)),
    bytes: fs.statSync(path.join(directory, relative)).size}));
  fs.writeFileSync(path.join(directory, "analysis-manifest.json"), JSON.stringify({
    schema_version: "V5-ANALYSIS-MANIFEST-v1", files: rows}, null, 2) + "\n");
}

function selfTest() {
  if (percentile([1,2,3,4,5], 0.5) !== 3 ||
      Math.abs(percentile([1,2,3,4,5], 0.05) - 1.2) > 1e-12) fail("percentile self-test failed");
  const first = bootstrapMedian([1,2,3,4,5], "boundary|1024|8|1|128");
  const second = bootstrapMedian([1,2,3,4,5], "boundary|1024|8|1|128");
  if (JSON.stringify(first) !== JSON.stringify(second)) fail("bootstrap determinism self-test failed");
  const config = json(path.resolve(path.dirname(fileURLToPath(import.meta.url)),
    "../benchmarks/config/v5-batch-optimization.json"));
  validateConfig(config);
  const corrupt = structuredClone(config); corrupt.workload.batch_sizes = [8, 32];
  let rejected = false; try { validateConfig(corrupt); } catch { rejected = true; }
  if (!rejected) fail("config completeness corruption was accepted");
  const rows = [
    {median_ns: 10, query_tile: 4, block_size: 128},
    {median_ns: 10, query_tile: 1, block_size: 512},
    {median_ns: 10, query_tile: 1, block_size: 128}
  ].sort((a,b) => a.median_ns-b.median_ns || a.query_tile-b.query_tile || a.block_size-b.block_size);
  if (rows[0].query_tile !== 1 || rows[0].block_size !== 128) fail("selection tie-break self-test failed");
  const diagnosticRows = [];
  let order = 0;
  for (const N of Ns) for (const B of Bs) for (const query_tile of tiles)
    for (const block_size of blocks) for (let iteration = 0; iteration < 100; ++iteration) {
      diagnosticRows.push({schema_version:"V5-BATCH-RAW-v1",revision:"a".repeat(40),
        tree_state:"clean",phase:"diagnostic",workload:"optional-secondary-batch-optimization",
        boundary:diagnosticBoundary,N:String(N),D:"128",B:String(B),query_tile:String(query_tile),
        block_size:String(block_size),run_order:String(order++),iteration:String(iteration),
        warmup:"false",elapsed_ns:String(1000 + query_tile + block_size + iteration),
        timer:"steady_clock",status:"completed",
        resource_scope:"prepared-resources-reused-destruction-excluded"});
    }
  const diagnosticGroups = validateRows(diagnosticRows, "diagnostic", null, "a".repeat(40));
  if (diagnosticGroups.size !== 108 || summarize(diagnosticGroups).length !== 108) {
    fail("diagnostic completeness self-test failed");
  }
  let rejectedCompleteness = false;
  try { validateRows(diagnosticRows.slice(1), "diagnostic", null, "a".repeat(40)); }
  catch { rejectedCompleteness = true; }
  if (!rejectedCompleteness) fail("missing diagnostic row was accepted");
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "v5-analysis-self-test-"));
  try {
    fs.writeFileSync(path.join(temporary, "raw_samples.csv"), "schema_version\n");
    rejected = false; try { validateRows(csv(path.join(temporary, "raw_samples.csv")), "diagnostic"); }
    catch { rejected = true; }
    if (!rejected) fail("missing diagnostic evidence was accepted");
  } finally { fs.rmSync(temporary, {recursive: true, force: true}); }
  console.log("V5 analyzer self-test: passed");
}

const args = process.argv.slice(2);
if (args.length === 1 && args[0] === "--self-test") selfTest();
else if (args.length === 2 && args[0] === "--diagnostic") diagnostic(path.resolve(args[1]));
else if (args.length === 2 && args[0] === "--formal") formal(path.resolve(args[1]));
else fail("usage: analyze-v5-batch-benchmarks.mjs --self-test | --diagnostic DIRECTORY | --formal DIRECTORY");
