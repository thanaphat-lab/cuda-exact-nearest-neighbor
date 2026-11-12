#!/usr/bin/env node
import fs from "node:fs";
import path from "node:path";
import {createHash} from "node:crypto";

function fail(message) { throw new Error(message); }

function sha256(file) {
  return createHash("sha256").update(fs.readFileSync(file)).digest("hex");
}

function decodeUtf8Json(bytes) {
  const text = Buffer.isBuffer(bytes) ? bytes.toString("utf8") : String(bytes);
  return text.charCodeAt(0) === 0xfeff ? text.slice(1) : text;
}

function parseUtf8Json(bytes) {
  return JSON.parse(decodeUtf8Json(bytes));
}

function readJson(file) {
  return parseUtf8Json(fs.readFileSync(file));
}

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

function readJsonLines(file) {
  if (!fs.existsSync(file)) return [];
  return decodeUtf8Json(fs.readFileSync(file)).split(/\r?\n/).filter(line => line.trim())
    .map((line, index) => {
      try { return JSON.parse(line); }
      catch { fail(`invalid JSONL row ${index + 1} in ${file}`); }
    });
}

function parseTime(value, label) {
  const parsed = Date.parse(value);
  if (!Number.isFinite(parsed)) fail(`invalid ${label} timestamp`);
  return parsed;
}

function normalizeUuid(value) {
  return String(value).toLowerCase().replace(/^gpu-/, "").replaceAll("-", "");
}

function finiteNonnegative(value, label) {
  const number = Number(value);
  if (!Number.isFinite(number) || number < 0) fail(`invalid ${label}`);
  return number;
}

