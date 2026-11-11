#!/usr/bin/env node
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import {createHash} from "node:crypto";

function fail(message) { throw new Error(message); }
function sha256(file) {
  return createHash("sha256").update(fs.readFileSync(file)).digest("hex");
}
function readJson(file) { return JSON.parse(fs.readFileSync(file, "utf8")); }
function readJsonLines(file) {
  return fs.readFileSync(file, "utf8").split(/\r?\n/).filter(Boolean)
    .map((line, index) => {
      try { return JSON.parse(line); }
      catch { fail(`invalid JSONL row ${index + 1} in ${file}`); }
    });
}
function parseCsv(file) {
  const text = fs.readFileSync(file, "utf8").trim();
  if (!text) fail(`empty CSV: ${file}`);
  const lines = text.split(/\r?\n/), header = lines.shift().split(",");
  return lines.filter(Boolean).map((line, index) => {
    const values = line.split(",");
    if (values.length !== header.length) fail(`CSV field mismatch ${file}:${index + 2}`);
    return Object.fromEntries(header.map((key, column) => [key, values[column]]));
  });
}
function percentile(values, p) {
  if (!values.length) fail("empty distribution");
  const sorted = [...values].sort((a, b) => a - b);
  const position = p * (sorted.length - 1), lower = Math.floor(position);
  const upper = Math.ceil(position);
  return sorted[lower] + (sorted[upper] - sorted[lower]) * (position - lower);
}
function identityHash(text) {
  let value = 2166136261 >>> 0;
  for (const byte of Buffer.from(text)) {
    value ^= byte; value = Math.imul(value, 16777619) >>> 0;
  }
  return value || 1;
}
function bootstrapMedian(values, identity) {
  let state = identityHash(identity);
  const medians = [];
  for (let replicate = 0; replicate < 2000; ++replicate) {
    const sample = [];
    for (let index = 0; index < values.length; ++index) {
      state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
      sample.push(values[Math.floor((state / 0x100000000) * values.length)]);
    }
    medians.push(percentile(sample, 0.5));
  }
  return [percentile(medians, 0.025), percentile(medians, 0.975)];
}

const candidateCounts = [1024, 16384, 262144, 1000000];
const queryOrdinals = [0,322,645,967,1290,1612,1935,2257,2580,2902,3225,
  3548,3870,4193,4515,4838,5160,5483,5805,6128,6450,6773,7096,7418,
  7741,8063,8386,8708,9031,9353,9676,9999];
const blockMapping = new Map([[1024,256],[16384,128],[262144,512],[1000000,256]]);
const lanes = ["CPU-E2E-RAW","BND-KERNEL","BND-E2E-RAW","BND-E2E-PREP"];
const construction = ["COST-PREP-DATASET-CONSTRUCTION",
  "COST-V2-WORKSPACE-CONSTRUCTION","COST-PREPARED-READY-CONSTRUCTION"];
const scopes = new Map([
  ["CPU-E2E-RAW","not-applicable"],
  ["BND-KERNEL","device-resident-kernel-sequence"],
  ["BND-E2E-RAW","call-owned-transient-cleanup-included"],
  ["BND-E2E-PREP","persistent-resources-reused-destruction-excluded"],
  ["COST-PREP-DATASET-CONSTRUCTION","persistent-prepared-dataset-destruction-excluded"],
  ["COST-V2-WORKSPACE-CONSTRUCTION","persistent-workspace-destruction-excluded"],
  ["COST-PREPARED-READY-CONSTRUCTION","persistent-resource-destruction-excluded-not-lifetime-cost"]
]);

function validateConfig(config) {
  if (config.schema_version !== "S15-PUBLIC-SIFT1M-CONFIG-v1" ||
      JSON.stringify(config.workload.candidate_counts) !== JSON.stringify(candidateCounts) ||
      config.workload.dimension !== 128 ||
      JSON.stringify(config.workload.query_ordinals) !== JSON.stringify(queryOrdinals) ||
      config.workload.base_file.bytes !== 516000000 ||
      config.workload.base_file.sha256 !== "21f66e2975057b5728ba56de1c825bac4f4d89d596609ae985741c6242631816" ||
      config.workload.query_file.bytes !== 5160000 ||
      config.workload.query_file.sha256 !== "f7fc9be140accdfd64116c2fa2365ecdb69b8f084970c6b0532db5ff79ac8fdc" ||
      config.sampling.warmups !== 10 || config.sampling.measured_repetitions !== 100 ||
      config.sampling.bootstrap_resamples !== 2000 ||
      config.environment_policy.telemetry_interval_ms !== 1000 ||
      config.environment_policy.maximum_accepted_start_gap_ms !== 3000 ||
      config.environment_policy.telemetry_query_timeout_ms !== 900 ||
      config.fast_math !== "disabled") fail("approved S15 configuration mismatch");
  for (const N of candidateCounts) {
    if (config.v2_block_mapping[String(N)] !== blockMapping.get(N)) {
      fail(`approved V2 block mapping mismatch for N=${N}`);
    }
  }
  for (let index = 0; index < queryOrdinals.length; ++index) {
    if (queryOrdinals[index] !== Math.floor(index * 9999 / 31)) {
      fail("query formula and expanded list mismatch");
    }
  }
}

