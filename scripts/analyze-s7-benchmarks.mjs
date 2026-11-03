import fs from "node:fs";
import path from "node:path";
import crypto from "node:crypto";

function fail(message) {
  throw new Error(message);
}

function parseCsv(text) {
  const rows = [];
  let row = [];
  let field = "";
  let quoted = false;
  for (let i = 0; i < text.length; ++i) {
    const c = text[i];
    if (quoted) {
      if (c === '"' && text[i + 1] === '"') {
        field += '"';
        ++i;
      } else if (c === '"') {
        quoted = false;
      } else {
        field += c;
      }
    } else if (c === '"') {
      quoted = true;
    } else if (c === ",") {
      row.push(field);
      field = "";
    } else if (c === "\n") {
      row.push(field.replace(/\r$/, ""));
      if (row.some((value) => value.length !== 0)) rows.push(row);
      row = [];
      field = "";
    } else {
      field += c;
    }
  }
  if (quoted) fail("unterminated CSV quote");
  if (field.length || row.length) {
    row.push(field);
    rows.push(row);
  }
  const header = rows.shift();
  if (!header) fail("empty CSV");
  return rows.map((values) => Object.fromEntries(header.map((name, i) => [name, values[i]])));
}

function xml(value) {
  return String(value).replaceAll("&", "&amp;").replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;").replaceAll('"', "&quot;");
}

function writeLatencySvg(file, rows, title, boundary) {
  const selected = rows.filter((row) => row.boundary === boundary ||
    (boundary === "BND-E2E-RAW" && row.boundary === "CPU-E2E-RAW"));
  const width = 1100;
  const height = 720;
  const margin = { left: 90, right: 30, top: 70, bottom: 90 };
  const plotWidth = width - margin.left - margin.right;
  const plotHeight = height - margin.top - margin.bottom;
  const values = selected.map((row) => Number(row.median_ns)).filter((value) => value > 0);
  if (!values.length) fail(`no ${boundary} rows for SVG`);
  const minY = Math.min(...values);
  const maxY = Math.max(...values);
  const y = (value) => margin.top + plotHeight *
    (1 - (Math.log(value) - Math.log(minY)) / Math.max(1e-12, Math.log(maxY) - Math.log(minY)));
  const ns = [...new Set(selected.map((row) => Number(row.N)))].sort((a, b) => a - b);
  const x = (value) => margin.left + plotWidth * ns.indexOf(value) / Math.max(1, ns.length - 1);
  const colors = { cpu: "#111827", v0: "#dc2626", v1: "#ea580c", v2: "#2563eb", v3: "#16a34a" };
  let svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">`;
  svg += `<rect width="100%" height="100%" fill="white"/><text x="${width / 2}" y="35" text-anchor="middle" font-family="sans-serif" font-size="22">${xml(title)}</text>`;
  svg += `<line x1="${margin.left}" y1="${margin.top + plotHeight}" x2="${margin.left + plotWidth}" y2="${margin.top + plotHeight}" stroke="#374151"/><line x1="${margin.left}" y1="${margin.top}" x2="${margin.left}" y2="${margin.top + plotHeight}" stroke="#374151"/>`;
  ns.forEach((value) => svg += `<text x="${x(value)}" y="${margin.top + plotHeight + 28}" text-anchor="middle" font-family="sans-serif" font-size="12">${value}</text>`);
  const groups = new Map();
  for (const row of selected) {
    const key = `${row.variant}|D=${row.D}`;
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(row);
  }
  let legendY = margin.top;
  for (const [key, group] of groups) {
    group.sort((a, b) => Number(a.N) - Number(b.N));
    const [variant] = key.split("|");
    const dash = key.endsWith("D=8") ? "" : key.endsWith("D=32") ? "6 4" : "2 4";
    const points = group.map((row) => `${x(Number(row.N))},${y(Number(row.median_ns))}`).join(" ");
    svg += `<polyline points="${points}" fill="none" stroke="${colors[variant] ?? "#7c3aed"}" stroke-width="2" stroke-dasharray="${dash}"/>`;
    if (legendY < margin.top + plotHeight) {
      svg += `<text x="${margin.left + 12}" y="${legendY + 16}" font-family="sans-serif" font-size="11" fill="${colors[variant] ?? "#7c3aed"}">${xml(key)}</text>`;
      legendY += 15;
    }
  }
  svg += `<text x="${width / 2}" y="${height - 25}" text-anchor="middle" font-family="sans-serif">N (required cases)</text><text transform="translate(22 ${height / 2}) rotate(-90)" text-anchor="middle" font-family="sans-serif">median latency (ns, log scale)</text></svg>`;
  fs.writeFileSync(file, svg);
}

