#!/usr/bin/env node
import fs from "node:fs";
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
  return [row.sample_role, row.scenario, row.boundary, row.variant, row.N,
    row.D, row.batch_size, row.seed, row.block_size, row.query_set_id,
    row.resource_scope].join("|");
}

function summarize(rows) {
  const groups = new Map();
  const runOrders = new Set();
  for (const row of rows) {
    if (row.schema_version !== "S14-BATCH-BENCHMARK-RAW-v1" ||
        row.sample_role !== "final" || row.workload !== "batch-query-secondary" ||
        row.status !== "completed" || row.warmup !== "false" ||
        row.timer !== "steady_clock") {
      fail(`invalid raw sample identity for ${rowKey(row)}`);
    }
    const elapsed = Number(row.elapsed_ns);
    const iteration = Number(row.iteration);
    const runOrder = Number(row.run_order);
    if (!Number.isSafeInteger(elapsed) || elapsed <= 0) fail("invalid elapsed_ns");
    if (!Number.isSafeInteger(iteration) || iteration < 0 || iteration >= 100) {
      fail("invalid measured iteration");
    }
    if (!Number.isSafeInteger(runOrder) || runOrder < 0 || runOrders.has(runOrder)) {
      fail("invalid or duplicate global run order");
    }
    runOrders.add(runOrder);
    const group = groups.get(rowKey(row)) ?? {values: [], iterations: new Set()};
    if (group.iterations.has(iteration)) fail(`duplicate iteration for ${rowKey(row)}`);
    group.values.push(elapsed);
    group.iterations.add(iteration);
    groups.set(rowKey(row), group);
  }
  return [...groups].map(([identity, group]) => {
    const {values, iterations} = group;
    if (values.length !== 100 || iterations.size !== 100) {
      fail(`expected iterations 0..99 for ${identity}`);
    }
    const [sampleRole, scenario, boundary, variant, N, D, batchSize, seed,
      blockSize, querySetId, resourceScope] = identity.split("|");
    const [ciLow, ciHigh] = bootstrapMedian(values, identity);
    const median = percentile(values, 0.5);
    const p5 = percentile(values, 0.05);
    const p95 = percentile(values, 0.95);
    const batch = Number(batchSize);
    const isE2e = boundary.startsWith("BND-BATCH-E2E-");
    return {
      sample_role: sampleRole, scenario, boundary, variant,
      N: Number(N), D: Number(D), batch_size: batch, seed,
      block_size: Number(blockSize), query_set_id: querySetId,
      resource_scope: resourceScope,
      sample_count: values.length, percentile_method: "linear_n_minus_1",
      median_ns: median, p5_ns: p5, p95_ns: p95,
      ci_low_ns: ciLow, ci_high_ns: ciHigh,
      median_ns_per_query: isE2e ? median / batch : null,
      median_queries_per_second: isE2e ? batch * 1e9 / median : null,
      p5_queries_per_second: isE2e ? batch * 1e9 / p95 : null,
      p95_queries_per_second: isE2e ? batch * 1e9 / p5 : null,
      throughput_ci_low: isE2e ? batch * 1e9 / ciHigh : null,
      throughput_ci_high: isE2e ? batch * 1e9 / ciLow : null
    };
  }).sort((a, b) => rowKey(a).localeCompare(rowKey(b)));
}

const candidateCounts = [1024, 16384, 262144, 1048576];
const dimensions = [8, 32, 128];
const batchSizes = [1, 8, 32];
const e2eBoundaries = ["BND-BATCH-E2E-CPU",
  "BND-BATCH-E2E-V2-SEQUENTIAL-PREP", "BND-BATCH-E2E-V4"];
const v2SetupBoundaries = ["COST-PREP-DATASET-CONSTRUCTION",
  "COST-V2-WORKSPACE-CONSTRUCTION", "COST-V2-READY-CONSTRUCTION"];
const v4SetupBoundaries = ["COST-V4-BATCH-WORKSPACE-CONSTRUCTION",
  "COST-V4-BATCH-READY-CONSTRUCTION"];