function validateCorrectness(directory, manifest, config) {
  const gateFile = path.join(directory, "correctness-gate.json");
  const ledgerFile = path.join(directory, "correctness-cases.jsonl");
  const gate = readJson(gateFile), ledger = readJsonLines(ledgerFile);
  if (gate.schema_version !== "S15-CORRECTNESS-GATE-v1" ||
      gate.status !== "passed-clean-candidate" || gate.performance_authorized !== true ||
      gate.revision !== manifest.revision || gate.working_tree_state !== "clean" ||
      gate.source_manifest_sha256 !== manifest.source_manifest_sha256 ||
      gate.experiment_config_sha256 !== manifest.experiment_config_sha256 ||
      gate.sift_base_sha256 !== manifest.sift_base_sha256 ||
      gate.sift_query_sha256 !== manifest.sift_query_sha256 ||
      gate.cuda_architecture !== manifest.cuda_architecture ||
      gate.build_flags !== manifest.build_flags ||
      gate.benchmark_binary_sha256 !== manifest.benchmark_binary_sha256 ||
      gate.artifact_manifest_sha256 !== manifest.artifact_manifest_sha256 ||
      gate.numerical_build_identity_sha256 !== manifest.numerical_build_identity_sha256 ||
      gate.required_workloads !== 128 || gate.required_production_results !== 384 ||
      gate.accepted_production_results !== 384 || gate.failed_production_results !== 0 ||
      gate.blocked_production_results !== 0 ||
      gate.query_selection !== "S15-Q32-floor-k-times-9999-over-31-v1" ||
      gate.v2_block_mapping !== "1024:256;16384:128;262144:512;1000000:256" ||
      gate.correctness_ledger_sha256 !== sha256(ledgerFile) ||
      gate.correctness_ledger_sha256 !== manifest.correctness_ledger_sha256) {
    fail("matching S15 correctness gate identity/status mismatch");
  }
  if (ledger.length !== 384) fail("S15 correctness ledger length mismatch");
  const methods = ["cpu-f32","v2-raw","v2-prepared"];
  let ordinal = 0;
  for (const N of candidateCounts) for (const query of queryOrdinals) {
    let expectedBits = null, expectedIndex = null;
    for (const method of methods) {
      const row = ledger[ordinal];
      if (row.schema_version !== "S15-CORRECTNESS-CASE-v1" ||
          row.ordinal !== ordinal || row.revision !== manifest.revision ||
          row.working_tree_state !== "clean" || row.N !== N || row.D !== 128 ||
          row.source_manifest_sha256 !== manifest.source_manifest_sha256 ||
          row.query_ordinal !== query || row.method !== method ||
          row.experiment_config_sha256 !== manifest.experiment_config_sha256 ||
          row.sift_base_sha256 !== config.workload.base_file.sha256 ||
          row.sift_query_sha256 !== config.workload.query_file.sha256 ||
          row.cuda_architecture !== manifest.cuda_architecture ||
          row.build_flags !== manifest.build_flags ||
          row.benchmark_binary_sha256 !== manifest.benchmark_binary_sha256 ||
          row.artifact_manifest_sha256 !== manifest.artifact_manifest_sha256 ||
          row.numerical_build_identity_sha256 !== manifest.numerical_build_identity_sha256 ||
          row.block_size !== (method === "cpu-f32" ? 0 : blockMapping.get(N)) ||
          !["accepted-unambiguous","accepted-ambiguous"].includes(row.nn_verdict) ||
          row.case_status !== "passed") fail(`invalid correctness row ${ordinal}`);
      for (const field of ["f32_lexicographic_status","production_path_status",
        "graph_status","arithmetic_environment_status","candidate_coverage_status",
        "production_candidate_coverage_status","production_match_status",
        "reference_match_status","all_candidate_distance_bound_status","ranking_status"]) {
        if (row[field] !== "passed") fail(`non-passing ${field} in correctness row ${ordinal}`);
      }
      if (expectedBits === null) {
        expectedBits = row.implementation_distance_bits;
        expectedIndex = row.implementation_index;
      } else if (row.implementation_distance_bits !== expectedBits ||
                 row.implementation_index !== expectedIndex) {
        fail(`production bitwise disagreement for N=${N}, query=${query}`);
      }
      ++ordinal;
    }
  }
  return gate;
}