function buildComparisons(rows, finalRole) {
  const result = [];
  const cases = [...new Set(rows.filter((row) => row.sample_role === finalRole)
    .map((row) => `${row.N}|${row.D}`))];
  const find = (n, d, variant, boundary) => rows.find((row) =>
    row.sample_role === finalRole && row.N === n && row.D === d &&
    row.variant === variant && row.boundary === boundary);
  const add = (n, d, name, numerator, denominator) => {
    if (!numerator || !denominator) return;
    const numeratorMedian = Number(numerator.median_ns);
    const denominatorMedian = Number(denominator.median_ns);
    result.push({
      comparison: name, N: Number(n), D: Number(d),
      numerator_variant: numerator.variant, denominator_variant: denominator.variant,
      numerator_boundary: numerator.boundary,
      denominator_boundary: denominator.boundary,
      ratio: numeratorMedian / denominatorMedian,
      ci_low: Number(numerator.ci_low_ns) / Number(denominator.ci_high_ns),
      ci_high: Number(numerator.ci_high_ns) / Number(denominator.ci_low_ns),
      uncertainty_method: "conservative ratio of separate bootstrap median intervals",
    });
  };
  for (const key of cases) {
    const [n, d] = key.split("|");
    const cpu = find(n, d, "cpu", "CPU-E2E-RAW");
    for (const gpu of ["v0", "v1", "v2", "v3"]) {
      add(n, d, `CPU/${gpu} raw speedup`, cpu, find(n, d, gpu, "BND-E2E-RAW"));
    }
    add(n, d, "V0/V1 kernel ratio", find(n, d, "v0", "BND-KERNEL"), find(n, d, "v1", "BND-KERNEL"));
    add(n, d, "V1/V2 kernel ratio", find(n, d, "v1", "BND-KERNEL"), find(n, d, "v2", "BND-KERNEL"));
    add(n, d, "V2/V3 kernel ratio", find(n, d, "v2", "BND-KERNEL"), find(n, d, "v3", "BND-KERNEL"));
  }
  return result;
}

function writeComparisons(root, comparisons) {
  const header = ["schema_version", "comparison", "N", "D", "numerator_variant",
    "denominator_variant", "numerator_boundary", "denominator_boundary", "ratio",
    "ci_low", "ci_high", "uncertainty_method"];
  const csv = [header.join(",")];
  for (const row of comparisons) {
    csv.push(["S7-BENCHMARK-COMPARISON-v1", row.comparison, row.N, row.D,
      row.numerator_variant, row.denominator_variant,
      row.numerator_boundary, row.denominator_boundary, row.ratio,
      row.ci_low, row.ci_high, row.uncertainty_method]
      .map((value) => `"${String(value).replaceAll('"', '""')}"`).join(","));
  }
  fs.writeFileSync(path.join(root, "comparisons.csv"), csv.join("\n") + "\n");
  fs.writeFileSync(path.join(root, "comparisons.json"), JSON.stringify({
    schema_version: "S7-BENCHMARK-COMPARISON-v1", rows: comparisons,
  }, null, 2) + "\n");
}

function writeRatioSvg(file, comparisons) {
  const width = 1100;
  const height = 500;
  const rows = comparisons.slice(0, 48);
  const max = Math.max(1, ...rows.map((row) => row.ratio));
  let svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}"><rect width="100%" height="100%" fill="white"/><text x="${width / 2}" y="30" text-anchor="middle" font-family="sans-serif" font-size="20">S7 compatible-boundary ratios</text>`;
  rows.forEach((row, i) => {
    const y = 48 + i * 9;
    const bar = 650 * row.ratio / max;
    svg += `<rect x="390" y="${y}" width="${bar}" height="6" fill="#2563eb"/><text x="385" y="${y + 6}" text-anchor="end" font-family="sans-serif" font-size="8">${xml(`${row.comparison} N${row.N} D${row.D}`)}</text>`;
  });
  svg += `</svg>`;
  fs.writeFileSync(file, svg);
}

