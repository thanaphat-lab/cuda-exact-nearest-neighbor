#!/usr/bin/env node
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

function fail(message) { throw new Error(message); }

function parseCsv(file) {
  const text = fs.readFileSync(file, "utf8").trim();
  if (!text) fail(`empty CSV: ${file}`);
  const lines = text.split(/\r?\n/);
  const header = lines.shift().split(",");
  return lines.filter(Boolean).map((line, lineIndex) => {
    const values = line.split(",");
    if (values.length !== header.length) {
      fail(`CSV field count mismatch at ${file}:${lineIndex + 2}`);
    }
    return Object.fromEntries(header.map((key, index) => [key, values[index]]));
  });
}

function percentile(values, p) {
  if (!values.length) fail("empty distribution");
  const sorted = [...values].sort((a, b) => a - b);
  const position = p * (sorted.length - 1);
  const lower = Math.floor(position), upper = Math.ceil(position);
  return sorted[lower] + (sorted[upper] - sorted[lower]) * (position - lower);
}

function identityHash(text) {
  let value = 2166136261 >>> 0;
  for (const byte of Buffer.from(text)) {
    value ^= byte;
    value = Math.imul(value, 16777619) >>> 0;
  }
  return value || 1;
}

function bootstrapMedian(values, identity) {
  let state = identityHash(identity);
  const random = () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    return state / 0x100000000;
  };
  const medians = [];
  for (let replicate = 0; replicate < 2000; ++replicate) {
    const resample = Array.from({length: values.length},
      () => values[Math.floor(random() * values.length)]);
    medians.push(percentile(resample, 0.5));
  }
  return [percentile(medians, 0.025), percentile(medians, 0.975)];
}

function rowKey(row) {
  return [row.sample_role, row.scenario, row.boundary, row.variant,
          row.N, row.D, row.seed, row.block_size].join("|");
}

function summarize(rows) {
  const groups = new Map();
  for (const row of rows) {
    if (row.schema_version !== "S13-BENCHMARK-RAW-v1" ||
        row.sample_role !== "final" || row.workload !== "single-query" ||
        row.status !== "completed" || row.warmup !== "false" ||
        row.timer !== "steady_clock") {
      fail(`invalid raw sample identity for ${rowKey(row)}`);
    }
    const elapsed = Number(row.elapsed_ns);
    if (!Number.isSafeInteger(elapsed) || elapsed < 0) fail("invalid elapsed_ns");
    const values = groups.get(rowKey(row)) ?? [];
    values.push(elapsed);
    groups.set(rowKey(row), values);
  }
  return [...groups].map(([identity, values]) => {
    if (values.length !== 100) fail(`expected 100 samples for ${identity}`);
    const [sampleRole, scenario, boundary, variant, N, D, seed, blockSize] =
      identity.split("|");
    const [ciLow, ciHigh] = bootstrapMedian(values, identity);
    return {
      sample_role: sampleRole, scenario, boundary, variant,
      N: Number(N), D: Number(D), seed, block_size: Number(blockSize),
      sample_count: values.length, percentile_method: "linear_n_minus_1",
      median_ns: percentile(values, 0.5), p5_ns: percentile(values, 0.05),
      p95_ns: percentile(values, 0.95), ci_low_ns: ciLow, ci_high_ns: ciHigh
    };
  }).sort((a, b) => rowKey(a).localeCompare(rowKey(b)));
}

const candidateCounts = [1024, 16384, 262144, 1048576];
const dimensions = [8, 32, 128];
const expectedBoundaries = [
  "CPU-E2E-RAW", "BND-E2E-RAW", "BND-E2E-PREP",
  "COST-PREP-DATASET-CONSTRUCTION", "COST-WORKSPACE-CONSTRUCTION",
  "COST-PREPARED-READY-CONSTRUCTION"
];

function findSummary(summaries, boundary, N, D) {
  return summaries.find(row => row.boundary === boundary && row.N === N && row.D === D);
}