const expectedScopes = new Map([
  ["BND-BATCH-E2E-CPU", "dataset-and-query-validation-included"],
  ["BND-BATCH-E2E-V2-SEQUENTIAL-PREP",
    "prepared-dataset-and-v2-workspace-reused-persistent-destruction-excluded"],
  ["BND-BATCH-E2E-V4",
    "prepared-dataset-and-v4-workspace-reused-persistent-destruction-excluded"],
  ["COST-PREP-DATASET-CONSTRUCTION",
    "persistent-prepared-dataset-destruction-excluded"],
  ["COST-V2-WORKSPACE-CONSTRUCTION",
    "persistent-v2-workspace-destruction-excluded"],
  ["COST-V2-READY-CONSTRUCTION",
    "persistent-resource-destruction-excluded-not-lifetime-cost"],
  ["COST-V4-BATCH-WORKSPACE-CONSTRUCTION",
    "persistent-v4-workspace-destruction-excluded"],
  ["COST-V4-BATCH-READY-CONSTRUCTION",
    "persistent-resource-destruction-excluded-not-lifetime-cost"]
]);
const expectedVariants = new Map([
  ["BND-BATCH-E2E-CPU", "cpu-batch"],
  ["BND-BATCH-E2E-V2-SEQUENTIAL-PREP", "v2-sequential"],
  ["BND-BATCH-E2E-V4", "v4-batch"]
]);

function findSummary(summaries, boundary, N, D, batchSize) {
  return summaries.find(row => row.boundary === boundary && row.N === N &&
    row.D === D && row.batch_size === batchSize);
}

function validateCoverage(rows, summaries, manifest, gate, freezeRows) {
  const revisions = new Set(rows.map(row => row.revision));
  const treeStates = new Set(rows.map(row => row.tree_state));
  if (revisions.size !== 1 || !revisions.has(manifest.revision)) fail("revision mismatch");
  if (treeStates.size !== 1 || !treeStates.has("clean") ||
      manifest.working_tree_state !== "clean") fail("clean tree identity required");
  if (manifest.warmups !== 10 || manifest.measured_repetitions !== 100 ||
      manifest.generator_id !== "S14-DISTINCT-BATCH-v1" ||
      JSON.stringify(manifest.batch_sizes) !== JSON.stringify(batchSizes)) {
    fail("sampling or workload contract mismatch");
  }
  if (gate.schema_version !== "S14-BATCH-CORRECTNESS-GATE-v1" ||
      gate.status !== "passed-clean-candidate" || gate.revision !== manifest.revision ||
      gate.working_tree_state !== "clean" ||
      gate.experiment_config_sha256 !== manifest.experiment_config_sha256 ||
      gate.freeze_sha256 !== manifest.freeze_sha256 ||
      gate.generator_id !== manifest.generator_id || gate.batch_configurations !== 36 ||
      gate.float64_queries !== 384) {
    fail("correctness gate identity/status mismatch");
  }
  if (!String(manifest.dataset_validation).includes("included in CPU batch") ||
      !String(manifest.persistent_resource_destruction).includes("not complete lifetime")) {
    fail("required cost-boundary disclosure missing");
  }
  for (const N of candidateCounts) for (const D of dimensions) {
    const frozen = freezeRows.find(row => row.schema_version === "S7-TUNING-FREEZE-v1" &&
      row.variant === "v2" && Number(row.N) === N && Number(row.D) === D);
    if (!frozen) fail(`missing frozen V2 configuration ${N}x${D}`);
    const blockSize = Number(frozen.block_size);
    for (const boundary of v2SetupBoundaries) {
      const summary = findSummary(summaries, boundary, N, D, 0);
      if (!summary || summary.variant !== "v2" || summary.block_size !== blockSize ||
          summary.resource_scope !== expectedScopes.get(boundary) ||
          summary.query_set_id !==
            `S14-DISTINCT-BATCH-v1-S${manifest.final_seed}-N${N}-D${D}-B0`) {
        fail(`missing V2 setup distribution ${boundary} ${N}x${D}`);
      }
    }
    for (const B of batchSizes) {
      const querySetId = `S14-DISTINCT-BATCH-v1-S${manifest.final_seed}-N${N}-D${D}-B${B}`;
      for (const boundary of e2eBoundaries) {
        const summary = findSummary(summaries, boundary, N, D, B);
        if (!summary || summary.query_set_id !== querySetId ||
            summary.variant !== expectedVariants.get(boundary)) {
          fail(`missing or mismatched E2E distribution ${boundary} ${N}x${D} B${B}`);
        }
        if ((summary.variant === "cpu-batch" && summary.block_size !== 0) ||
            (summary.variant !== "cpu-batch" && summary.block_size !== blockSize)) {
          fail(`block-size mismatch ${boundary} ${N}x${D} B${B}`);
        }
        if (summary.resource_scope !== expectedScopes.get(boundary)) {
          fail(`resource-scope mismatch ${boundary} ${N}x${D} B${B}`);
        }
      }
      for (const boundary of v4SetupBoundaries) {
        const summary = findSummary(summaries, boundary, N, D, B);
        if (!summary || summary.variant !== "v4" || summary.block_size !== blockSize ||
            summary.resource_scope !== expectedScopes.get(boundary) ||
            summary.query_set_id !== querySetId) {
          fail(`missing V4 setup distribution ${boundary} ${N}x${D} B${B}`);
        }
      }
    }
  }
  if (summaries.length !== 216) fail(`unexpected summary count ${summaries.length}`);
}

