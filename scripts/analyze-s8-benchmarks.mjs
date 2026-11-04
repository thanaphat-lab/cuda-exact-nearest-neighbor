#!/usr/bin/env node
import fs from "node:fs";
import path from "node:path";

function fail(message) { throw new Error(message); }
function parseCsv(file) {
  const lines = fs.readFileSync(file, "utf8").trim().split(/\r?\n/);
  const header = lines.shift().split(",");
  return lines.filter(Boolean).map(line => {
    const values = line.split(",");
    return Object.fromEntries(header.map((key, index) => [key, values[index]]));
  });
}
function percentile(values, p) {
  const sorted = [...values].sort((a, b) => a - b);
  if (!sorted.length) fail("empty distribution");
  const position = p * (sorted.length - 1);
  const lower = Math.floor(position), upper = Math.ceil(position);
  return sorted[lower] + (sorted[upper] - sorted[lower]) * (position - lower);
}
function hash(text) {
  let value = 2166136261 >>> 0;
  for (const byte of Buffer.from(text)) {
    value ^= byte;
    value = Math.imul(value, 16777619) >>> 0;
  }
  return value || 1;
}
function bootstrapMedian(values, identity) {
  let state = hash(identity);
  const random = () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    return state / 0x100000000;
  };
  const medians = [];
  for (let sample = 0; sample < 2000; ++sample) {
    const resample = Array.from({length: values.length},
      () => values[Math.floor(random() * values.length)]);
    medians.push(percentile(resample, 0.5));
  }
  return [percentile(medians, 0.025), percentile(medians, 0.975)];
}
function key(row) {
  return [row.sample_role, row.variant, row.transpose_impl, row.boundary,
          row.N, row.D, row.seed, row.block_size].join("|");
}
function summarize(rows) {
  const groups = new Map();
  for (const row of rows) {
    if (row.status !== "completed" || row.warmup !== "false") fail("invalid sample row");
    const group = groups.get(key(row)) ?? [];
    group.push(Number(row.elapsed_ns));
    groups.set(key(row), group);
  }
  return [...groups].map(([identity, values]) => {
    const [role, variant, implementation, boundary, N, D, seed, block] =
      identity.split("|");
    if (values.length !== 100) fail(`expected 100 samples for ${identity}`);
    const ci = bootstrapMedian(values, identity);
    return {sample_role: role, variant, transpose_impl: implementation, boundary,
      N: Number(N), D: Number(D), seed, block_size: Number(block),
      sample_count: values.length, percentile_method: "linear_n_minus_1",
      median_ns: percentile(values, 0.5), p5_ns: percentile(values, 0.05),
      p95_ns: percentile(values, 0.95), ci_low_ns: ci[0], ci_high_ns: ci[1]};
  }).sort((a, b) => key(a).localeCompare(key(b)));
}
function writeSummary(directory, summaries) {
  const fields = ["sample_role","variant","transpose_impl","boundary","N","D","seed",
    "block_size","sample_count","percentile_method","median_ns","p5_ns","p95_ns",
    "ci_low_ns","ci_high_ns"];
  fs.writeFileSync(path.join(directory, "summary.csv"),
    fields.join(",") + "\n" + summaries.map(row => fields.map(f => row[f]).join(",")).join("\n") + "\n");
  fs.writeFileSync(path.join(directory, "summary.json"),
    JSON.stringify({schema_version:"S8-SUMMARY-v1",
      uncertainty:"deterministic percentile bootstrap, 2000 resamples, 95% median interval",
      summaries}, null, 2) + "\n");
}
function validateIdentity(directory, rows, role) {
  const manifest = JSON.parse(fs.readFileSync(path.join(directory, "manifest.json"), "utf8"));
  if (manifest.sample_role !== role) fail("sample role mismatch");
  if (manifest.warmups !== 10 || manifest.repetitions !== 100) fail("sampling contract mismatch");
  const revisions = new Set(rows.map(row => row.revision));
  const states = new Set(rows.map(row => row.tree_state));
  if (revisions.size !== 1 || !revisions.has(manifest.revision)) fail("revision mismatch");
  if (states.size !== 1 || !states.has(manifest.working_tree_state)) fail("tree-state mismatch");
  return manifest;
}
function tune(directory) {
  const rows = parseCsv(path.join(directory, "raw_samples.csv"));
  const manifest = validateIdentity(directory, rows, "tuning");
  const summaries = summarize(rows);
  writeSummary(directory, summaries);
  const candidates = ["32x32", "64x32", "64x64"];
  const variants = ["v1", "v2", "v3"];
  const ns = [1024,16384,262144,1048576], ds = [8,32,128];
  const freeze = [];
  for (const variant of variants) for (const N of ns) for (const D of ds) {
    const available = candidates.map(implementation => summaries.find(row =>
      row.variant === variant && row.transpose_impl === implementation &&
      row.boundary === "BND-HOST-TRANSPOSE" && row.N === N && row.D === D));
    if (available.some(value => !value)) fail(`missing tuning distribution ${variant} ${N}x${D}`);
    let winner = available[0];
    for (const candidate of available.slice(1)) {
      if (candidate.median_ns < winner.median_ns) winner = candidate;
    }
    freeze.push({variant,N,D,transpose_impl:winner.transpose_impl,
      selection_boundary:"BND-HOST-TRANSPOSE",tuning_median_ns:winner.median_ns});
  }
  const fields = ["schema_version","experiment_id","revision","tree_state",
                  "config_sha256","tuning_seed","variant","N","D",
                  "transpose_impl","selection_boundary","tuning_median_ns"];
  fs.writeFileSync(path.join(directory, "transpose-freeze.csv"), fields.join(",") + "\n" +
    freeze.map(row => ["S8-TRANSPOSE-TUNING-FREEZE-v1",manifest.experiment_id,
      manifest.revision,manifest.working_tree_state,manifest.config_sha256,
      manifest.tuning_seed,row.variant,row.N,row.D,row.transpose_impl,
      row.selection_boundary,row.tuning_median_ns].join(",")).join("\n") + "\n");
}
function final(directory, freezeFile) {
  const rows = parseCsv(path.join(directory, "raw_samples.csv"));
  const manifest = validateIdentity(directory, rows, "final");
  const summaries = summarize(rows);
  writeSummary(directory, summaries);
  const freezeRows = parseCsv(freezeFile);
  const results = [];
  for (const frozen of freezeRows) {
    if (frozen.experiment_id !== manifest.experiment_id ||
        frozen.revision !== manifest.revision ||
        frozen.tree_state !== manifest.working_tree_state ||
        frozen.config_sha256 !== manifest.config_sha256 ||
        frozen.tuning_seed !== manifest.tuning_seed) {
      fail("transpose freeze identity does not match final run");
    }
    const find = (implementation, boundary) => summaries.find(row =>
      row.variant === frozen.variant && row.transpose_impl === implementation &&
      row.boundary === boundary && row.N === Number(frozen.N) && row.D === Number(frozen.D));
    for (const boundary of ["BND-HOST-TRANSPOSE","BND-E2E-RAW"]) {
      const reference = find("original", boundary);
      const candidate = find(frozen.transpose_impl, boundary);
      if (!reference || !candidate) fail("missing final comparison");
      const low = reference.ci_low_ns / candidate.ci_high_ns;
      const high = reference.ci_high_ns / candidate.ci_low_ns;
      results.push({variant:frozen.variant,N:Number(frozen.N),D:Number(frozen.D),
        candidate:frozen.transpose_impl,boundary,
        ratio:reference.median_ns/candidate.median_ns,ci_low:low,ci_high:high,
        outcome:low>1?"supported":high<1?"rejected":"inconclusive"});
    }
  }
  const variants = ["v1","v2","v3"], implementations = ["original","32x32","64x32","64x64"];
  for (const N of [1024,16384,262144,1048576]) for (const D of [8,32,128]) {
    const cpu = summaries.find(row => row.variant === "cpu" &&
      row.boundary === "CPU-E2E-RAW" && row.N === N && row.D === D);
    if (!cpu) fail("missing CPU final distribution");
    for (const variant of variants) for (const implementation of implementations) {
      const candidate = summaries.find(row => row.variant === variant &&
        row.transpose_impl === implementation && row.boundary === "BND-E2E-RAW" &&
        row.N === N && row.D === D);
      if (!candidate) fail("missing GPU final distribution");
      const low = cpu.ci_low_ns / candidate.ci_high_ns;
      const high = cpu.ci_high_ns / candidate.ci_low_ns;
      results.push({variant,N,D,candidate:implementation,boundary:"CPU-E2E-RAW/BND-E2E-RAW",
        ratio:cpu.median_ns/candidate.median_ns,ci_low:low,ci_high:high,
        outcome:low>1?"supported":high<1?"rejected":"inconclusive"});
    }
  }
  fs.writeFileSync(path.join(directory, "comparisons.json"),
    JSON.stringify({schema_version:"S8-COMPARISONS-v1",results}, null, 2) + "\n");
}
const args = process.argv.slice(2);
if (args[0] === "--self-test" && args.length === 1) {
  if (percentile([1,2,3,4,5], 0.5) !== 3) fail("median self-test failed");
  const first = bootstrapMedian([1,2,3,4,5], "identity");
  const second = bootstrapMedian([1,2,3,4,5], "identity");
  if (first[0] !== second[0] || first[1] !== second[1]) {
    fail("bootstrap determinism self-test failed");
  }
} else if (args[0] === "--tuning" && args.length === 2) tune(args[1]);
else if (args[0] === "--final" && args.length === 4 && args[2] === "--freeze") final(args[1], args[3]);
else fail("usage: analyze-s8-benchmarks.mjs --self-test | --tuning DIR | --final DIR --freeze FILE");