function validateCoverage(rows, summaries, manifest, gate, freezeRows) {
  const revisions = new Set(rows.map(row => row.revision));
  const treeStates = new Set(rows.map(row => row.tree_state));
  if (revisions.size !== 1 || !revisions.has(manifest.revision)) fail("revision mismatch");
  if (treeStates.size !== 1 || !treeStates.has("clean") ||
      manifest.working_tree_state !== "clean") fail("clean tree identity required");
  if (manifest.warmups !== 10 || manifest.measured_repetitions !== 100) {
    fail("sampling contract mismatch");
  }
  if (gate.schema_version !== "S13-CORRECTNESS-GATE-v1" ||
      gate.status !== "passed-clean-candidate" ||
      gate.revision !== manifest.revision || gate.working_tree_state !== "clean" ||
      gate.experiment_config_sha256 !== manifest.experiment_config_sha256 ||
      gate.freeze_sha256 !== manifest.freeze_sha256 ||
      gate.prepared_production_path_required !== true) {
    fail("correctness gate identity/status mismatch");
  }
  if (!String(manifest.persistent_resource_destruction).includes("not complete lifetime")) {
    fail("manifest does not disclose excluded persistent-resource destruction");
  }
  for (const N of candidateCounts) for (const D of dimensions) {
    for (const boundary of expectedBoundaries) {
      const summary = findSummary(summaries, boundary, N, D);
      if (!summary) fail(`missing distribution ${boundary} ${N}x${D}`);
    }
    const rowsForCase = rows.filter(row => Number(row.N) === N && Number(row.D) === D);
    if (!rowsForCase.every(row => row.seed === String(manifest.final_seed))) {
      fail(`seed mismatch for ${N}x${D}`);
    }
    const frozen = freezeRows.find(row => row.schema_version === "S7-TUNING-FREEZE-v1" &&
      row.variant === "v2" && Number(row.N) === N && Number(row.D) === D);
    if (!frozen) fail(`missing frozen V2 configuration ${N}x${D}`);
    const frozenBlock = Number(frozen.block_size);
    if (!rowsForCase.every(row => row.variant === "cpu"
      ? Number(row.block_size) === 0 : Number(row.block_size) === frozenBlock)) {
      fail(`frozen block-size mismatch for ${N}x${D}`);
    }
    const requiredScopes = new Map([
      ["CPU-E2E-RAW", "not-applicable"],
      ["BND-E2E-RAW", "call-owned-transient-cleanup-included"],
      ["COST-PREP-DATASET-CONSTRUCTION", "persistent-prepared-dataset-destruction-excluded"],
      ["COST-WORKSPACE-CONSTRUCTION", "persistent-workspace-destruction-excluded"],
      ["COST-PREPARED-READY-CONSTRUCTION", "persistent-resource-destruction-excluded-not-lifetime-cost"],
      ["BND-E2E-PREP", "persistent-resources-reused-destruction-excluded"]
    ]);
    for (const [boundary, scope] of requiredScopes) {
      if (!rowsForCase.filter(row => row.boundary === boundary)
          .every(row => row.resource_scope === scope)) {
        fail(`resource-scope mismatch for ${boundary} ${N}x${D}`);
      }
    }
  }
  if (summaries.length !== candidateCounts.length * dimensions.length * expectedBoundaries.length) {
    fail("unexpected summary distribution count");
  }
}

function comparison(reference, candidate, id, N, D) {
  const ratio = reference.median_ns / candidate.median_ns;
  const ciLow = reference.ci_low_ns / candidate.ci_high_ns;
  const ciHigh = reference.ci_high_ns / candidate.ci_low_ns;
  return {id, N, D, reference_boundary: reference.boundary,
    candidate_boundary: candidate.boundary, speedup: ratio,
    ci_low: ciLow, ci_high: ciHigh,
    outcome: ciLow > 1 ? "supported" : ciHigh < 1 ? "rejected" : "inconclusive"};
}

function universalOutcome(rows) {
  if (rows.some(row => row.outcome === "rejected")) return "rejected";
  if (rows.every(row => row.outcome === "supported")) return "supported";
  return "inconclusive";
}

function breakEven(reference, prepared, construction, referenceId, N, D) {
  const delta = reference.median_ns - prepared.median_ns;
  const equalityQueries = delta > 0 ? construction.median_ns / delta : null;
  const minimumQueries = delta > 0 ? Math.floor(equalityQueries) + 1 : null;
  const conservativeDelta = reference.ci_low_ns - prepared.ci_high_ns;
  const conservativeMinimumQueries = conservativeDelta > 0
    ? Math.floor(construction.ci_high_ns / conservativeDelta) + 1 : null;
  return {N, D, reference: referenceId,
    reference_boundary: reference.boundary,
    candidate_boundary: prepared.boundary,
    construction_boundary: construction.boundary,
    construction_median_ns: construction.median_ns,
    prepared_median_ns: prepared.median_ns,
    reference_median_ns: reference.median_ns,
    equality_queries: equalityQueries,
    minimum_integer_queries_for_speedup_gt_1: minimumQueries,
    conservative_minimum_queries: conservativeMinimumQueries,
    outcome: delta <= 0 ? "no-finite-point-break-even" :
      conservativeDelta <= 0 ? "finite-point-estimate-uncertainty-inconclusive" :
      "finite-break-even-supported",
    persistent_resource_destruction: "excluded; construction-plus-query amortization, not complete lifetime cost"};
}