function groupKey(row) { return `${row.boundary}|${row.N}|${row.query_ordinal}`; }
function validateRowsAndOrder(rows, manifest) {
  if (rows.length !== 52400) fail("raw sample row count mismatch");
  for (let index = 0; index < rows.length; ++index) {
    const row = rows[index], N = Number(row.N), block = Number(row.block_size);
    const elapsed = Number(row.elapsed_ns), iteration = Number(row.iteration);
    if (row.schema_version !== "S15-BENCHMARK-RAW-v1" ||
        row.revision !== manifest.revision || row.tree_state !== "clean" ||
        row.sample_role !== "final" || row.workload !== "single-query" ||
        row.D !== "128" || row.status !== "completed" || row.warmup !== "false" ||
        Number(row.run_order) !== index || !Number.isSafeInteger(elapsed) || elapsed < 0 ||
        !Number.isSafeInteger(iteration) || iteration < 0 || iteration >= 100 ||
        !candidateCounts.includes(N) || !scopes.has(row.boundary) ||
        row.resource_scope !== scopes.get(row.boundary) ||
        row.timer !== (row.boundary === "BND-KERNEL" ? "cuda_event" : "steady_clock") ||
        block !== (row.boundary === "CPU-E2E-RAW" ? 0 : blockMapping.get(N))) {
      fail(`invalid raw sample row ${index}`);
    }
  }
  let cursor = 0, workload = 0;
  for (const N of candidateCounts) {
    for (let iteration = 0; iteration < 100; ++iteration) {
      for (const boundary of construction) {
        const row = rows[cursor++];
        if (row.boundary !== boundary || row.N !== String(N) ||
            row.query_ordinal !== "dataset" || Number(row.iteration) !== iteration) {
          fail("construction sample order mismatch");
        }
      }
    }
    for (const query of queryOrdinals) {
      for (let iteration = 0; iteration < 100; ++iteration) {
        const rotation = (workload + iteration + 10) % 4;
        for (let laneIndex = 0; laneIndex < 4; ++laneIndex) {
          const row = rows[cursor++], boundary = lanes[(rotation + laneIndex) % 4];
          if (row.boundary !== boundary || row.N !== String(N) ||
              row.query_ordinal !== String(query) || Number(row.iteration) !== iteration) {
            fail("deterministic four-lane sample order mismatch");
          }
        }
      }
      ++workload;
    }
  }
  if (cursor !== rows.length || workload !== 128) fail("sample order coverage mismatch");
}

function summarize(rows) {
  const groups = new Map();
  for (const row of rows) {
    const values = groups.get(groupKey(row)) ?? [];
    values.push(Number(row.elapsed_ns)); groups.set(groupKey(row), values);
  }
  const summaries = [];
  for (const [identity, values] of groups) {
    if (values.length !== 100) fail(`distribution count mismatch: ${identity}`);
    const [boundary, N, query] = identity.split("|");
    const [ciLow, ciHigh] = bootstrapMedian(values, identity);
    summaries.push({boundary, N:Number(N), query_ordinal:query === "dataset" ? null : Number(query),
      block_size: boundary === "CPU-E2E-RAW" ? 0 : blockMapping.get(Number(N)),
      sample_count:100, percentile_method:"linear_n_minus_1",
      median_ns:percentile(values,0.5), p5_ns:percentile(values,0.05),
      p95_ns:percentile(values,0.95), ci_low_ns:ciLow, ci_high_ns:ciHigh});
  }
  if (summaries.length !== 524) fail("summary distribution count mismatch");
  return summaries.sort((a,b) => a.N-b.N ||
    String(a.query_ordinal).localeCompare(String(b.query_ordinal)) ||
    a.boundary.localeCompare(b.boundary));
}