function evaluateEnvironment(data) {
  const {config, approval, environment, interval, attempts, metrics, processes,
    captureErrors, launchErrors, ready, session} = data;
  const policy = config.environment_policy;
  if (config.schema_version !== "S14-BATCH-EXPERIMENT-CONFIG-v2" ||
      policy?.telemetry_required !== true || policy.telemetry_interval_ms !== 1000 ||
      policy.maximum_telemetry_gap_intervals !== 3 ||
      policy.telemetry_query_timeout_ms !== 900 ||
      policy.telemetry_incomplete_action !==
        "retain-samples-and-mark-hypotheses-inconclusive") {
    fail("S14 telemetry policy mismatch");
  }
  const maximumGap = policy.telemetry_interval_ms *
    policy.maximum_telemetry_gap_intervals;
  if (interval.schema_version !== "S14-BENCHMARK-INTERVAL-v1" ||
      interval.telemetry_interval_ms !== policy.telemetry_interval_ms ||
      interval.maximum_telemetry_gap_ms !== maximumGap ||
      interval.telemetry_query_timeout_ms !== policy.telemetry_query_timeout_ms ||
      !Number.isSafeInteger(interval.benchmark_pid) || interval.benchmark_pid <= 0 ||
      !Number.isSafeInteger(interval.telemetry_pid) || interval.telemetry_pid <= 0 ||
      interval.benchmark_pid === interval.telemetry_pid ||
      interval.benchmark_exit_code !== 0) {
    fail("invalid benchmark/telemetry lifecycle identity");
  }
  if (environment.experiment_id !== approval.experiment_id ||
      normalizeUuid(environment.gpu_uuid) !== normalizeUuid(approval.expected_gpu_uuid)) {
    fail("environment approval or GPU identity mismatch");
  }
  if (environment.approved_design_id !== config.approved_design?.id ||
      environment.approved_design_sha256 !== config.approved_design?.sha256 ||
      environment.design_approval_id !== config.approved_design?.approval_id) {
    fail("approved design identity mismatch");
  }
  const start = parseTime(interval.benchmark_started_at, "benchmark start");
  const end = parseTime(interval.benchmark_ended_at, "benchmark end");
  if (end < start) fail("benchmark interval is reversed");
  const reasons = [];
  const addReason = (code, detail = {}) => {
    if (!reasons.some(reason => reason.code === code)) reasons.push({code, ...detail});
  };
  if (interval.telemetry_exit_code !== 0) {
    addReason("telemetry-nonzero-exit", {exit_code: interval.telemetry_exit_code});
  }
  if (interval.telemetry_exited_before_benchmark_end === true) {
    addReason("telemetry-exited-before-benchmark-end");
  }
  if (interval.telemetry_exited_before_stop_signal === true) {
    addReason("telemetry-exited-before-stop-signal");
  }
  if (interval.telemetry_forced_termination === true) {
    addReason("telemetry-forced-termination");
  }
  if (!ready) {
    addReason("telemetry-ready-record-missing");
  } else if (ready.schema_version !== "S14-TELEMETRY-READY-v1" ||
      !Number.isSafeInteger(ready.sequence) || ready.sequence < 0) {
    fail("malformed telemetry ready record");
  } else {
    parseTime(ready.ready_at, "telemetry ready");
  }
  if (!session) {
    addReason("telemetry-session-record-missing");
  } else if (session.schema_version !== "S14-TELEMETRY-SESSION-v1" ||
      session.process_id !== interval.telemetry_pid ||
      session.interval_ms !== policy.telemetry_interval_ms ||
      session.query_timeout_ms !== policy.telemetry_query_timeout_ms ||
      !Number.isSafeInteger(session.attempted_samples) ||
      !Number.isSafeInteger(session.completed_samples) ||
      !Number.isSafeInteger(session.failed_samples) ||
      session.attempted_samples !== attempts.length ||
      session.completed_samples + session.failed_samples !== session.attempted_samples) {
    fail("malformed or contradictory telemetry session record");
  } else {
    parseTime(session.started_at, "telemetry session start");
    parseTime(session.ended_at, "telemetry session end");
  }
  if (captureErrors.trim()) addReason("telemetry-capture-errors");
  if (launchErrors.trim()) addReason("telemetry-launch-errors");
  if (!attempts.length) addReason("telemetry-attempts-missing");
  for (let index = 0; index < attempts.length; ++index) {
    const row = attempts[index];
    if (row.schema_version !== "S14-TELEMETRY-ATTEMPT-v1" ||
        !Number.isSafeInteger(row.sequence) || row.sequence !== index ||
        !["completed", "failed"].includes(row.status) ||
        !["completed", "failed", "timeout"].includes(row.metric_status) ||
        !["completed", "failed", "timeout"].includes(row.process_status)) {
      fail("malformed or noncontiguous telemetry attempt sequence");
    }
    const attemptStart = parseTime(row.attempt_started_at, "telemetry attempt start");
    const attemptFinish = parseTime(row.attempt_finished_at, "telemetry attempt finish");
    if (attemptFinish < attemptStart ||
        finiteNonnegative(row.scheduled_elapsed_ms, "scheduled telemetry time") !==
          row.sequence * policy.telemetry_interval_ms ||
        finiteNonnegative(row.attempt_started_elapsed_ms,
          "telemetry attempt monotonic start") < 0 ||
        finiteNonnegative(row.attempt_finished_elapsed_ms,
          "telemetry attempt monotonic finish") < row.attempt_started_elapsed_ms ||
        finiteNonnegative(row.metric_duration_ms, "metric query duration") < 0 ||
        finiteNonnegative(row.process_duration_ms, "process query duration") < 0) {
      fail("malformed telemetry attempt timing");
    }
    if (row.status !== "completed" || row.metric_status !== "completed" ||
        row.process_status !== "completed") {
      addReason("telemetry-attempt-failed", {first_sequence: row.sequence});
    }
    const correctedFields = ["metric_normal_completion_duration_ms",
      "metric_cleanup_duration_ms", "metric_cleanup_status",
      "process_normal_completion_duration_ms", "process_cleanup_duration_ms",
      "process_cleanup_status"];
    const correctedCount = correctedFields.filter(field => field in row).length;
    if (correctedCount !== 0 && correctedCount !== correctedFields.length) {
      fail("partial corrected telemetry deadline accounting");
    }
    if (correctedCount === correctedFields.length) {
      for (const prefix of ["metric", "process"]) {
        const normal = row[`${prefix}_normal_completion_duration_ms`];
        const cleanup = finiteNonnegative(row[`${prefix}_cleanup_duration_ms`],
          `${prefix} cleanup duration`);
        const cleanupStatus = row[`${prefix}_cleanup_status`];
        if (row[`${prefix}_status`] === "completed") {
          if (!Number.isFinite(Number(normal)) || Number(normal) < 0 ||
              Number(normal) > policy.telemetry_query_timeout_ms || cleanup !== 0 ||
              cleanupStatus !== "not-required") {
            fail("completed telemetry query violates corrected deadline accounting");
          }
        } else if (normal !== null ||
            !["completed", "failed"].includes(cleanupStatus)) {
          fail("timed-out telemetry query violates corrected cleanup accounting");
        }
      }
    }
  }
  if (session && (session.completed_samples !==
        attempts.filter(row => row.status === "completed").length ||
      session.failed_samples !== attempts.filter(row => row.status !== "completed").length)) {
    fail("telemetry session counts contradict attempt records");
  }
  const attemptBySequence = new Map(attempts.map(row => [row.sequence, row]));
  const metricTimes = [];
  const metricSequences = new Set();
  let thermalSlowdownSamples = 0;
  let unsupportedThermalSamples = 0;
  for (const row of metrics) {
    if (row.schema_version !== "S14-GPU-TELEMETRY-v1" ||
        !Number.isSafeInteger(row.sequence) || metricSequences.has(row.sequence) ||
        !attemptBySequence.has(row.sequence) ||
        normalizeUuid(row.uuid) !== normalizeUuid(environment.gpu_uuid)) {
      fail("malformed metric sequence or GPU identity mismatch");
    }
    for (const field of ["captured_at", "query_duration_ms", "pstate",
      "temperature_gpu_c", "utilization_gpu_percent", "utilization_memory_percent",
      "clock_sm_mhz", "clock_memory_mhz", "power_draw_w", "power_limit_w",
      "pcie_generation", "pcie_width", "hardware_thermal_slowdown"]) {
      if (!(field in row) || String(row[field]).trim() === "") {
        fail(`missing metric field ${field}`);
      }
    }
    finiteNonnegative(row.query_duration_ms, "metric row query duration");
    const numeric = {};
    for (const field of ["temperature_gpu_c", "utilization_gpu_percent",
      "utilization_memory_percent", "clock_sm_mhz", "clock_memory_mhz",
      "power_draw_w", "power_limit_w", "pcie_generation", "pcie_width"]) {
      const text = String(row[field]).trim().toLowerCase();
      numeric[field] = ["n/a", "[n/a]", "not supported", "[not supported]"]
        .includes(text) ? null : finiteNonnegative(row[field], field);
    }
    if ((numeric.utilization_gpu_percent ?? 0) > 100 ||
        (numeric.utilization_memory_percent ?? 0) > 100 ||
        numeric.clock_sm_mhz === 0 || numeric.clock_memory_mhz === 0 ||
        numeric.pcie_generation === 0 || numeric.pcie_width === 0 ||
        !/^P\d+$/i.test(String(row.pstate))) {
      fail("invalid metric range or P-state");
    }
    metricSequences.add(row.sequence);
    metricTimes.push(parseTime(row.captured_at, "metric capture"));
    const thermal = String(row.hardware_thermal_slowdown).trim().toLowerCase();
    if (["n/a", "[n/a]", "not supported", "[not supported]"].includes(thermal)) {
      ++unsupportedThermalSamples;
    } else if (!["not active", "no", "0"].includes(thermal)) {
      ++thermalSlowdownSamples;
    }
  }
  const processTimesBySequence = new Map();
  const processIdentities = new Set();
  const unauthorized = new Set();
  let benchmarkProcessObserved = false;
  const allowedProcesses = new Set([
    ...(approval.approved_gpu_process_names ?? []),
    ...(policy.experiment_process_names ?? [])
  ].map(value => path.win32.basename(String(value)).toLowerCase()));
  for (const row of processes) {
    if (row.schema_version !== "S14-GPU-PROCESSES-v1" ||
        !Number.isSafeInteger(row.sequence) || !attemptBySequence.has(row.sequence)) {
      fail("malformed process telemetry sequence");
    }
    finiteNonnegative(row.query_duration_ms, "process row query duration");
    const time = parseTime(row.captured_at, "process capture");
    processTimesBySequence.set(row.sequence, time);
    if (row.no_processes === true) {
      if (row.gpu_uuid !== "" || row.pid !== "" || row.process_name !== "") {
        fail("inconsistent empty process telemetry row");
      }
      continue;
    }
    if (row.no_processes !== false ||
        normalizeUuid(row.gpu_uuid) !== normalizeUuid(environment.gpu_uuid) ||
        !/^[1-9][0-9]*$/.test(String(row.pid)) ||
        !Number.isSafeInteger(Number(row.pid))) {
      fail("malformed process row or GPU identity mismatch");
    }
    const identity = `${row.sequence}|${row.pid}`;
    if (processIdentities.has(identity)) fail("duplicate process telemetry identity");
    processIdentities.add(identity);
    const executable = path.win32.basename(String(row.process_name)).toLowerCase();
    if (!allowedProcesses.has(executable)) unauthorized.add(executable);
    if (executable === "s14_batch_benchmark.exe" &&
        Number(row.pid) === interval.benchmark_pid) benchmarkProcessObserved = true;
  }
  for (const attempt of attempts) {
    if (attempt.status === "completed" &&
        (!metricSequences.has(attempt.sequence) ||
         !processTimesBySequence.has(attempt.sequence))) {
      addReason("telemetry-completed-slot-missing-observation",
        {first_sequence: attempt.sequence});
      break;
    }
  }
  if (!metrics.length) addReason("metric-telemetry-missing");
  if (!processes.length) addReason("process-telemetry-missing");
  if (!benchmarkProcessObserved) addReason("benchmark-process-not-observed");
  if (unauthorized.size) {
    addReason("unauthorized-gpu-process", {processes: [...unauthorized].sort()});
  }
  if (thermalSlowdownSamples) {
    addReason("hardware-thermal-slowdown", {samples: thermalSlowdownSamples});
  }
  if (unsupportedThermalSamples) {
    addReason("hardware-thermal-slowdown-unsupported",
      {samples: unsupportedThermalSamples});
  }
  const inspectContinuity = (times, label) => {
    let observedMaximum = 0;
    for (let index = 1; index < times.length; ++index) {
      if (times[index] <= times[index - 1]) fail(`${label} timestamps are not strictly increasing`);
      observedMaximum = Math.max(observedMaximum, times[index] - times[index - 1]);
    }
    if (observedMaximum > maximumGap) {
      addReason(`${label}-gap-exceeded`, {
        observed_gap_ms: observedMaximum, allowed_gap_ms: maximumGap});
    }
    if (!times.length || times[0] > start || times.at(-1) < end) {
      addReason(`${label}-coverage-incomplete`);
    }
    return observedMaximum;
  };
  const metricMaximumGapMs = inspectContinuity(metricTimes, "metric-telemetry");
  const processTimes = [...processTimesBySequence.entries()]
    .sort((a, b) => a[0] - b[0]).map(entry => entry[1]);
  const processMaximumGapMs = inspectContinuity(processTimes, "process-telemetry");
  return {
    schema_version: "S14-BATCH-ENVIRONMENT-STATUS-v2",
    status: reasons.length ? "retain-samples-mark-hypotheses-inconclusive" :
      "accepted-for-analysis",
    reasons,
    metric_samples: metrics.length,
    process_samples: processes.length,
    telemetry_attempts: attempts.length,
    metric_maximum_gap_ms: metricMaximumGapMs,
    process_maximum_gap_ms: processMaximumGapMs,
    allowed_gap_ms: maximumGap,
    unauthorized_gpu_processes: [...unauthorized].sort(),
    hardware_thermal_slowdown_samples: thermalSlowdownSamples,
    unsupported_hardware_thermal_slowdown_samples: unsupportedThermalSamples
  };
}