function comparison(reference, candidate, id, N, D, B) {
  const speedup = reference.median_ns / candidate.median_ns;
  const ciLow = reference.ci_low_ns / candidate.ci_high_ns;
  const ciHigh = reference.ci_high_ns / candidate.ci_low_ns;
  return {id, N, D, batch_size: B, reference_boundary: reference.boundary,
    candidate_boundary: candidate.boundary, speedup, ci_low: ciLow,
    ci_high: ciHigh,
    outcome: ciLow > 1 ? "supported" : ciHigh < 1 ? "rejected" : "inconclusive"};
}

function all36Outcome(rows) {
  if (rows.some(row => row.outcome === "rejected")) return "rejected";
  if (rows.every(row => row.outcome === "supported")) return "supported";
  return "inconclusive";
}

function minimumBatches(setupDelta, timingDelta) {
  return timingDelta > 0
    ? Math.max(1, Math.floor(setupDelta / timingDelta) + 1) : null;
}

function breakEven(reference, candidate, referenceSetup, candidateSetup,
                   referenceId, N, D, B) {
  const referenceSetupMedian = referenceSetup?.median_ns ?? 0;
  const referenceSetupLow = referenceSetup?.ci_low_ns ?? 0;
  const candidateSetupMedian = candidateSetup.median_ns;
  const timingDelta = reference.median_ns - candidate.median_ns;
  const setupDelta = candidateSetupMedian - referenceSetupMedian;
  const batches = minimumBatches(setupDelta, timingDelta);
  const conservativeTimingDelta = reference.ci_low_ns - candidate.ci_high_ns;
  const conservativeSetupDelta = candidateSetup.ci_high_ns - referenceSetupLow;
  const conservativeBatches = minimumBatches(
    conservativeSetupDelta, conservativeTimingDelta);
  return {N, D, batch_size: B, reference: referenceId,
    reference_boundary: reference.boundary,
    candidate_boundary: candidate.boundary,
    reference_setup_boundary: referenceSetup?.boundary ?? "host-data-ready-zero",
    candidate_setup_boundary: candidateSetup.boundary,
    reference_setup_median_ns: referenceSetupMedian,
    candidate_setup_median_ns: candidateSetupMedian,
    reference_batch_median_ns: reference.median_ns,
    candidate_batch_median_ns: candidate.median_ns,
    minimum_batches_for_speedup_gt_1: batches,
    minimum_queries_for_speedup_gt_1: batches === null ? null : batches * B,
    conservative_minimum_batches: conservativeBatches,
    conservative_minimum_queries: conservativeBatches === null
      ? null : conservativeBatches * B,
    outcome: timingDelta <= 0 ? "no-sustained-finite-break-even" :
      conservativeTimingDelta <= 0 ? "finite-point-estimate-uncertainty-inconclusive" :
      "finite-break-even-supported",
    persistent_resource_destruction:
      "excluded; construction-plus-batch amortization, not complete lifetime cost"};
}

function writeCsv(file, rows, fields) {
  fs.writeFileSync(file, fields.join(",") + "\n" +
    rows.map(row => fields.map(field => row[field] ?? "").join(",")).join("\n") + "\n");
}