function findSummary(rows, boundary, N, query = null) {
  const found = rows.find(row => row.boundary === boundary && row.N === N &&
    row.query_ordinal === query);
  if (!found) fail(`missing summary ${boundary}, N=${N}, query=${query}`);
  return found;
}
function comparison(reference, candidate, id, N, query) {
  const ciLow = reference.ci_low_ns / candidate.ci_high_ns;
  const ciHigh = reference.ci_high_ns / candidate.ci_low_ns;
  return {id,N,query_ordinal:query,reference_boundary:reference.boundary,
    candidate_boundary:candidate.boundary,
    speedup:reference.median_ns/candidate.median_ns,ci_low:ciLow,ci_high:ciHigh,
    outcome:ciLow>1?"supported":ciHigh<1?"rejected":"inconclusive"};
}
function universal(rows) {
  return rows.some(row=>row.outcome==="rejected") ? "rejected" :
    rows.every(row=>row.outcome==="supported") ? "supported" : "inconclusive";
}
function breakEven(reference, prepared, ready, referenceId, N, query) {
  const delta = reference.median_ns - prepared.median_ns;
  const conservativeDelta = reference.ci_low_ns - prepared.ci_high_ns;
  return {N,query_ordinal:query,reference:referenceId,
    reference_boundary:reference.boundary,candidate_boundary:prepared.boundary,
    construction_boundary:ready.boundary,construction_median_ns:ready.median_ns,
    minimum_integer_queries_for_speedup_gt_1:delta>0?Math.floor(ready.median_ns/delta)+1:null,
    conservative_minimum_queries:conservativeDelta>0?
      Math.floor(ready.ci_high_ns/conservativeDelta)+1:null,
    outcome:delta<=0?"no-finite-point-break-even":conservativeDelta<=0?
      "finite-point-estimate-uncertainty-inconclusive":"finite-break-even-supported",
    persistent_resource_destruction:"excluded; construction-plus-query amortization, not complete lifetime cost"};
}