function validateEnvironment(directory, manifest) {
  const capture = path.join(path.dirname(directory), "environment-capture");
  const required = ["experiment-config.json", "environment-approval.json",
    "environment.json", "benchmark-interval.json", "approved-design.md",
    "design-approval-record.md"];
  for (const name of required) {
    if (!fs.existsSync(path.join(capture, name))) fail(`missing identity artifact ${name}`);
  }
  const telemetry = path.join(capture, "telemetry");
  const readText = name => fs.existsSync(path.join(telemetry, name))
    ? fs.readFileSync(path.join(telemetry, name), "utf8") : "";
  const config = readJson(path.join(capture, "experiment-config.json"));
  const approval = readJson(path.join(capture, "environment-approval.json"));
  const environment = readJson(path.join(capture, "environment.json"));
  if (environment.revision !== manifest.revision ||
      environment.working_tree_state !== "clean" ||
      environment.config_sha256 !== manifest.experiment_config_sha256 ||
      environment.experiment_id !== manifest.experiment_id ||
      approval.experiment_id !== manifest.experiment_id) {
    fail("environment revision, configuration, or experiment identity mismatch");
  }
  if (sha256(path.join(capture, "approved-design.md")) !== config.approved_design?.sha256) {
    fail("captured approved design hash mismatch");
  }
  const approvalRecord = fs.readFileSync(path.join(capture,
    "design-approval-record.md"), "utf8");
  if (!approvalRecord.includes(config.approved_design.approval_id) ||
      !approvalRecord.includes(config.approved_design.sha256)) {
    fail("captured design approval binding mismatch");
  }
  return evaluateEnvironment({
    config,
    approval,
    environment,
    interval: readJson(path.join(capture, "benchmark-interval.json")),
    attempts: readJsonLines(path.join(telemetry, "telemetry-attempts.jsonl")),
    metrics: readJsonLines(path.join(telemetry, "gpu-telemetry.jsonl")),
    processes: readJsonLines(path.join(telemetry, "gpu-processes.jsonl")),
    ready: fs.existsSync(path.join(telemetry, "telemetry-ready.json"))
      ? readJson(path.join(telemetry, "telemetry-ready.json")) : null,
    session: fs.existsSync(path.join(telemetry, "telemetry-session.json"))
      ? readJson(path.join(telemetry, "telemetry-session.json")) : null,
    captureErrors: readText("telemetry-errors.log"),
    launchErrors: fs.existsSync(path.join(capture, "telemetry-launch-errors.log"))
      ? fs.readFileSync(path.join(capture, "telemetry-launch-errors.log"), "utf8") : ""
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

function applyEnvironmentStatus(environmentStatus, comparisons,
                                hypothesisOutcomes, breakEvens) {
  if (environmentStatus.status === "accepted-for-analysis") return;
  for (const row of comparisons) row.outcome = "inconclusive";
  for (const row of hypothesisOutcomes) row.outcome = "inconclusive";
  for (const row of breakEvens) {
    if (row.outcome === "finite-break-even-supported") {
      row.outcome = "finite-point-estimate-environment-inconclusive";
    }
  }
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
  const manifest = readJson(path.join(directory, "manifest.json"));
  if (manifest.schema_version !== "S14-BATCH-BENCHMARK-MANIFEST-v1") {
    fail("manifest schema mismatch");
  }
  const capturedConfigPath = path.join(path.dirname(directory),
    "environment-capture", "experiment-config.json");
  if (!fs.existsSync(capturedConfigPath) ||
      sha256(capturedConfigPath) !== manifest.experiment_config_sha256) {
    fail("captured experiment configuration hash mismatch");
  }
  const environmentStatus = validateEnvironment(directory, manifest);
  fs.writeFileSync(path.join(directory, "environment-status.json"),
    JSON.stringify(environmentStatus, null, 2) + "\n");
  const gate = readJson(path.join(directory, "correctness-gate.json"));
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
  applyEnvironmentStatus(environmentStatus, comparisons, hypothesisOutcomes,
    breakEvens);

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
    environment_reasons: environmentStatus.reasons,
    comparisons, hypothesis_outcomes: hypothesisOutcomes
  }, null, 2) + "\n");
  fs.writeFileSync(path.join(directory, "break-even.json"), JSON.stringify({
    schema_version: "S14-BATCH-BREAK-EVEN-v1",
    minimum_batches_formula:
      "max(1,floor((candidate_setup-reference_setup)/(reference_batch-candidate_batch))+1)",
    persistent_resource_destruction: "excluded; results are not complete lifetime costs",
    environment_status: environmentStatus.status,
    environment_reasons: environmentStatus.reasons,
    break_even: breakEvens
  }, null, 2) + "\n");
}