function writeStageSvg(file, rows) {
  const stages = rows.filter((row) => row.boundary.startsWith("STAGE-") &&
    row.sample_role.endsWith("final"));
  const width = 1100;
  const height = 600;
  const shown = stages.slice(0, 60);
  const max = Math.max(1, ...shown.map((row) => Number(row.median_ns)));
  let svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}"><rect width="100%" height="100%" fill="white"/><text x="${width / 2}" y="30" text-anchor="middle" font-family="sans-serif" font-size="20">S7 stage medians (diagnostic; not additive)</text>`;
  shown.forEach((row, i) => {
    const y = 48 + i * 8;
    const bar = 650 * Number(row.median_ns) / max;
    svg += `<rect x="420" y="${y}" width="${bar}" height="5" fill="#16a34a"/><text x="415" y="${y + 5}" text-anchor="end" font-family="sans-serif" font-size="7">${xml(`${row.variant} ${row.boundary} N${row.N} D${row.D}`)}</text>`;
  });
  svg += `</svg>`;
  fs.writeFileSync(file, svg);
}

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, "utf8"));
}

function sha256(file) {
  return crypto.createHash("sha256").update(fs.readFileSync(file)).digest("hex");
}

function requireString(object, field, context) {
  if (typeof object?.[field] !== "string" || !object[field].trim() ||
      object[field] === "unknown") {
    fail(`${context}.${field} is mandatory`);
  }
}

function validateExperimentConfig(config) {
  if (config.schema_version !== "S7-EXPERIMENT-CONFIG-v1") fail("invalid experiment config schema");
  requireString(config, "experiment_id", "experiment_config");
  requireString(config, "approved_design", "experiment_config");
  if (config.expected_revision_source !== "owner-supplied runner argument") {
    fail("experiment config must not embed the final expected revision");
  }
  if (!config.approved_numerical_policy?.sha256?.match(/^[0-9a-f]{64}$/)) {
    fail("approved numerical policy hash is missing");
  }
  if (String(config.workload?.tuning_seed) === String(config.workload?.final_seed)) {
    fail("tuning and final seeds must differ");
  }
  if (config.sampling?.warmups !== 10 || config.sampling?.measured_repetitions !== 100) {
    fail("experiment config must predeclare 10 warmups and 100 repetitions");
  }
  if (!Array.isArray(config.hypotheses) || config.hypotheses.length !== 4 ||
      config.hypotheses.map((value) => value.id).join(",") !== "H1,H2,H3,H4") {
    fail("experiment config must define H1 through H4");
  }
  for (const hypothesis of config.hypotheses) {
    for (const field of ["statement", "reference_variant", "reference_boundary",
      "candidate_variant", "candidate_boundary", "scope"]) {
      requireString(hypothesis, field, hypothesis.id);
    }
    if (hypothesis.scope !== "all-12-required-cases") fail(`${hypothesis.id} has invalid scope`);
  }
  if (config.environment_policy?.telemetry_required !== true ||
      !(Number(config.environment_policy?.telemetry_interval_ms) >= 100)) {
    fail("continuous telemetry policy is mandatory");
  }
  if (!(Number(config.environment_policy?.maximum_telemetry_gap_intervals) >= 1)) {
    fail("telemetry gap policy is mandatory");
  }
  if (!Array.isArray(config.environment_policy?.experiment_process_names) ||
      !config.environment_policy.experiment_process_names.length) {
    fail("expected experiment process identities are mandatory");
  }
}

function loadJsonLines(file) {
  return fs.readFileSync(file, "utf8").split(/\r?\n/).filter((line) => line.trim())
    .map((line, index) => {
      try { return JSON.parse(line); }
      catch { fail(`invalid JSONL row ${index + 1} in ${file}`); }
    });
}