function analyze(directory) {
  const manifest = JSON.parse(fs.readFileSync(path.join(directory, "manifest.json"), "utf8"));
  if (manifest.schema_version !== "S14-BATCH-BENCHMARK-MANIFEST-v1") {
    fail("manifest schema mismatch");
  }
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
    const v2Workspace = findSummary(summaries,
      "COST-V2-WORKSPACE-CONSTRUCTION", N, D, 0);
    for (const B of batchSizes) {
      const cpu = findSummary(summaries, "BND-BATCH-E2E-CPU", N, D, B);
      const sequential = findSummary(summaries,
        "BND-BATCH-E2E-V2-SEQUENTIAL-PREP", N, D, B);
      const v4 = findSummary(summaries, "BND-BATCH-E2E-V4", N, D, B);
      const v4Ready = findSummary(summaries,
        "COST-V4-BATCH-READY-CONSTRUCTION", N, D, B);
      const v4Workspace = findSummary(summaries,
        "COST-V4-BATCH-WORKSPACE-CONSTRUCTION", N, D, B);
      comparisons.push(comparison(cpu, v4, "H1", N, D, B));
      comparisons.push(comparison(sequential, v4, "H2", N, D, B));
      breakEvens.push(breakEven(cpu, v4, null, v4Ready,
        "cpu-batch", N, D, B));
      breakEvens.push(breakEven(sequential, v4, v2Workspace, v4Workspace,
        "v2-sequential", N, D, B));
    }
  }
  const hypothesisOutcomes = ["H1", "H2"].map(id => ({id,
    outcome: all36Outcome(comparisons.filter(row => row.id === id)),
    required_configurations: 36, classification: "optional-all-36"}));
  if (environmentStatus.status !== "accepted-for-analysis") {
    for (const row of comparisons) row.outcome = "inconclusive";
    for (const row of hypothesisOutcomes) row.outcome = "inconclusive";
    for (const row of breakEvens) {
      if (row.outcome === "finite-break-even-supported") {
        row.outcome = "finite-point-estimate-environment-inconclusive";
      }
    }
  }

  const summaryFields = ["sample_role", "scenario", "boundary", "variant",
    "N", "D", "batch_size", "seed", "block_size", "query_set_id",
    "resource_scope", "sample_count", "percentile_method", "median_ns", "p5_ns", "p95_ns",
    "ci_low_ns", "ci_high_ns", "median_ns_per_query",
    "median_queries_per_second", "p5_queries_per_second",
    "p95_queries_per_second", "throughput_ci_low", "throughput_ci_high"];
  writeCsv(path.join(directory, "summary.csv"), summaries, summaryFields);
  fs.writeFileSync(path.join(directory, "summary.json"), JSON.stringify({
    schema_version: "S14-BATCH-SUMMARY-v1",
    uncertainty: "deterministic percentile bootstrap, 2000 resamples, 95% median interval",
    throughput: "batch_size * 1e9 / elapsed_ns", summaries
  }, null, 2) + "\n");
  fs.writeFileSync(path.join(directory, "comparisons.json"), JSON.stringify({
    schema_version: "S14-BATCH-COMPARISONS-v1",
    ratio: "median(reference)/median(candidate)",
    interval: "[reference_ci_low/candidate_ci_high, reference_ci_high/candidate_ci_low]",
    primary_classification: "per-configuration",
    environment_status: environmentStatus.status,
    comparisons, hypothesis_outcomes: hypothesisOutcomes
  }, null, 2) + "\n");
  fs.writeFileSync(path.join(directory, "break-even.json"), JSON.stringify({
    schema_version: "S14-BATCH-BREAK-EVEN-v1",
    minimum_batches_formula:
      "max(1,floor((candidate_setup-reference_setup)/(reference_batch-candidate_batch))+1)",
    persistent_resource_destruction: "excluded; results are not complete lifetime costs",
    break_even: breakEvens
  }, null, 2) + "\n");
}