function parseTime(value, label) {
  const parsed = Date.parse(value); if (!Number.isFinite(parsed)) fail(`invalid ${label}`);
  return parsed;
}
function evaluateEnvironment(directory, manifest, config) {
  const capture = path.join(path.dirname(directory), "environment-capture");
  const intervalFile = path.join(capture,"benchmark-interval.json");
  const telemetry = path.join(capture,"telemetry");
  const environmentFile=path.join(capture,"environment.json");
  for (const file of [environmentFile,intervalFile,path.join(telemetry,"telemetry-attempts.jsonl"),
    path.join(telemetry,"telemetry-ready.json"),path.join(telemetry,"telemetry-session.json"),
    path.join(telemetry,"gpu-telemetry.jsonl"),path.join(telemetry,"gpu-processes.jsonl")]) {
    if (!fs.existsSync(file)) return {status:"retain-samples-mark-hypotheses-inconclusive",
      reasons:[{code:"telemetry-artifact-missing",file:path.basename(file)}]};
  }
  const interval = readJson(intervalFile), attempts = readJsonLines(path.join(telemetry,"telemetry-attempts.jsonl"));
  const environment=readJson(environmentFile);
  const ready = readJson(path.join(telemetry,"telemetry-ready.json"));
  const session = readJson(path.join(telemetry,"telemetry-session.json"));
  const metrics = readJsonLines(path.join(telemetry,"gpu-telemetry.jsonl"));
  const processes = readJsonLines(path.join(telemetry,"gpu-processes.jsonl"));
  if (interval.schema_version !== "S15-BENCHMARK-INTERVAL-v1" ||
      interval.revision !== manifest.revision || interval.experiment_id !== manifest.experiment_id ||
      interval.telemetry_interval_ms !== 1000 || interval.maximum_telemetry_gap_ms !== 3000 ||
      interval.telemetry_query_timeout_ms !== 900 || interval.benchmark_exit_code !== 0 ||
      environment.revision!==manifest.revision || environment.experiment_id!==manifest.experiment_id ||
      environment.working_tree_state!=="clean" || environment.config_sha256!==manifest.experiment_config_sha256 ||
      ready.schema_version !== "S14-TELEMETRY-READY-v1" ||
      session.schema_version !== "S14-TELEMETRY-SESSION-v1" ||
      session.process_id!==interval.telemetry_pid || session.interval_ms!==1000 ||
      session.query_timeout_ms!==900 || session.attempted_samples !== attempts.length ||
      session.completed_samples+session.failed_samples!==session.attempted_samples) {
    fail("malformed telemetry lifecycle identity");
  }
  const start = parseTime(interval.benchmark_started_at,"benchmark start");
  const end = parseTime(interval.benchmark_ended_at,"benchmark end");
  if (end < start) fail("reversed benchmark interval");
  const reasons = [];
  const add = code => { if (!reasons.some(reason=>reason.code===code)) reasons.push({code}); };
  if (interval.telemetry_exit_code !== 0 || interval.telemetry_exited_before_benchmark_end ||
      interval.telemetry_exited_before_stop_signal || interval.telemetry_forced_termination) add("telemetry-lifecycle-incomplete");
  for (let index=0; index<attempts.length; ++index) {
    const row=attempts[index];
    if (row.schema_version!=="S14-TELEMETRY-ATTEMPT-v1" || row.sequence!==index ||
        !["completed","failed"].includes(row.status) ||
        !["completed","failed","timeout"].includes(row.metric_status) ||
        !["completed","failed","timeout"].includes(row.process_status) ||
        Number(row.scheduled_elapsed_ms)!==index*1000 ||
        !Number.isFinite(Number(row.metric_duration_ms)) || Number(row.metric_duration_ms)<0 ||
        !Number.isFinite(Number(row.process_duration_ms)) || Number(row.process_duration_ms)<0) {
      fail("malformed telemetry attempt ledger");
    }
    if (row.status!=="completed" || row.metric_status!=="completed" || row.process_status!=="completed") add("telemetry-query-failed-or-timeout");
    if(Number(row.metric_duration_ms)>900 || Number(row.process_duration_ms)>900)
      add("telemetry-query-duration-exceeded-timeout-policy");
  }
  const expectedUuid=String(environment.gpu_snapshot?.[0]??"").split(",")[1]?.trim()
    .toLowerCase().replace(/^gpu-/,"").replaceAll("-","");
  if(!expectedUuid) fail("environment GPU UUID is missing");
  const metricSequences=new Set();
  const times = metrics.map(row=>{
    for(const field of ["captured_at","query_duration_ms","uuid","pstate",
      "temperature_gpu_c","utilization_gpu_percent","utilization_memory_percent",
      "clock_sm_mhz","clock_memory_mhz","power_draw_w","power_limit_w",
      "pcie_generation","pcie_width","hardware_thermal_slowdown"]){
      if(!(field in row)||String(row[field]).trim()==="")
        fail(`missing metric telemetry field ${field}`);
    }
    const uuid=String(row.uuid??"").toLowerCase().replace(/^gpu-/,"").replaceAll("-","");
    if(row.schema_version!=="S14-GPU-TELEMETRY-v1" ||
        !Number.isSafeInteger(row.sequence) || metricSequences.has(row.sequence) ||
        !attempts[row.sequence] || uuid!==expectedUuid ||
        !Number.isFinite(Number(row.query_duration_ms)) || Number(row.query_duration_ms)<0)
      fail("malformed metric telemetry identity");
    metricSequences.add(row.sequence);
    return parseTime(row.captured_at,"metric capture");
  });
  let maximumGap=0;
  for(let index=1;index<times.length;++index){
    if(times[index]<=times[index-1]) fail("nonmonotonic telemetry timestamps");
    maximumGap=Math.max(maximumGap,times[index]-times[index-1]);
  }
  if(!times.length || times[0]>start || times.at(-1)<end) add("metric-telemetry-coverage-incomplete");
  if(maximumGap>3000) add("telemetry-gap-exceeded");
  if(metrics.some(row=>!["not active","no","0"].includes(String(row.hardware_thermal_slowdown).trim().toLowerCase()))) add("hardware-thermal-slowdown-or-unsupported");
  const allowed = new Set(["s15_public_sift1m_benchmark.exe","nvidia-smi.exe"]);
  let benchmarkObserved=false; const processTimes=new Map(), processIdentities=new Set();
  for(const row of processes){
    if(row.schema_version!=="S14-GPU-PROCESSES-v1" ||
        !Number.isSafeInteger(row.sequence) || !attempts[row.sequence] ||
        !Number.isFinite(Number(row.query_duration_ms)) || Number(row.query_duration_ms)<0)
      fail("malformed process telemetry");
    const captured=parseTime(row.captured_at,"process capture");
    processTimes.set(row.sequence,captured);
    if(row.no_processes===true){
      if(row.gpu_uuid!=="" || row.pid!=="" || row.process_name!=="")
        fail("inconsistent empty process telemetry row");
      continue;
    }
    const uuid=String(row.gpu_uuid??"").toLowerCase().replace(/^gpu-/,"").replaceAll("-","");
    const identity=`${row.sequence}|${row.pid}`;
    if(row.no_processes!==false || uuid!==expectedUuid ||
        !/^[1-9][0-9]*$/.test(String(row.pid)) || processIdentities.has(identity))
      fail("malformed process telemetry identity");
    processIdentities.add(identity);
    const executable=path.win32.basename(String(row.process_name)).toLowerCase();
    if(executable==="s15_public_sift1m_benchmark.exe" && Number(row.pid)===interval.benchmark_pid) benchmarkObserved=true;
    if(!allowed.has(executable)) add("unauthorized-gpu-process");
  }
  for(const attempt of attempts){
    if(attempt.status==="completed" &&
        (!metricSequences.has(attempt.sequence)||!processTimes.has(attempt.sequence))){
      add("completed-telemetry-slot-missing-observation"); break;
    }
  }
  if(!benchmarkObserved) add("benchmark-process-not-observed");
  const orderedProcessTimes=[...processTimes.entries()].sort((a,b)=>a[0]-b[0]).map(entry=>entry[1]);
  let processMaximumGap=0;
  for(let index=1;index<orderedProcessTimes.length;++index){
    if(orderedProcessTimes[index]<=orderedProcessTimes[index-1])
      fail("nonmonotonic process telemetry timestamps");
    processMaximumGap=Math.max(processMaximumGap,
      orderedProcessTimes[index]-orderedProcessTimes[index-1]);
  }
  if(!orderedProcessTimes.length || orderedProcessTimes[0]>start ||
      orderedProcessTimes.at(-1)<end) add("process-telemetry-coverage-incomplete");
  if(processMaximumGap>3000) add("process-telemetry-gap-exceeded");
  for(const name of ["telemetry-errors.log","telemetry-launch-errors.log"]){
    const file=name==="telemetry-errors.log"?path.join(telemetry,name):path.join(capture,name);
    if(fs.existsSync(file)&&fs.readFileSync(file,"utf8").trim()) add("telemetry-errors");
  }
  return {schema_version:"S15-ENVIRONMENT-STATUS-v1",
    status:reasons.length?"retain-samples-mark-hypotheses-inconclusive":"accepted-for-analysis",
    reasons,telemetry_attempts:attempts.length,metric_samples:metrics.length,
    process_samples:processes.length,metric_maximum_gap_ms:maximumGap,
    process_maximum_gap_ms:processMaximumGap,
    allowed_gap_ms:config.environment_policy.maximum_accepted_start_gap_ms};
}