function writeCsv(file, rows, fields) {
  fs.writeFileSync(file, fields.join(",") + "\n" +
    rows.map(row => fields.map(field => row[field] ?? "").join(",")).join("\n") + "\n");
}

function analyze(directory) {
  const manifest = JSON.parse(fs.readFileSync(path.join(directory, "manifest.json"), "utf8"));
  if (manifest.schema_version !== "S13-BENCHMARK-MANIFEST-v1") fail("manifest schema mismatch");
  const environmentStatusPath = path.join(directory, "environment-status.json");
  const environmentStatus = fs.existsSync(environmentStatusPath)
    ? JSON.parse(fs.readFileSync(environmentStatusPath, "utf8"))
    : {status: "missing-environment-status"};
  const gate = JSON.parse(fs.readFileSync(path.join(directory, "correctness-gate.json"), "utf8"));
  if (!fs.existsSync(manifest.freeze_path)) fail("manifest freeze path is unavailable");
  const freezeRows = parseCsv(manifest.freeze_path);
  const rows = parseCsv(path.join(directory, "raw_samples.csv"));
  const summaries = summarize(rows);
  validateCoverage(rows, summaries, manifest, gate, freezeRows);

  const comparisons = [];
  const breakEvens = [];
  for (const N of candidateCounts) for (const D of dimensions) {
    const cpu = findSummary(summaries, "CPU-E2E-RAW", N, D);
    const raw = findSummary(summaries, "BND-E2E-RAW", N, D);
    const prepared = findSummary(summaries, "BND-E2E-PREP", N, D);
    const construction = findSummary(summaries, "COST-PREPARED-READY-CONSTRUCTION", N, D);
    comparisons.push(comparison(cpu, prepared, "H1", N, D));
    comparisons.push(comparison(cpu, raw, "H2", N, D));
    comparisons.push(comparison(raw, prepared, "H3", N, D));
    breakEvens.push(breakEven(cpu, prepared, construction, "cpu-raw", N, D));
    breakEvens.push(breakEven(raw, prepared, construction, "v2-raw", N, D));
  }
  const hypothesisOutcomes = ["H1", "H2", "H3"].map(id => ({id,
    outcome: universalOutcome(comparisons.filter(row => row.id === id)),
    required_cases: 12}));
  if (environmentStatus.status !== "accepted-for-analysis") {
    for (const row of comparisons) row.outcome = "inconclusive";
    for (const row of hypothesisOutcomes) row.outcome = "inconclusive";
    for (const row of breakEvens) {
      if (row.outcome === "finite-break-even-supported") {
        row.outcome = "finite-point-estimate-environment-inconclusive";
      }
    }
  }

  const summaryFields = ["sample_role","scenario","boundary","variant","N","D",
    "seed","block_size","sample_count","percentile_method","median_ns","p5_ns",
    "p95_ns","ci_low_ns","ci_high_ns"];
  writeCsv(path.join(directory, "summary.csv"), summaries, summaryFields);
  fs.writeFileSync(path.join(directory, "summary.json"), JSON.stringify({
    schema_version: "S13-SUMMARY-v1",
    uncertainty: "deterministic percentile bootstrap, 2000 resamples, 95% median interval",
    summaries
  }, null, 2) + "\n");
  fs.writeFileSync(path.join(directory, "comparisons.json"), JSON.stringify({
    schema_version: "S13-COMPARISONS-v1", ratio: "median(reference)/median(candidate)",
    interval: "[reference_ci_low/candidate_ci_high, reference_ci_high/candidate_ci_low]",
    environment_status: environmentStatus.status,
    comparisons, hypothesis_outcomes: hypothesisOutcomes
  }, null, 2) + "\n");
  fs.writeFileSync(path.join(directory, "break-even.json"), JSON.stringify({
    schema_version: "S13-BREAK-EVEN-v1",
    persistent_resource_destruction: "excluded; results are not complete lifetime costs",
    formula: "floor(construction_median/(reference_median-prepared_median))+1",
    break_even: breakEvens
  }, null, 2) + "\n");
}