function validateIdentity(manifest, metadata, config, observedConfigHash) {
  if (observedConfigHash !== manifest.experiment_config_sha256 ||
      observedConfigHash !== metadata.experiment_config_sha256) {
    fail("experiment configuration hash mismatch");
  }
  if (manifest.experiment_id !== config.experiment_id ||
      metadata.experiment_id !== config.experiment_id) fail("experiment identity mismatch");
  if (manifest.revision !== manifest.owner_expected_revision ||
      manifest.revision !== metadata.owner_expected_revision ||
      manifest.revision !== metadata.observed_head) {
    fail("owner/compiled/observed revision mismatch");
  }
  if (String(manifest.tuning_seed) !== String(config.workload.tuning_seed) ||
      String(manifest.final_seed) !== String(config.workload.final_seed)) {
    fail("manifest seeds do not match the predeclared configuration");
  }
}

function validateContext(root, sharedRoot, manifest, config) {
  const capture = path.join(sharedRoot, "environment-capture");
  const metadataPath = path.join(capture, "run-metadata.json");
  const hostPath = path.join(capture, "host-environment.json");
  const approvalPath = path.join(capture, "environment-approval.json");
  const configPath = path.join(capture, "experiment-config.json");
  const telemetryPath = path.join(capture, "telemetry", "gpu-telemetry.jsonl");
  const processPath = path.join(capture, "telemetry", "gpu-processes.jsonl");
  const errorPath = path.join(capture, "telemetry", "telemetry-errors.log");
  const launchErrorPath = path.join(capture, "telemetry-launch-errors.log");
  for (const file of [metadataPath, hostPath, approvalPath, configPath, telemetryPath,
    processPath, errorPath, path.join(capture, "nvidia-smi-before.xml"),
    path.join(capture, "nvidia-smi-after.xml"), path.join(capture, "commands.jsonl"),
    path.join(capture, "CMakeCache.txt"), path.join(capture, "build.ninja"), launchErrorPath]) {
    if (!fs.existsSync(file)) fail(`missing mandatory AC-014 artifact: ${file}`);
  }
  const metadata = readJson(metadataPath);
  const host = readJson(hostPath);
  const approval = readJson(approvalPath);
  validateIdentity(manifest, metadata, config, sha256(configPath));
  if (sha256(path.join(root, "experiment-config.json")) !== sha256(configPath)) {
    fail("benchmark-local and shared experiment configurations differ");
  }
  if (approval.experiment_id !== config.experiment_id) fail("environment approval identity mismatch");
  for (const field of ["captured_at", "timezone", "approved_design", "generator_id",
    "cuda_architecture", "build_flags", "stream_policy"]) {
    requireString(manifest, field, "manifest");
  }
  for (const field of ["captured_at", "timezone", "operator", "build_type",
    "cuda_architecture", "build_flags", "build_ninja_sha256", "cmake_cache_sha256",
    "fast_math", "generator_id", "stream_policy"]) {
    requireString(metadata, field, "run_metadata");
  }
  for (const field of ["operating_system", "os_version", "os_build", "cpu",
    "host_memory_bytes", "gpu_name", "gpu_uuid", "gpu_memory", "driver_version",
    "wddm_or_tcc", "pcie_current_generation", "pcie_current_width", "power_limit"]) {
    requireString(host, field, "host_environment");
  }
  if (manifest.fast_math !== "disabled-by-target-contract" ||
      !String(metadata.fast_math).startsWith("disabled")) fail("fast math is not verified disabled");
  const dryRun = metadata.dry_run === true;
  if (!dryRun && (manifest.working_tree_state !== "clean" ||
      metadata.working_tree_state !== "clean")) fail("acceptance evidence is not clean-revision evidence");
  if (!dryRun && (approval.owner_environment_ready !== true ||
      approval.power_clock_settings_unchanged !== true)) fail("owner environment approval is incomplete");
  if (!dryRun && approval.expected_gpu_uuid !== host.gpu_uuid) fail("approved GPU UUID mismatch");
  const normalizeUuid = (value) => String(value).toLowerCase().replace(/^gpu-/, "").replaceAll("-", "");
  if (normalizeUuid(manifest.gpu_uuid) !== normalizeUuid(host.gpu_uuid)) {
    fail("benchmark and host GPU identities differ");
  }
  const telemetryErrors = fs.readFileSync(errorPath, "utf8").trim();
  if (telemetryErrors) fail(`telemetry reported errors: ${telemetryErrors}`);
  const launchErrors = fs.readFileSync(launchErrorPath, "utf8").trim();
  if (launchErrors) fail(`telemetry helper reported startup errors: ${launchErrors}`);
  const telemetry = loadJsonLines(telemetryPath);
  const processes = loadJsonLines(processPath);
  if (!telemetry.length || !processes.length) fail("continuous telemetry is empty");
  if (!dryRun && telemetry.length < 2) fail("continuous telemetry requires multiple samples");
  if (telemetry.some((row) => row.uuid !== host.gpu_uuid)) fail("telemetry GPU UUID changed");
  const allowed = new Set((metadata.approved_gpu_process_names ?? [])
    .map((value) => String(value).toLowerCase()));
  for (const value of config.environment_policy.experiment_process_names) {
    allowed.add(String(value).toLowerCase());
  }
  const unauthorized = [...new Set(processes.filter((row) => !row.no_processes && row.process_name)
    .map((row) => path.win32.basename(row.process_name).toLowerCase())
    .filter((name) => !allowed.has(name)))];
  const thermalSlowdown = telemetry.some((row) =>
    !["Not Active", "[N/A]", "N/A"].includes(String(row.hardware_thermal_slowdown)));
  const confounders = [];
  if (!dryRun && unauthorized.length) confounders.push(`unauthorized GPU processes: ${unauthorized.join(", ")}`);
  if (thermalSlowdown) confounders.push("hardware thermal slowdown reported during measurement");
  const telemetryTimes = telemetry.map((row) => Date.parse(row.captured_at));
  if (telemetryTimes.some((value) => !Number.isFinite(value))) fail("telemetry timestamp is invalid");
  const allowedGap = Number(config.environment_policy.telemetry_interval_ms) *
    Number(config.environment_policy.maximum_telemetry_gap_intervals);
  for (let i = 1; i < telemetryTimes.length; ++i) {
    if (telemetryTimes[i] - telemetryTimes[i - 1] > allowedGap) {
      confounders.push(`telemetry gap exceeded ${allowedGap} ms`);
      break;
    }
  }
  if (manifest.sample_role === "final") {
    const gatePath = path.join(root, "correctness-gate.json");
    if (!fs.existsSync(gatePath)) fail("final analysis requires the copied correctness gate");
    const gate = readJson(gatePath);
    if (gate.schema_version !== "S7-CORRECTNESS-GATE-v1" ||
        gate.status !== "passed-clean-candidate" || gate.revision !== manifest.revision ||
        gate.working_tree_state !== "clean" || gate.experiment_id !== config.experiment_id ||
        gate.experiment_config_sha256 !== manifest.experiment_config_sha256) {
      fail("final correctness gate identity or acceptance mismatch");
    }
  }
  return { capture, metadata, host, approval, telemetry, processes, confounders, dryRun };
}