function writeCsv(file, rows, fields) {
  fs.writeFileSync(file, fields.join(",")+"\n"+
    rows.map(row=>fields.map(field=>row[field]??"").join(",")).join("\n")+"\n");
}
function writePacketManifest(directory) {
  const packetRoot=path.dirname(directory), manifestPath=path.join(packetRoot,"packet-manifest.json");
  const entries=[];
  const visit=current=>{
    for(const entry of fs.readdirSync(current,{withFileTypes:true})){
      const full=path.join(current,entry.name);
      if(entry.isDirectory()) visit(full);
      else if(entry.isFile()&&full!==manifestPath) entries.push({
        path:path.relative(packetRoot,full).replaceAll("\\","/"),
        bytes:fs.statSync(full).size,sha256:sha256(full)});
    }
  };
  visit(packetRoot); entries.sort((a,b)=>a.path.localeCompare(b.path));
  fs.writeFileSync(manifestPath,JSON.stringify({
    schema_version:"S15-PACKET-MANIFEST-v1",excludes:["packet-manifest.json"],artifacts:entries
  },null,2)+"\n");
}

function analyze(directory) {
  if (fs.existsSync(path.join(directory,"preflight.json"))) fail("formal analyzer rejects preflight evidence");
  for(const file of ["manifest.json","experiment-config.json","correctness-gate.json",
    "correctness-cases.jsonl","raw_samples.csv"]){
    if(!fs.existsSync(path.join(directory,file))) fail(`missing mandatory S15 artifact ${file}`);
  }
  const manifest=readJson(path.join(directory,"manifest.json"));
  const config=readJson(path.join(directory,"experiment-config.json"));
  const capture=path.join(path.dirname(directory),"environment-capture");
  for(const file of ["dataset-provenance.json","authority-identities.json",
    "approved-design.md","design-approval-record.md","numerical-policy.md",
    "artifact-manifest.json","numerical-build-identity.json"]){
    if(!fs.existsSync(path.join(capture,file))) fail(`missing mandatory authority/provenance artifact ${file}`);
  }
  const provenance=readJson(path.join(capture,"dataset-provenance.json"));
  const authority=readJson(path.join(capture,"authority-identities.json"));
  const artifactManifest=readJson(path.join(capture,"artifact-manifest.json"));
  if(manifest.schema_version!=="S15-BENCHMARK-MANIFEST-v1" ||
      manifest.working_tree_state!=="clean" || manifest.warmups!==10 ||
      manifest.measured_repetitions!==100 ||
      manifest.experiment_config_sha256!==sha256(path.join(directory,"experiment-config.json")) ||
      manifest.sift_base_sha256!==config.workload.base_file.sha256 ||
      manifest.sift_query_sha256!==config.workload.query_file.sha256 ||
      !String(manifest.persistent_resource_destruction).includes("not complete lifetime")) {
    fail("manifest/config/dataset identity mismatch");
  }
  validateConfig(config);
  const benchmarkArtifact=artifactManifest.artifacts?.find(
    artifact=>artifact.name==="s15_public_sift1m_benchmark.exe");
  if(manifest.artifact_manifest_sha256!==sha256(path.join(capture,"artifact-manifest.json")) ||
      manifest.numerical_build_identity_sha256!==sha256(path.join(capture,"numerical-build-identity.json")) ||
      !benchmarkArtifact || manifest.benchmark_binary_sha256!==benchmarkArtifact.sha256) {
    fail("binary/artifact/numerical-build identity mismatch");
  }
  if(provenance.schema_version!=="S15-DATASET-PROVENANCE-v1" ||
      !Number.isFinite(Date.parse(provenance.acquired_at)) ||
      !String(provenance.acquisition_method).trim() ||
      !String(provenance.extraction_procedure).trim() ||
      provenance.base_file.bytes!==config.workload.base_file.bytes ||
      provenance.base_file.sha256!==config.workload.base_file.sha256 ||
      provenance.query_file.bytes!==config.workload.query_file.bytes ||
      provenance.query_file.sha256!==config.workload.query_file.sha256 ||
      provenance.dataset_bytes_redistributed!==false) fail("dataset provenance mismatch");
  const approvalText=fs.readFileSync(path.join(capture,"design-approval-record.md"),"utf8");
  if(authority.schema_version!=="S15-AUTHORITY-IDENTITIES-v1" ||
      authority.approved_candidate_design_sha256!==config.approved_design.approved_candidate_sha256 ||
      authority.synchronized_design_sha256!==config.approved_design.synchronized_sha256 ||
      authority.synchronized_design_sha256!==sha256(path.join(capture,"approved-design.md")) ||
      authority.approval_record_sha256!==sha256(path.join(capture,"design-approval-record.md")) ||
      authority.numerical_policy_sha256!==config.approved_numerical_policy.sha256 ||
      authority.numerical_policy_sha256!==sha256(path.join(capture,"numerical-policy.md")) ||
      authority.config_sha256!==manifest.experiment_config_sha256 ||
      !approvalText.includes(authority.approved_candidate_design_sha256)) {
    fail("authority/design/numerical-policy identity mismatch");
  }
  validateCorrectness(directory,manifest,config);
  const rows=parseCsv(path.join(directory,"raw_samples.csv"));
  validateRowsAndOrder(rows,manifest);
  const summaries=summarize(rows), comparisons=[], breakEvens=[];
  for(const N of candidateCounts){
    const ready=findSummary(summaries,"COST-PREPARED-READY-CONSTRUCTION",N,null);
    for(const query of queryOrdinals){
      const cpu=findSummary(summaries,"CPU-E2E-RAW",N,query);
      const raw=findSummary(summaries,"BND-E2E-RAW",N,query);
      const prepared=findSummary(summaries,"BND-E2E-PREP",N,query);
      comparisons.push(comparison(cpu,prepared,"H1",N,query),
        comparison(cpu,raw,"H2",N,query),comparison(raw,prepared,"H3",N,query));
      breakEvens.push(breakEven(cpu,prepared,ready,"cpu-raw",N,query),
        breakEven(raw,prepared,ready,"v2-raw",N,query));
    }
  }
  const environment=evaluateEnvironment(directory,manifest,config);
  fs.writeFileSync(path.join(directory,"environment-status.json"),JSON.stringify(environment,null,2)+"\n");
  const hypotheses=["H1","H2","H3"].map(id=>({id,required_workloads:128,
    outcome:universal(comparisons.filter(row=>row.id===id))}));
  if(environment.status!=="accepted-for-analysis"){
    for(const row of comparisons) row.outcome="inconclusive";
    for(const row of hypotheses) row.outcome="inconclusive";
    for(const row of breakEvens) if(row.outcome==="finite-break-even-supported")
      row.outcome="environment-inconclusive";
  }
  const perN=candidateCounts.map(N=>({N,query_count:32,hypotheses:["H1","H2","H3"].map(id=>{
    const selected=comparisons.filter(row=>row.N===N&&row.id===id);
    return {id,supported:selected.filter(row=>row.outcome==="supported").length,
      rejected:selected.filter(row=>row.outcome==="rejected").length,
      inconclusive:selected.filter(row=>row.outcome==="inconclusive").length};
  })}));
  writeCsv(path.join(directory,"summary.csv"),summaries,["boundary","N","query_ordinal",
    "block_size","sample_count","percentile_method","median_ns","p5_ns","p95_ns","ci_low_ns","ci_high_ns"]);
  fs.writeFileSync(path.join(directory,"summary.json"),JSON.stringify({schema_version:"S15-SUMMARY-v1",
    uncertainty:"deterministic percentile bootstrap, 2000 resamples, 95% median interval",summaries},null,2)+"\n");
  fs.writeFileSync(path.join(directory,"comparisons.json"),JSON.stringify({schema_version:"S15-COMPARISONS-v1",
    evidence_acceptance:environment.status==="accepted-for-analysis"?"accepted for performance analysis":"environment inconclusive",
    per_workload_outcomes:comparisons,per_n_descriptive_results:perN,
    universal_hypothesis_outcomes:hypotheses},null,2)+"\n");
  fs.writeFileSync(path.join(directory,"break-even.json"),JSON.stringify({schema_version:"S15-BREAK-EVEN-v1",
    persistent_resource_destruction:"excluded; results are not complete lifetime costs",break_even:breakEvens},null,2)+"\n");
  fs.writeFileSync(path.join(directory,"acceptance.json"),JSON.stringify({schema_version:"S15-ACCEPTANCE-v1",
    evidence_acceptance:environment.status==="accepted-for-analysis"?"accepted for performance analysis":"environment inconclusive",
    performance_favorability_is_not_an_evidence_gate:true,
    per_workload_outcome_count:comparisons.length,per_n_group_count:perN.length,
    universal_hypothesis_outcomes:hypotheses,environment_status:environment.status},null,2)+"\n");
  writePacketManifest(directory);
}