function selfTest() {
  if (percentile([1, 2, 3, 4, 5], 0.5) !== 3 ||
      Math.abs(percentile([1, 2, 3, 4, 5], 0.05) - 1.2) > 1e-12) {
    fail("percentile self-test failed");
  }
  const first = bootstrapMedian([1, 2, 3, 4, 5], "identity");
  const second = bootstrapMedian([1, 2, 3, 4, 5], "identity");
  if (first[0] !== second[0] || first[1] !== second[1]) {
    fail("bootstrap determinism self-test failed");
  }
  const reference = {boundary: "CPU", median_ns: 100, ci_low_ns: 95, ci_high_ns: 105};
  const candidate = {boundary: "V4", median_ns: 40, ci_low_ns: 38, ci_high_ns: 42};
  const setup = {boundary: "READY", median_ns: 150, ci_low_ns: 140, ci_high_ns: 160};
  const result = breakEven(reference, candidate, null, setup, "cpu", 1, 1, 1);
  if (result.minimum_batches_for_speedup_gt_1 !== 3 ||
      result.conservative_minimum_batches !== 4 ||
      comparison(reference, candidate, "H", 1, 1, 1).outcome !== "supported") {
    fail("comparison or break-even self-test failed");
  }
  if (all36Outcome([{outcome: "supported"}, {outcome: "inconclusive"}]) !==
      "inconclusive" || minimumBatches(0, 0) !== null) {
    fail("classification self-test failed");
  }
  const synthetic = Array.from({length: 100}, (_, iteration) => ({
    schema_version: "S14-BATCH-BENCHMARK-RAW-v1", revision: "r",
    tree_state: "clean", sample_role: "final", workload: "batch-query-secondary",
    scenario: "batch-search", boundary: "BND-BATCH-E2E-CPU",
    variant: "cpu-batch", N: "1024", D: "8", batch_size: "1",
    seed: "1", block_size: "0", query_set_id: "q", run_order: String(iteration),
    iteration: String(iteration), warmup: "false", elapsed_ns: String(100 + iteration),
    timer: "steady_clock", status: "completed",
    resource_scope: "dataset-and-query-validation-included"
  }));
  const summarized = summarize(synthetic);
  if (summarized.length !== 1 || summarized[0].sample_count !== 100 ||
      summarized[0].resource_scope !== "dataset-and-query-validation-included") {
    fail("raw-sample identity self-test failed");
  }
  let duplicateRejected = false;
  try { summarize(synthetic.map(row => ({...row, run_order: "0"}))); }
  catch { duplicateRejected = true; }
  if (!duplicateRejected) fail("duplicate run-order self-test failed");
  const coverageSummaries = [];
  const freezeRows = [];
  const addSummary = (boundary, variant, N, D, B, blockSize) =>
    coverageSummaries.push({boundary, variant, N, D, batch_size: B,
      block_size: blockSize,
      query_set_id: `S14-DISTINCT-BATCH-v1-S1-N${N}-D${D}-B${B}`,
      resource_scope: expectedScopes.get(boundary)});
  for (const N of candidateCounts) for (const D of dimensions) {
    freezeRows.push({schema_version: "S7-TUNING-FREEZE-v1", variant: "v2",
      N: String(N), D: String(D), block_size: "128"});
    for (const boundary of v2SetupBoundaries) addSummary(boundary, "v2", N, D, 0, 128);
    for (const B of batchSizes) {
      for (const boundary of e2eBoundaries) {
        addSummary(boundary, expectedVariants.get(boundary), N, D, B,
          boundary === "BND-BATCH-E2E-CPU" ? 0 : 128);
      }
      for (const boundary of v4SetupBoundaries) addSummary(boundary, "v4", N, D, B, 128);
    }
  }
  const manifest = {revision: "r", working_tree_state: "clean", warmups: 10,
    measured_repetitions: 100, generator_id: "S14-DISTINCT-BATCH-v1",
    batch_sizes: batchSizes, final_seed: "1", experiment_config_sha256: "c",
    freeze_sha256: "f", dataset_validation: "included in CPU batch",
    persistent_resource_destruction: "excluded; not complete lifetime"};
  const gate = {schema_version: "S14-BATCH-CORRECTNESS-GATE-v1",
    status: "passed-clean-candidate", revision: "r", working_tree_state: "clean",
    experiment_config_sha256: "c", freeze_sha256: "f",
    generator_id: "S14-DISTINCT-BATCH-v1", batch_configurations: 36,
    float64_queries: 384};
  validateCoverage([{revision: "r", tree_state: "clean"}], coverageSummaries,
    manifest, gate, freezeRows);
  let coverageRejected = false;
  try { validateCoverage([{revision: "r", tree_state: "clean"}],
    coverageSummaries.slice(1), manifest, gate, freezeRows); }
  catch { coverageRejected = true; }
  if (!coverageRejected) fail("coverage omission self-test failed");
  console.log("S14 batch analysis self-test: passed");
}

try {
  if (process.argv[2] === "--self-test") selfTest();
  else {
    if (!process.argv[2]) fail("usage: analyze-s14-batch-benchmarks.mjs DIRECTORY");
    analyze(process.argv[2]);
  }
} catch (error) {
  console.error(`S14 batch analysis: FAIL: ${error.message}`);
  process.exitCode = 1;
}