function classifyHypotheses(config, rows, environment) {
  if (!rows.some((row) => row.sample_role === "final")) {
    return { schema_version: "S7-HYPOTHESIS-OUTCOMES-v1", status: "not-applicable-non-final", rows: [] };
  }
  const find = (variant, boundary, n, d) => rows.find((row) => row.sample_role === "final" &&
    row.variant === variant && row.boundary === boundary && row.N === n && row.D === d);
  const cases = [...new Set(rows.filter((row) => row.sample_role === "final")
    .map((row) => `${row.N}|${row.D}`))].sort();
  if (cases.length !== 12) fail(`hypothesis classification found ${cases.length} cases, expected 12`);
  const outcomes = config.hypotheses.map((hypothesis) => {
    const caseOutcomes = cases.map((key) => {
      const [n, d] = key.split("|");
      const reference = find(hypothesis.reference_variant, hypothesis.reference_boundary, n, d);
      const candidate = find(hypothesis.candidate_variant, hypothesis.candidate_boundary, n, d);
      if (!reference || !candidate) fail(`${hypothesis.id} is missing N${n}/D${d} evidence`);
      const ratio = Number(reference.median_ns) / Number(candidate.median_ns);
      const ciLow = Number(reference.ci_low_ns) / Number(candidate.ci_high_ns);
      const ciHigh = Number(reference.ci_high_ns) / Number(candidate.ci_low_ns);
      const outcome = ciLow > 1 ? "supported" : ciHigh < 1 ? "rejected" : "inconclusive";
      return { N: Number(n), D: Number(d), ratio, ci_low: ciLow, ci_high: ciHigh, outcome };
    });
    let outcome = caseOutcomes.some((row) => row.outcome === "rejected") ? "rejected" :
      caseOutcomes.every((row) => row.outcome === "supported") ? "supported" : "inconclusive";
    if (environment.confounders.length) outcome = "inconclusive";
    return { ...hypothesis, outcome, environment_override: environment.confounders.length > 0,
      cases: caseOutcomes };
  });
  return { schema_version: "S7-HYPOTHESIS-OUTCOMES-v1", status: "classified",
    environment_confounders: environment.confounders, rows: outcomes };
}