function selfTest(){
  if(percentile([1,2,3,4,5],.5)!==3 || Math.abs(percentile([1,2,3,4,5],.05)-1.2)>1e-12) fail("percentile self-test failed");
  const first=bootstrapMedian([1,2,3,4,5],"identity"), second=bootstrapMedian([1,2,3,4,5],"identity");
  if(JSON.stringify(first)!==JSON.stringify(second)) fail("bootstrap determinism failed");
  const reference={boundary:"CPU-E2E-RAW",median_ns:100,ci_low_ns:95,ci_high_ns:105};
  const prepared={boundary:"BND-E2E-PREP",median_ns:40,ci_low_ns:38,ci_high_ns:42};
  const ready={boundary:"COST-PREPARED-READY-CONSTRUCTION",median_ns:150,ci_low_ns:140,ci_high_ns:160};
  const result=breakEven(reference,prepared,ready,"cpu-raw",1024,0);
  if(result.minimum_integer_queries_for_speedup_gt_1!==3 || result.conservative_minimum_queries!==4) fail("break-even self-test failed");
  const config={schema_version:"S15-PUBLIC-SIFT1M-CONFIG-v1",workload:{candidate_counts:candidateCounts,dimension:128,
    query_ordinals:queryOrdinals,base_file:{bytes:516000000,sha256:"21f66e2975057b5728ba56de1c825bac4f4d89d596609ae985741c6242631816"},
    query_file:{bytes:5160000,sha256:"f7fc9be140accdfd64116c2fa2365ecdb69b8f084970c6b0532db5ff79ac8fdc"}},
    sampling:{warmups:10,measured_repetitions:100,bootstrap_resamples:2000},
    environment_policy:{telemetry_interval_ms:1000,maximum_accepted_start_gap_ms:3000,telemetry_query_timeout_ms:900},
    v2_block_mapping:Object.fromEntries(blockMapping),fast_math:"disabled"};
  validateConfig(config);
  const corrupted=structuredClone(config); corrupted.workload.query_ordinals[1]=323;
  let rejected=false; try{validateConfig(corrupted);}catch{rejected=true;}
  if(!rejected) fail("query-list corruption was accepted");
  const directory=fs.mkdtempSync(path.join(os.tmpdir(),"s15-analysis-self-test-"));
  try{
    fs.writeFileSync(path.join(directory,"preflight.json"),"{}");
    rejected=false; try{analyze(directory);}catch(error){rejected=String(error.message).includes("rejects preflight");}
    if(!rejected) fail("preflight was accepted as formal evidence");
  }finally{fs.rmSync(directory,{recursive:true,force:true});}
}

const args=process.argv.slice(2);
if(args.length===1&&args[0]==="--self-test") selfTest();
else if(args.length===1) analyze(path.resolve(args[0]));
else fail("usage: analyze-s15-public-sift1m.mjs --self-test | FORMAL_RESULTS_DIRECTORY");