function selfTest() {
  const bomJson = Buffer.concat([
    Buffer.from([0xef, 0xbb, 0xbf]), Buffer.from('{"bom":"accepted"}', "utf8")]);
  let malformedBomRejected = false;
  try { parseUtf8Json(Buffer.from("\ufeffnot-json", "utf8")); }
  catch { malformedBomRejected = true; }
  if (parseUtf8Json(bomJson).bom !== "accepted" || !malformedBomRejected) {
    fail("UTF-8 BOM JSON self-test failed");
  }
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

  const times = ["2026-01-01T00:00:00.000Z", "2026-01-01T00:00:01.000Z",
    "2026-01-01T00:00:02.000Z", "2026-01-01T00:00:03.000Z"];
  const environmentFixture = () => ({
    config: {schema_version: "S14-BATCH-EXPERIMENT-CONFIG-v2",
      approved_design: {id: "design", sha256: "hash", approval_id: "approval"},
      environment_policy: {telemetry_required: true,
      telemetry_interval_ms: 1000, maximum_telemetry_gap_intervals: 3,
      telemetry_query_timeout_ms: 900,
      telemetry_incomplete_action: "retain-samples-and-mark-hypotheses-inconclusive",
      experiment_process_names: ["s14_batch_benchmark.exe"]}},
    approval: {experiment_id: "e", expected_gpu_uuid: "GPU-1",
      approved_gpu_process_names: []},
    environment: {experiment_id: "e", gpu_uuid: "GPU-1",
      approved_design_id: "design", approved_design_sha256: "hash",
      design_approval_id: "approval"},
    interval: {schema_version: "S14-BENCHMARK-INTERVAL-v1",
      benchmark_started_at: "2026-01-01T00:00:00.500Z",
      benchmark_ended_at: "2026-01-01T00:00:02.500Z", benchmark_pid: 456,
      benchmark_exit_code: 0, telemetry_pid: 123, telemetry_exit_code: 0,
      telemetry_exited_before_benchmark_end: false,
      telemetry_exited_before_stop_signal: false, telemetry_forced_termination: false,
      telemetry_interval_ms: 1000, maximum_telemetry_gap_ms: 3000,
      telemetry_query_timeout_ms: 900},
    attempts: times.map((time, sequence) => ({
      schema_version: "S14-TELEMETRY-ATTEMPT-v1", sequence,
      attempt_started_at: time, attempt_finished_at: time,
      scheduled_elapsed_ms: sequence * 1000,
      attempt_started_elapsed_ms: sequence * 1000,
      attempt_finished_elapsed_ms: sequence * 1000 + 100,
      metric_duration_ms: 40, process_duration_ms: 40,
      metric_status: "completed", process_status: "completed", status: "completed"})),
    metrics: times.map((time, sequence) => ({
      schema_version: "S14-GPU-TELEMETRY-v1", sequence, captured_at: time,
      query_duration_ms: 40, uuid: "GPU-1", pstate: "P0",
      temperature_gpu_c: "50", utilization_gpu_percent: "50",
      utilization_memory_percent: "10", clock_sm_mhz: "1000",
      clock_memory_mhz: "5000", power_draw_w: "50", power_limit_w: "100",
      pcie_generation: "4", pcie_width: "4",
      hardware_thermal_slowdown: "Not Active"})),
    processes: times.map((time, sequence) => sequence === 1 || sequence === 2
      ? {schema_version: "S14-GPU-PROCESSES-v1", sequence, captured_at: time,
        query_duration_ms: 40,
        gpu_uuid: "GPU-1", pid: "456", process_name: "s14_batch_benchmark.exe",
        no_processes: false}
      : {schema_version: "S14-GPU-PROCESSES-v1", sequence, captured_at: time,
        query_duration_ms: 40,
        gpu_uuid: "", pid: "", process_name: "", no_processes: true}),
    ready: {schema_version: "S14-TELEMETRY-READY-v1", sequence: 0,
      ready_at: times[0]},
    session: {schema_version: "S14-TELEMETRY-SESSION-v1", process_id: 123,
      started_at: times[0], ended_at: times[3], interval_ms: 1000,
      query_timeout_ms: 900, attempted_samples: 4, completed_samples: 4,
      failed_samples: 0},
    captureErrors: "", launchErrors: ""
  });
  if (evaluateEnvironment(environmentFixture()).status !== "accepted-for-analysis") {
    fail("valid telemetry environment self-test failed");
  }
  const expectInconclusive = mutation => {
    const fixture = environmentFixture(); mutation(fixture);
    return evaluateEnvironment(fixture).status ===
      "retain-samples-mark-hypotheses-inconclusive";
  };
  const expectBlocked = mutation => {
    const fixture = environmentFixture(); mutation(fixture);
    try { evaluateEnvironment(fixture); } catch { return true; }
    return false;
  };
  if (!expectInconclusive(value => {
        value.metrics[2].captured_at = "2026-01-01T00:02:22.000Z";
        value.metrics[3].captured_at = "2026-01-01T00:02:23.000Z";
      }) ||
      !expectInconclusive(value => value.metrics.pop()) ||
      !expectInconclusive(value => { value.processes.splice(1, 1); }) ||
      !expectInconclusive(value => { value.ready = null; }) ||
      !expectInconclusive(value => { value.session = null; }) ||
      !expectInconclusive(value => { value.interval.telemetry_exited_before_benchmark_end = true; }) ||
      !expectInconclusive(value => { value.attempts[1].status = "failed";
        value.session.completed_samples = 3; value.session.failed_samples = 1; }) ||
      !expectInconclusive(value => { value.processes[1].process_name = "foreign.exe"; }) ||
      !expectInconclusive(value => { value.metrics[1].hardware_thermal_slowdown = "Active"; }) ||
      !expectInconclusive(value => { value.metrics[1].hardware_thermal_slowdown = "N/A"; }) ||
      !expectBlocked(value => { value.attempts[2].sequence = 7; }) ||
      !expectBlocked(value => { value.metrics[1].uuid = "GPU-2"; })) {
    fail("telemetry integrity negative self-test failed");
  }
  const comparisonsForDowngrade = [{outcome: "supported"}, {outcome: "rejected"}];
  const hypothesesForDowngrade = [{outcome: "supported"}];
  const breakEvenForDowngrade = [{outcome: "finite-break-even-supported"},
    {outcome: "no-sustained-finite-break-even"}];
  applyEnvironmentStatus({status: "retain-samples-mark-hypotheses-inconclusive"},
    comparisonsForDowngrade, hypothesesForDowngrade, breakEvenForDowngrade);
  if (comparisonsForDowngrade.some(row => row.outcome !== "inconclusive") ||
      hypothesesForDowngrade[0].outcome !== "inconclusive" ||
      breakEvenForDowngrade[0].outcome !==
        "finite-point-estimate-environment-inconclusive") {
    fail("environment downgrade self-test failed");
  }
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