function writeHypothesisArtifacts(root, outcomes) {
  fs.writeFileSync(path.join(root, "hypothesis-outcomes.json"), JSON.stringify(outcomes, null, 2) + "\n");
  const lines = ["schema_version,hypothesis,N,D,ratio,ci_low,ci_high,case_outcome,hypothesis_outcome"];
  for (const hypothesis of outcomes.rows) {
    for (const row of hypothesis.cases) {
      lines.push(["S7-HYPOTHESIS-OUTCOMES-v1", hypothesis.id, row.N, row.D, row.ratio,
        row.ci_low, row.ci_high, row.outcome, hypothesis.outcome].join(","));
    }
  }
  fs.writeFileSync(path.join(root, "hypothesis-outcomes.csv"), lines.join("\n") + "\n");
}

function expectFailure(name, action, results) {
  try { action(); results.push({ name, status: "failed-did-not-reject" }); }
  catch { results.push({ name, status: "passed-rejected" }); }
}

function runSelfTest(output) {
  fs.mkdirSync(output, { recursive: true });
  const base = {
    schema_version: "S7-EXPERIMENT-CONFIG-v1", experiment_id: "test",
    expected_revision_source: "owner-supplied runner argument", approved_design: "design",
    approved_numerical_policy: { sha256: "a".repeat(64) },
    workload: { tuning_seed: "1", final_seed: "2" },
    sampling: { warmups: 10, measured_repetitions: 100 },
    environment_policy: { telemetry_required: true, telemetry_interval_ms: 1000,
      maximum_telemetry_gap_intervals: 3, experiment_process_names: ["s7_benchmark.exe"] },
    hypotheses: ["H1", "H2", "H3", "H4"].map((id) => ({ id, statement: id,
      reference_variant: "v0", reference_boundary: "BND-KERNEL",
      candidate_variant: "v1", candidate_boundary: "BND-KERNEL",
      scope: "all-12-required-cases" })),
  };
  const results = [];
  expectFailure("missing-experiment-id", () => validateExperimentConfig({ ...base, experiment_id: "" }), results);
  expectFailure("identical-seeds", () => validateExperimentConfig({ ...base,
    workload: { tuning_seed: "1", final_seed: "1" } }), results);
  expectFailure("missing-hypothesis", () => validateExperimentConfig({ ...base,
    hypotheses: base.hypotheses.slice(0, 3) }), results);
  expectFailure("missing-telemetry-policy", () => validateExperimentConfig({ ...base,
    environment_policy: { telemetry_required: false, telemetry_interval_ms: 1000,
      maximum_telemetry_gap_intervals: 3, experiment_process_names: ["s7_benchmark.exe"] } }), results);
  const identityManifest = { experiment_id: "test", revision: "a".repeat(40),
    owner_expected_revision: "a".repeat(40), experiment_config_sha256: "b".repeat(64),
    tuning_seed: "1", final_seed: "2" };
  const identityMetadata = { experiment_id: "test", owner_expected_revision: "a".repeat(40),
    observed_head: "a".repeat(40), experiment_config_sha256: "b".repeat(64) };
  expectFailure("config-hash-mismatch", () => validateIdentity(identityManifest,
    identityMetadata, base, "c".repeat(64)), results);
  expectFailure("revision-mismatch", () => validateIdentity({ ...identityManifest,
    revision: "d".repeat(40) }, identityMetadata, base, "b".repeat(64)), results);
  const summaryRows = [];
  for (const n of [1024, 16384, 262144, 1048576]) {
    for (const d of [8, 32, 128]) {
      summaryRows.push({ sample_role: "final", variant: "v0", boundary: "BND-KERNEL",
        N: String(n), D: String(d), median_ns: "200", ci_low_ns: "180", ci_high_ns: "220" });
      summaryRows.push({ sample_role: "final", variant: "v1", boundary: "BND-KERNEL",
        N: String(n), D: String(d), median_ns: "100", ci_low_ns: "90", ci_high_ns: "110" });
    }
  }
  const classified = classifyHypotheses(base, summaryRows, { confounders: [] });
  if (!classified.rows.every((row) => row.outcome === "supported")) {
    results.push({ name: "supported-classification", status: "failed" });
  } else {
    results.push({ name: "supported-classification", status: "passed" });
  }
  const confounded = classifyHypotheses(base, summaryRows, { confounders: ["test"] });
  if (!confounded.rows.every((row) => row.outcome === "inconclusive")) {
    results.push({ name: "confounder-preserves-inconclusive", status: "failed" });
  } else {
    results.push({ name: "confounder-preserves-inconclusive", status: "passed" });
  }
  if (results.some((row) => !row.status.startsWith("passed"))) fail("negative self-test failed");
  fs.writeFileSync(path.join(output, "negative-tests.json"), JSON.stringify({
    schema_version: "S7-ACCEPTANCE-NEGATIVE-TESTS-v1", status: "passed", rows: results,
  }, null, 2) + "\n");
}