function selfTest() {
  if (percentile([1,2,3,4,5], 0.5) !== 3 ||
      Math.abs(percentile([1,2,3,4,5], 0.05) - 1.2) > 1e-12) {
    fail("percentile self-test failed");
  }
  const first = bootstrapMedian([1,2,3,4,5], "identity");
  const second = bootstrapMedian([1,2,3,4,5], "identity");
  if (first[0] !== second[0] || first[1] !== second[1]) {
    fail("bootstrap determinism self-test failed");
  }
  const reference = {boundary:"CPU-E2E-RAW", median_ns:100, ci_low_ns:95, ci_high_ns:105};
  const prepared = {boundary:"BND-E2E-PREP", median_ns:40, ci_low_ns:38, ci_high_ns:42};
  const construction = {boundary:"COST-PREPARED-READY-CONSTRUCTION",
    median_ns:150, ci_low_ns:140, ci_high_ns:160};
  const result = breakEven(reference, prepared, construction, "cpu-raw", 1, 1);
  if (result.minimum_integer_queries_for_speedup_gt_1 !== 3 ||
      result.conservative_minimum_queries !== 4 ||
      !result.persistent_resource_destruction.includes("not complete lifetime")) {
    fail("break-even self-test failed");
  }

  const directory = fs.mkdtempSync(path.join(os.tmpdir(), "s13-analysis-self-test-"));
  try {
    const freezePath = path.join(directory, "freeze.csv");
    const freezeLines = ["schema_version,variant,N,D,block_size"];
    for (const N of candidateCounts) for (const D of dimensions) {
      freezeLines.push(`S7-TUNING-FREEZE-v1,v2,${N},${D},256`);
    }
    fs.writeFileSync(freezePath, freezeLines.join("\n") + "\n");
    const manifest = {
      schema_version: "S13-BENCHMARK-MANIFEST-v1", revision: "r", working_tree_state: "clean",
      experiment_id: "self-test", experiment_config_sha256: "c", freeze_path: freezePath,
      freeze_sha256: "f", final_seed: "589684135938649225", warmups: 10,
      measured_repetitions: 100,
      persistent_resource_destruction: "excluded and not complete lifetime cost"
    };
    fs.writeFileSync(path.join(directory, "manifest.json"), JSON.stringify(manifest));
    fs.writeFileSync(path.join(directory, "correctness-gate.json"), JSON.stringify({
      schema_version: "S13-CORRECTNESS-GATE-v1", status: "passed-clean-candidate",
      revision: "r", working_tree_state: "clean", experiment_config_sha256: "c",
      freeze_sha256: "f", prepared_production_path_required: true
    }));
    fs.writeFileSync(path.join(directory, "environment-status.json"), JSON.stringify({
      schema_version: "S13-ENVIRONMENT-STATUS-v1", status: "accepted-for-analysis"
    }));
    const header = "schema_version,revision,tree_state,sample_role,workload,scenario," +
      "boundary,variant,N,D,seed,block_size,run_order,iteration,warmup," +
      "elapsed_ns,timer,status,resource_scope";
    const rawLines = [header];
    const definitions = [
      ["raw-one-shot","CPU-E2E-RAW","cpu",0,100,"not-applicable"],
      ["raw-one-shot","BND-E2E-RAW","v2",256,80,"call-owned-transient-cleanup-included"],
      ["prepared-reuse","BND-E2E-PREP","v2",256,40,"persistent-resources-reused-destruction-excluded"],
      ["prepared-construction","COST-PREP-DATASET-CONSTRUCTION","v2",256,100,"persistent-prepared-dataset-destruction-excluded"],
      ["prepared-construction","COST-WORKSPACE-CONSTRUCTION","v2",256,50,"persistent-workspace-destruction-excluded"],
      ["prepared-construction","COST-PREPARED-READY-CONSTRUCTION","v2",256,150,"persistent-resource-destruction-excluded-not-lifetime-cost"]
    ];
    let order = 0;
    for (const N of candidateCounts) for (const D of dimensions) {
      for (const [scenario,boundary,variant,block,elapsed,scope] of definitions) {
        for (let iteration = 0; iteration < 100; ++iteration) {
          rawLines.push(["S13-BENCHMARK-RAW-v1","r","clean","final","single-query",
            scenario,boundary,variant,N,D,"589684135938649225",block,order++,iteration,
            "false",elapsed,"steady_clock","completed",scope].join(","));
        }
      }
    }
    fs.writeFileSync(path.join(directory, "raw_samples.csv"), rawLines.join("\n") + "\n");
    analyze(directory);
    const comparisons = JSON.parse(fs.readFileSync(path.join(directory, "comparisons.json"), "utf8"));
    const breakEvenOutput = JSON.parse(fs.readFileSync(path.join(directory, "break-even.json"), "utf8"));
    if (!comparisons.hypothesis_outcomes.every(row => row.outcome === "supported") ||
        breakEvenOutput.break_even.length !== 24 ||
        !breakEvenOutput.persistent_resource_destruction.includes("not complete lifetime")) {
      fail("end-to-end analysis self-test failed");
    }
  } finally {
    fs.rmSync(directory, {recursive: true, force: true});
  }
}

const args = process.argv.slice(2);
if (args.length === 1 && args[0] === "--self-test") selfTest();
else if (args.length === 1) analyze(args[0]);
else fail("usage: analyze-s13-benchmarks.mjs --self-test | OUTPUT_DIRECTORY");