function main() {
  const root = process.argv[2];
  if (root === "--self-test") {
    if (!process.argv[3]) fail("--self-test requires an output directory");
    runSelfTest(process.argv[3]);
    return;
  }
  if (!root) fail("usage: node scripts/analyze-s7-benchmarks.mjs RESULT_DIR [--shared-root ROOT]");
  const sharedIndex = process.argv.indexOf("--shared-root");
  const sharedRoot = sharedIndex >= 0 ? process.argv[sharedIndex + 1] : path.dirname(root);
  const manifestPath = path.join(root, "manifest.json");
  const rawPath = path.join(root, "raw_samples.csv");
  const summaryPath = path.join(root, "summary.csv");
  for (const file of [manifestPath, rawPath, summaryPath, path.join(root, "tuning-freeze.json")]) {
    if (!fs.existsSync(file)) fail(`missing required artifact: ${file}`);
  }
  const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
  const configPath = path.join(root, "experiment-config.json");
  if (!fs.existsSync(configPath)) fail(`missing required artifact: ${configPath}`);
  const config = readJson(configPath);
  validateExperimentConfig(config);
  const environment = validateContext(root, sharedRoot, manifest, config);
  const raw = parseCsv(fs.readFileSync(rawPath, "utf8"));
  const summary = parseCsv(fs.readFileSync(summaryPath, "utf8"));
  const expected = Number(manifest.measured_repetitions);
  for (const row of summary) {
    if (Number(row.sample_count) !== expected) {
      fail(`summary count mismatch for ${row.sample_role}/${row.variant}/${row.boundary}/N${row.N}/D${row.D}`);
    }
  }
  if (manifest.tuning_seed === manifest.final_seed) fail("tuning and final seeds must differ");
  if (manifest.prepared_query_public_scenario !== "unsupported" ||
      manifest.device_resident_public_scenario !== "unsupported") {
    fail("unsupported public reuse scenarios must remain explicit");
  }
  if (manifest.sample_role === "final") {
    const cases = new Set(summary.filter((row) => row.sample_role === "final")
      .map((row) => `${row.N}|${row.D}`));
    if (cases.size !== 12) fail(`final output covers ${cases.size} cases instead of 12`);
    for (const key of cases) {
      const [n, d] = key.split("|");
      for (const variant of ["cpu", "v0", "v1", "v2", "v3"]) {
        const boundary = variant === "cpu" ? "CPU-E2E-RAW" : "BND-E2E-RAW";
        if (!summary.some((row) => row.sample_role === "final" && row.N === n &&
            row.D === d && row.variant === variant && row.boundary === boundary)) {
          fail(`missing final ${variant}/${boundary}/N${n}/D${d}`);
        }
      }
    }
  }
  if (manifest.sample_role === "tuning") {
    const freeze = JSON.parse(fs.readFileSync(path.join(root, "tuning-freeze.json"), "utf8"));
    if (freeze.mapping.length !== 24) fail("tuning freeze must contain 24 V2/V3 case mappings");
    for (const variant of ["v2", "v3"]) {
      for (const block of [128, 256, 512]) {
        const count = summary.filter((row) => row.sample_role === "tuning" &&
          row.variant === variant && row.boundary === "BND-KERNEL" &&
          Number(row.block_size) === block).length;
        if (count !== 12) fail(`tuning coverage for ${variant}/${block} is ${count}, expected 12`);
      }
    }
  }
  const plots = path.join(root, "plots");
  fs.mkdirSync(plots, { recursive: true });
  if (summary.some((row) => row.boundary === "BND-KERNEL")) {
    writeLatencySvg(path.join(plots, "kernel-latency.svg"), summary,
                    "S7 V0-V3 kernel-only latency", "BND-KERNEL");
  }
  if (summary.some((row) => row.boundary === "BND-E2E-RAW")) {
    writeLatencySvg(path.join(plots, "raw-end-to-end-latency.svg"), summary,
                    "S7 CPU and GPU raw end-to-end latency", "BND-E2E-RAW");
  }
  const comparisons = buildComparisons(summary,
    manifest.sample_role === "dry-run" ? "dry-run-final" : "final");
  writeComparisons(root, comparisons);
  writeRatioSvg(path.join(plots, "compatible-ratios.svg"), comparisons);
  writeStageSvg(path.join(plots, "stage-attribution.svg"), summary);

  const outcomes = classifyHypotheses(config, summary, environment);
  writeHypothesisArtifacts(root, outcomes);
  const finalRole = manifest.sample_role === "final";
  const acceptance = {
    schema_version: "S7-PERFORMANCE-ACCEPTANCE-v1",
    experiment_id: config.experiment_id,
    revision: manifest.revision,
    working_tree_state: manifest.working_tree_state,
    ac014: environment.dryRun ? "provisional-dry-run" : "passed",
    ac015: finalRole ? "passed" : "pending-final",
    performance_acceptance: environment.dryRun ? "blocked-provisional-dry-run" :
      finalRole ? (environment.confounders.length ? "accepted-inconclusive" : "accepted") : "pending-final",
    hypothesis_outcomes: outcomes.rows.map((row) => ({ id: row.id, outcome: row.outcome })),
    limitations: environment.confounders,
    samples_discarded_for_environment: 0,
  };
  fs.writeFileSync(path.join(root, "acceptance.json"), JSON.stringify(acceptance, null, 2) + "\n");

  const hashes = {};
  for (const file of ["manifest.json", "environment.json", "experiment-config.json",
    "raw_samples.csv", "summary.csv", "summary.json", "tuning-freeze.json",
    "tuning-freeze.csv", "comparisons.csv", "comparisons.json", "commands.txt",
    "hypothesis-outcomes.json", "hypothesis-outcomes.csv", "acceptance.json"]) {
    const bytes = fs.readFileSync(path.join(root, file));
    hashes[file] = crypto.createHash("sha256").update(bytes).digest("hex");
  }
  for (const file of ["experiment-config.json", "environment-approval.json",
    "host-environment.json", "run-metadata.json", "nvidia-smi-before.xml",
    "nvidia-smi-after.xml", "commands.jsonl", "CMakeCache.txt", "build.ninja",
    "telemetry-launch-errors.log",
    "telemetry/gpu-telemetry.jsonl",
    "telemetry/gpu-processes.jsonl", "telemetry/telemetry-errors.log"]) {
    hashes[`environment-capture/${file}`] = sha256(path.join(environment.capture, file));
  }
  for (const file of fs.readdirSync(plots).filter((value) => value.endsWith(".svg"))) {
    hashes[`plots/${file}`] = sha256(path.join(plots, file));
  }
  if (fs.existsSync(path.join(root, "correctness-gate.json"))) {
    hashes["correctness-gate.json"] = sha256(path.join(root, "correctness-gate.json"));
  }
  fs.writeFileSync(path.join(root, "integrity.json"), JSON.stringify({
    schema_version: "S7-BENCHMARK-INTEGRITY-v1",
    status: "passed",
    raw_rows: raw.length,
    summary_rows: summary.length,
    sha256: hashes,
  }, null, 2) + "\n");
}

try {
  main();
} catch (error) {
  console.error(`S7 analysis: FAIL: ${error.message}`);
  process.exitCode = 1;
}
