#!/usr/bin/env node
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import crypto from "node:crypto";

const samplingGapPolicyMs = 2500;

function fail(message) { throw new Error(message); }
function sha256File(file) {
  return crypto.createHash("sha256").update(fs.readFileSync(file)).digest("hex");
}
function exactKeys(object, expected, label) {
  if (!object || typeof object !== "object" || Array.isArray(object) ||
      Object.keys(object).length !== expected.length ||
      expected.some(key => !Object.hasOwn(object,key))) fail(`invalid ${label} schema`);
}
function parseTime(value, label) {
  const time = Date.parse(value);
  if (!Number.isFinite(time)) fail(`invalid ${label} timestamp`);
  return time;
}
function parseStrictJson(text,label) {
  let position=0;
  const whitespace=()=>{ while (/\s/.test(text[position] ?? "")) ++position; };
  const string=()=>{
    const start=position;
    if (text[position++] !== '"') fail(`invalid ${label} JSON string`);
    while (position < text.length) {
      if (text[position] === '"') {
        ++position;
        try { return JSON.parse(text.slice(start,position)); }
        catch { fail(`invalid ${label} JSON string`); }
      }
      if (text[position++] === "\\") {
        if (position >= text.length) fail(`invalid ${label} JSON escape`);
        if (text[position] === "u") {
          if (!/^[0-9a-fA-F]{4}$/.test(text.slice(position+1,position+5))) fail(`invalid ${label} JSON escape`);
          position += 5;
        } else ++position;
      }
    }
    fail(`unterminated ${label} JSON string`);
  };
  const value=()=>{
    whitespace();
    if (text[position] === '"') { string(); return; }
    if (text[position] === "{") {
      ++position; whitespace(); const keys=new Set();
      if (text[position] === "}") { ++position; return; }
      for (;;) {
        whitespace(); const key=string();
        if (keys.has(key)) fail(`duplicate key in ${label}: ${key}`); keys.add(key);
        whitespace(); if (text[position++] !== ":") fail(`invalid ${label} JSON object`);
        value(); whitespace();
        if (text[position] === "}") { ++position; return; }
        if (text[position++] !== ",") fail(`invalid ${label} JSON object`);
      }
    }
    if (text[position] === "[") {
      ++position; whitespace();
      if (text[position] === "]") { ++position; return; }
      for (;;) {
        value(); whitespace();
        if (text[position] === "]") { ++position; return; }
        if (text[position++] !== ",") fail(`invalid ${label} JSON array`);
      }
    }
    const token=text.slice(position).match(/^(?:true|false|null|-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?)/)?.[0];
    if (!token) fail(`invalid ${label} JSON value`); position += token.length;
  };
  value(); whitespace();
  if (position !== text.length) fail(`trailing content in ${label}`);
  return JSON.parse(text);
}
function readStrictJson(file,label=path.basename(file)) {
  return parseStrictJson(fs.readFileSync(file,"utf8"),label);
}

function decodeXmlText(value) {
  return value.replace(/&(?:#x[0-9a-fA-F]+|#[0-9]+|amp|lt|gt|quot|apos);/g, entity => {
    if (entity === "&amp;") return "&";
    if (entity === "&lt;") return "<";
    if (entity === "&gt;") return ">";
    if (entity === "&quot;") return '"';
    if (entity === "&apos;") return "'";
    const hexadecimal = entity.startsWith("&#x");
    const codePoint = Number.parseInt(entity.slice(hexadecimal ? 3 : 2,-1),hexadecimal ? 16 : 10);
    if (!Number.isInteger(codePoint) || codePoint < 0 || codePoint > 0x10ffff) fail("invalid XML entity");
    return String.fromCodePoint(codePoint);
  });
}

function parseXmlDocument(text,label) {
  const root={name:"#document",children:[],text:""}, stack=[root];
  let position=0;
  while (position < text.length) {
    const open=text.indexOf("<",position);
    if (open < 0) {
      if (text.slice(position).trim()) fail(`trailing text in ${label}`);
      break;
    }
    stack.at(-1).text += decodeXmlText(text.slice(position,open));
    if (text.startsWith("<!--",open)) {
      const close=text.indexOf("-->",open+4); if (close < 0) fail(`malformed ${label}`);
      position=close+3; continue;
    }
    if (text.startsWith("<?",open)) {
      const close=text.indexOf("?>",open+2); if (close < 0) fail(`malformed ${label}`);
      position=close+2; continue;
    }
    if (text.startsWith("<!DOCTYPE",open)) {
      const close=text.indexOf(">",open+9); if (close < 0) fail(`malformed ${label}`);
      position=close+1; continue;
    }
    const close=text.indexOf(">",open+1); if (close < 0) fail(`malformed ${label}`);
    const token=text.slice(open+1,close).trim();
    if (!token || token.startsWith("!")) fail(`malformed ${label}`);
    if (token.startsWith("/")) {
      const name=token.slice(1).trim();
      if (!/^[A-Za-z_][\w:.-]*$/.test(name) || stack.length === 1 || stack.at(-1).name !== name) {
        fail(`mismatched element in ${label}`);
      }
      stack.pop();
    } else {
      const selfClosing=token.endsWith("/");
      const content=(selfClosing ? token.slice(0,-1) : token).trim();
      const match=content.match(/^([A-Za-z_][\w:.-]*)(?:\s+[^<>]*)?$/);
      if (!match) fail(`malformed element in ${label}`);
      const node={name:match[1],children:[],text:""};
      stack.at(-1).children.push(node);
      if (!selfClosing) stack.push(node);
    }
    position=close+1;
  }
  if (stack.length !== 1 || root.children.length !== 1) fail(`malformed ${label}`);
  return root.children[0];
}

function xmlChildren(node,name) { return node.children.filter(child => child.name === name); }
function xmlChildText(node,name,label) {
  const matches=xmlChildren(node,name);
  if (matches.length !== 1 || matches[0].children.length) fail(`invalid ${label} XML field ${name}`);
  const value=matches[0].text.trim(); if (!value) fail(`empty ${label} XML field ${name}`); return value;
}
function parseNvidiaXml(file,expectedUuid,label) {
  const root=parseXmlDocument(fs.readFileSync(file,"utf8"),label);
  if (root.name !== "nvidia_smi_log") fail(`invalid ${label} root`);
  const driverVersion=xmlChildText(root,"driver_version",label);
  const matches=xmlChildren(root,"gpu").filter(gpu => {
    try { return normalizeUuid(xmlChildText(gpu,"uuid",label)) === normalizeUuid(expectedUuid); }
    catch { return false; }
  });
  if (matches.length !== 1) fail(`ambiguous or missing ${label} GPU identity`);
  const gpu=matches[0], processesNodes=xmlChildren(gpu,"processes");
  if (processesNodes.length > 1) fail(`ambiguous ${label} process list`);
  const processPids=[];
  if (processesNodes.length === 1) {
    for (const processInfo of xmlChildren(processesNodes[0],"process_info")) {
      const pid=xmlChildText(processInfo,"pid",label);
      if (!/^[1-9][0-9]*$/.test(pid) || !Number.isSafeInteger(Number(pid))) fail(`invalid ${label} process PID`);
      processPids.push(Number(pid));
    }
  }
  if (new Set(processPids).size !== processPids.length) fail(`duplicate ${label} process PID`);
  return {driver_version:driverVersion,gpu_name:xmlChildText(gpu,"product_name",label),
    gpu_uuid:xmlChildText(gpu,"uuid",label),process_pids:processPids.sort((a,b)=>a-b)};
}

function parseCsv(file) {
  const text = fs.readFileSync(file, "utf8").trim();
  if (!text) fail("empty CSV");
  const lines = text.split(/\r?\n/);
  const header = lines.shift().split(",");
  const expected = ["schema_version","revision","tree_state","sample_role","boundary",
    "variant","N","D","query_ordinal","block_size","run_order","iteration","warmup",
    "elapsed_ns","timer","status"];
  if (header.length !== expected.length || header.some((value, i) => value !== expected[i]) ||
      new Set(header).size !== header.length) fail("unexpected S10 CSV schema");
  return lines.filter(Boolean).map(line => {
    const values = line.split(",");
    if (values.length !== header.length) fail("malformed CSV row");
    return Object.fromEntries(header.map((key, index) => [key, values[index]]));
  });
}

const candidateCounts = [1024, 16384, 262144, 1000000];
const cudaVariants = ["v0", "v1", "v2", "v3"];
function integer(row, field) {
  if (!/^(0|[1-9][0-9]*)$/.test(row[field] ?? "")) fail(`invalid ${field}`);
  const value = Number(row[field]);
  if (!Number.isSafeInteger(value)) fail(`invalid ${field}`);
  return value;
}
function expectedRows() {
  const expected = [];
  for (const N of candidateCounts) {
    for (let iteration = 0; iteration < 100; ++iteration) {
      expected.push({variant:"cpu-f32", boundary:"CPU-E2E-RAW", N, block_size:0,
        run_order:0, iteration, timer:"steady_clock"});
    }
    for (let iteration = 0; iteration < 100; ++iteration) {
      const offset = iteration % cudaVariants.length;
      for (let order = 0; order < cudaVariants.length; ++order) {
        const variant = cudaVariants[(order + offset) % cudaVariants.length];
        expected.push({variant, boundary:"BND-E2E-RAW", N, block_size:256,
          run_order:order, iteration, timer:"steady_clock"});
        expected.push({variant, boundary:"BND-KERNEL", N, block_size:256,
          run_order:order, iteration, timer:"cuda_event"});
      }
    }
  }
  return expected;
}
const expectedMatrixRows = expectedRows();
function validateRows(rows) {
  if (rows.length !== expectedMatrixRows.length) fail("unexpected S10 raw sample count");
  for (let i = 0; i < rows.length; ++i) {
    const row = rows[i], expected = expectedMatrixRows[i];
    if (row.schema_version !== "S10-RAW-SAMPLE-v1" || row.sample_role !== "final" ||
        row.status !== "completed" || row.warmup !== "false" ||
        row.variant !== expected.variant || row.boundary !== expected.boundary ||
        row.timer !== expected.timer || integer(row,"N") !== expected.N ||
        integer(row,"D") !== 128 || integer(row,"query_ordinal") !== 0 ||
        integer(row,"block_size") !== expected.block_size ||
        integer(row,"run_order") !== expected.run_order ||
        integer(row,"iteration") !== expected.iteration) {
      fail(`raw sample matrix/order mismatch at row ${i + 1}`);
    }
    integer(row,"elapsed_ns");
  }
}

function percentile(values, fraction) {
  if (!values.length) fail("empty distribution");
  const sorted = [...values].sort((a, b) => a - b);
  const position = fraction * (sorted.length - 1);
  const lower = Math.floor(position), upper = Math.ceil(position);
  return sorted[lower] + (sorted[upper] - sorted[lower]) * (position - lower);
}

function seed(identity) {
  let value = 2166136261 >>> 0;
  for (const byte of Buffer.from(identity)) {
    value ^= byte;
    value = Math.imul(value, 16777619) >>> 0;
  }
  return value || 1;
}

function bootstrapMedian(values, identity) {
  let state = seed(identity);
  const random = () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    return state / 0x100000000;
  };
  const medians = [];
  for (let sample = 0; sample < 2000; ++sample) {
    const resampled = Array.from({length: values.length},
      () => values[Math.floor(random() * values.length)]);
    medians.push(percentile(resampled, 0.5));
  }
  return [percentile(medians, 0.025), percentile(medians, 0.975)];
}

function groupKey(row) {
  return [row.variant, row.boundary, row.N, row.D, row.query_ordinal,
          row.block_size].join("|");
}

function summarize(rows) {
  const groups = new Map();
  for (const row of rows) {
    if (row.schema_version !== "S10-RAW-SAMPLE-v1" ||
        row.sample_role !== "final" || row.status !== "completed" ||
        row.warmup !== "false") fail("invalid S10 sample row");
    const value = Number(row.elapsed_ns);
    if (!Number.isSafeInteger(value) || value < 0) fail("invalid elapsed_ns");
    const values = groups.get(groupKey(row)) ?? [];
    values.push(value);
    groups.set(groupKey(row), values);
  }
  return [...groups].map(([identity, values]) => {
    if (values.length !== 100) fail(`expected 100 samples for ${identity}`);
    const [variant, boundary, N, D, queryOrdinal, blockSize] = identity.split("|");
    const [ciLow, ciHigh] = bootstrapMedian(values, identity);
    return {variant, boundary, N:Number(N), D:Number(D),
      query_ordinal:Number(queryOrdinal), block_size:Number(blockSize),
      sample_count:values.length, percentile_method:"linear_n_minus_1",
      median_ns:percentile(values, 0.5), p5_ns:percentile(values, 0.05),
      p95_ns:percentile(values, 0.95), ci_low_ns:ciLow, ci_high_ns:ciHigh};
  }).sort((a, b) => groupKey(a).localeCompare(groupKey(b)));
}

function validateMatrix(summaries) {
  const ns = candidateCounts;
  const variants = cudaVariants;
  const find = (variant, boundary, N) => summaries.find(row =>
    row.variant === variant && row.boundary === boundary && row.N === N &&
    row.D === 128 && row.query_ordinal === 0);
  for (const N of ns) {
    const cpu = find("cpu-f32", "CPU-E2E-RAW", N);
    if (!cpu || cpu.block_size !== 0) fail(`missing CPU distribution N=${N}`);
    for (const variant of variants) {
      const raw = find(variant, "BND-E2E-RAW", N), kernel = find(variant, "BND-KERNEL", N);
      if (!raw || !kernel || raw.block_size !== 256 || kernel.block_size !== 256) {
        fail(`missing CUDA distribution ${variant} N=${N}`);
      }
    }
  }
  if (summaries.length !== 36) fail("unexpected S10 distribution count");
}

function readJsonLines(file) {
  const text = fs.readFileSync(file,"utf8").trim();
  if (!text) fail(`empty JSONL: ${file}`);
  return text.split(/\r?\n/).map((line,index) => parseStrictJson(line,`${path.basename(file)} row ${index+1}`));
}
function normalizeUuid(value) { return String(value).toLowerCase().replace(/^gpu-/,"").replaceAll("-",""); }
function analyzeEnvironment(directory, manifest) {
  const capture = path.join(directory,"environment-capture");
  const host = readStrictJson(path.join(capture,"host-environment.json"));
  for (const field of ["revision","working_tree_state","operating_system","processor","compiler_id","compiler_version",
    "compiler_path","cuda_compiler_path","cuda_toolkit_version","driver_version","gpu_name","gpu_uuid",
    "build_type","cuda_architecture","build_flags","source_manifest_sha256","config_sha256",
    "sift_base_sha256","sift_query_sha256","numerical_build_identity_sha256",
    "artifact_manifest_sha256","benchmark_binary_sha256"]) {
    if (typeof host[field] !== "string" || !host[field]) fail(`missing environment field ${field}`);
  }
  if (host.revision !== manifest.revision || host.working_tree_state !== "clean" ||
      host.source_manifest_sha256 !== manifest.source_manifest_sha256 ||
      host.config_sha256 !== manifest.config_sha256 ||
      host.sift_base_sha256 !== manifest.sift_base_sha256 ||
      host.sift_query_sha256 !== manifest.sift_query_sha256 || host.gpu_name !== manifest.gpu_name ||
      normalizeUuid(host.gpu_uuid) !== normalizeUuid(manifest.gpu_uuid) ||
      host.cuda_architecture !== manifest.cuda_architecture || host.build_flags !== manifest.build_flags ||
      host.numerical_build_identity_sha256 !== manifest.numerical_build_identity_sha256 ||
      host.artifact_manifest_sha256 !== manifest.artifact_manifest_sha256 ||
      host.benchmark_binary_sha256 !== manifest.benchmark_binary_sha256) fail("environment identity mismatch");
  for (const name of ["nvidia-smi-before.xml","nvidia-smi-after.xml","gpu-before.json",
    "gpu-after.json","compile_commands.json","CMakeCache.txt","numerical-build-identity.json",
    "artifact-manifest.json","artifact-post-manifest.json","benchmark-interval.json"]) {
    const target = path.join(capture,name);
    if (!fs.existsSync(target) || fs.statSync(target).size === 0) fail(`missing environment artifact ${name}`);
  }
  const artifactPath = path.join(capture,"artifact-manifest.json");
  const postArtifactPath = path.join(capture,"artifact-post-manifest.json");
  const numericalPath = path.join(capture,"numerical-build-identity.json");
  if (sha256File(artifactPath) !== manifest.artifact_manifest_sha256 ||
      sha256File(numericalPath) !== manifest.numerical_build_identity_sha256) {
    fail("authoritative build artifact hash mismatch");
  }
  const artifact = readStrictJson(artifactPath);
  const postArtifact = readStrictJson(postArtifactPath);
  exactKeys(artifact,["schema_version","artifacts"],"artifact manifest");
  exactKeys(postArtifact,["schema_version","artifacts"],"post artifact manifest");
  if (artifact.schema_version !== "S10-ARTIFACT-MANIFEST-v1" ||
      postArtifact.schema_version !== "S10-POST-RUN-ARTIFACT-MANIFEST-v1") fail("artifact manifest version mismatch");
  const expectedArtifacts = ["s10_sift1m.exe","s10_sift1m_io.lib","s6_cpu_baseline.lib",
    "s6_cpu_evidence.lib","s7_cuda_v0.lib","s7_cuda_v1.lib","s7_cuda_v2.lib","s7_cuda_v3.lib"];
  const validateArtifacts = rows => {
    if (!Array.isArray(rows) || rows.length !== expectedArtifacts.length) fail("artifact membership is incomplete");
    rows.forEach((row,index) => {
      exactKeys(row,["name","bytes","sha256"],"artifact record");
      if (row.name !== expectedArtifacts[index] || !Number.isSafeInteger(row.bytes) || row.bytes <= 0 ||
          !/^[0-9a-f]{64}$/.test(row.sha256)) fail("invalid artifact record");
    });
  };
  validateArtifacts(artifact.artifacts); validateArtifacts(postArtifact.artifacts);
  if (JSON.stringify(artifact.artifacts) !== JSON.stringify(postArtifact.artifacts) ||
      artifact.artifacts[0].sha256 !== manifest.benchmark_binary_sha256) fail("runtime artifact identity drift");

  const numerical = readStrictJson(numericalPath);
  exactKeys(numerical,["schema_version","build_type","generator","cuda_architecture","compiler_id",
    "compiler_version","compiler_path","cuda_compiler_version","cuda_compiler_path",
    "cuda_toolkit_version","build_flags","source_manifest_sha256","compile_commands"],
    "numerical build identity");
  if (numerical.schema_version !== "S10-NUMERICAL-BUILD-IDENTITY-v1" ||
      numerical.build_type !== "Release" || numerical.cuda_architecture !== manifest.cuda_architecture ||
      numerical.build_flags !== manifest.build_flags ||
      numerical.source_manifest_sha256 !== manifest.source_manifest_sha256) fail("numerical build identity mismatch");
  const compileCommands = readStrictJson(path.join(capture,"compile_commands.json"));
  const expectedSources = ["src/cpu_baseline.cpp","src/cpu_evidence.cpp","benchmarks/s10_sift1m.cpp",
    "benchmarks/s10_sift1m_loader.cpp","src/cuda_v0.cu","src/cuda_v1.cu","src/cuda_v2.cu","src/cuda_v3.cu"];
  if (!Array.isArray(numerical.compile_commands) || numerical.compile_commands.length !== expectedSources.length ||
      !Array.isArray(compileCommands)) fail("compile command identity is incomplete");
  numerical.compile_commands.forEach((entry,index) => {
    exactKeys(entry,["source","command"],"compile command identity");
    const source = expectedSources[index];
    const actual = compileCommands.find(row => String(row.file).replaceAll("\\","/").endsWith(source));
    if (entry.source !== source || !actual || entry.command !== actual.command) fail("compile command identity mismatch");
    const command = entry.command;
    if (/--use_fast_math|-ffast-math|\/fp:fast|--ftz=true|--prec-div=false|--prec-sqrt=false/i.test(command)) {
      fail("relaxed numerical compile flag detected");
    }
    if (source.endsWith(".cu")) {
      for (const flag of ["--ftz=false","--fmad=true","--prec-div=true","--prec-sqrt=true","-Xcompiler=/fp:strict"]) {
        if (!command.includes(flag)) fail(`missing CUDA numerical flag ${flag}`);
      }
    } else if (!command.includes("/fp:strict")) fail("missing CPU /fp:strict");
  });
  const cache = fs.readFileSync(path.join(capture,"CMakeCache.txt"),"utf8");
  const cacheValue = name => {
    const match = cache.match(new RegExp(`^${name}(?::[^=]+)?=(.*)$`,"m"));
    if (!match) fail(`missing CMake cache field ${name}`); return match[1].trim();
  };
  if (cacheValue("CMAKE_BUILD_TYPE") !== "Release" ||
      cacheValue("CMAKE_CUDA_ARCHITECTURES") !== numerical.cuda_architecture ||
      cacheValue("CMAKE_CXX_COMPILER").replaceAll("\\","/") !== numerical.compiler_path.replaceAll("\\","/") ||
      cacheValue("CMAKE_CUDA_COMPILER").replaceAll("\\","/") !== numerical.cuda_compiler_path.replaceAll("\\","/")) {
    fail("CMake configuration disagrees with numerical build identity");
  }

  const before = readStrictJson(path.join(capture,"gpu-before.json"));
  const after = readStrictJson(path.join(capture,"gpu-after.json"));
  const snapshotKeys = ["schema_version","captured_at","xml_sha256","driver_version","gpu_name",
    "gpu_uuid","pstate","temperature_gpu","clock_sm","clock_memory","power_draw","power_limit",
    "hardware_thermal_slowdown","processes"];
  exactKeys(before,snapshotKeys,"preflight GPU snapshot"); exactKeys(after,snapshotKeys,"postflight GPU snapshot");
  const beforeXmlPath=path.join(capture,"nvidia-smi-before.xml");
  const afterXmlPath=path.join(capture,"nvidia-smi-after.xml");
  const beforeXml=parseNvidiaXml(beforeXmlPath,host.gpu_uuid,"preflight NVIDIA");
  const afterXml=parseNvidiaXml(afterXmlPath,host.gpu_uuid,"postflight NVIDIA");
  if (before.schema_version !== "S10-GPU-SNAPSHOT-v1" || after.schema_version !== "S10-GPU-SNAPSHOT-v1" ||
      before.xml_sha256 !== sha256File(beforeXmlPath) || after.xml_sha256 !== sha256File(afterXmlPath) ||
      normalizeUuid(before.gpu_uuid) !== normalizeUuid(host.gpu_uuid) ||
      normalizeUuid(after.gpu_uuid) !== normalizeUuid(host.gpu_uuid) || before.driver_version !== host.driver_version ||
      after.driver_version !== host.driver_version || before.gpu_name !== host.gpu_name || after.gpu_name !== host.gpu_name ||
      normalizeUuid(beforeXml.gpu_uuid) !== normalizeUuid(before.gpu_uuid) ||
      normalizeUuid(afterXml.gpu_uuid) !== normalizeUuid(after.gpu_uuid) ||
      beforeXml.driver_version !== before.driver_version || afterXml.driver_version !== after.driver_version ||
      beforeXml.gpu_name !== before.gpu_name || afterXml.gpu_name !== after.gpu_name) {
    fail("pre/post GPU identity or XML continuity mismatch");
  }
  if (!Array.isArray(before.processes) || !Array.isArray(after.processes) ||
      before.processes.some(row => !/^[1-9][0-9]*$/.test(String(row.pid)) || !Number.isSafeInteger(Number(row.pid))) ||
      after.processes.some(row => !/^[1-9][0-9]*$/.test(String(row.pid)) || !Number.isSafeInteger(Number(row.pid))) ||
      JSON.stringify(before.processes.map(row=>Number(row.pid)).sort((a,b)=>a-b)) !== JSON.stringify(beforeXml.process_pids) ||
      JSON.stringify(after.processes.map(row=>Number(row.pid)).sort((a,b)=>a-b)) !== JSON.stringify(afterXml.process_pids) ||
      before.processes.length || after.processes.length) fail("pre/post GPU compute process contamination");

  const interval = readStrictJson(path.join(capture,"benchmark-interval.json"));
  exactKeys(interval,["schema_version","benchmark_started_at","benchmark_ended_at","benchmark_pid",
    "benchmark_exit_code","telemetry_pid","telemetry_exit_code","telemetry_exited_before_benchmark_end",
    "telemetry_exited_before_stop_signal","process_telemetry_status","maximum_sampling_gap_ms"],"benchmark interval");
  const start = parseTime(interval.benchmark_started_at,"benchmark start");
  const end = parseTime(interval.benchmark_ended_at,"benchmark end");
  if (interval.schema_version !== "S10-BENCHMARK-INTERVAL-v1" || end < start ||
      !Number.isSafeInteger(interval.benchmark_pid) || interval.benchmark_pid <= 0 ||
      !Number.isSafeInteger(interval.telemetry_pid) || interval.telemetry_pid <= 0 ||
      interval.benchmark_pid === interval.telemetry_pid || interval.benchmark_exit_code !== 0 ||
      interval.telemetry_exit_code !== 0 || interval.telemetry_exited_before_benchmark_end !== false ||
      interval.telemetry_exited_before_stop_signal !== false ||
      !["supported","unsupported"].includes(interval.process_telemetry_status) ||
      interval.maximum_sampling_gap_ms !== samplingGapPolicyMs) {
    fail("invalid benchmark/telemetry lifecycle");
  }
  const telemetry = path.join(capture,"telemetry");
  const errors = fs.readFileSync(path.join(telemetry,"telemetry-errors.log"),"utf8").trim();
  if (errors && (interval.process_telemetry_status === "supported" ||
      errors.split(/\r?\n/).some(line => !/GPU process telemetry|process telemetry query failed/i.test(line)))) {
    fail("telemetry capture reported errors");
  }
  const metrics = readJsonLines(path.join(telemetry,"gpu-telemetry.jsonl"));
  const inconclusive = [];
  const snapshotNumber = (value,label) => {
    const text = String(value).trim();
    if (!text || /n\/a|not supported/i.test(text)) { inconclusive.push(`unsupported ${label}`); return null; }
    const match = text.match(/^(-?(?:\d+\.?\d*|\.\d+))(?:\s+[A-Za-z%]+)?$/);
    if (!match || !Number.isFinite(Number(match[1]))) fail(`malformed ${label}`);
    return Number(match[1]);
  };
  const snapshotMetrics={};
  for (const [label,snapshot] of [["preflight",before],["postflight",after]]) {
    snapshotMetrics[label]={};
    if (!/^P\d+$/i.test(String(snapshot.pstate))) inconclusive.push(`unsupported ${label} P-state`);
    for (const field of ["temperature_gpu","clock_sm","clock_memory","power_draw","power_limit"]) {
      const value = snapshotNumber(snapshot[field],`${label} ${field}`);
      if (value !== null && value < 0) fail(`negative ${label} ${field}`);
      snapshotMetrics[label][field]=value;
    }
    const slowdown = String(snapshot.hardware_thermal_slowdown).trim().toLowerCase();
    if (["n/a","[n/a]","not supported","[not supported]",""] .includes(slowdown)) {
      inconclusive.push(`unsupported ${label} thermal slowdown state`);
    } else if (!["not active","0"].includes(slowdown)) fail(`${label} thermal slowdown contamination`);
  }
  for (const phase of ["preflight","postflight"]) {
    const values=snapshotMetrics[phase];
    if (values.clock_sm === 0 || values.clock_memory === 0 || values.power_limit === 0) {
      fail(`invalid zero ${phase} clock or power limit`);
    }
    if (values.power_draw !== null && values.power_limit !== null && values.power_draw > values.power_limit) {
      fail(`${phase} power draw exceeds power limit`);
    }
  }
  const beforeLimit=snapshotMetrics.preflight.power_limit, afterLimit=snapshotMetrics.postflight.power_limit;
  if (beforeLimit !== null && afterLimit !== null && beforeLimit !== afterLimit) fail("GPU power limit changed during run");
  if (metrics.length < 2) inconclusive.push("fewer than two telemetry metric samples");
  const metricTimes = [];
  const numericMetric = (row,field) => {
    const text = String(row[field]).trim();
    if (["n/a","[n/a]","not supported","[not supported]"].includes(text.toLowerCase())) {
      inconclusive.push(`unsupported telemetry field ${field}`); return null;
    }
    if (!/^-?(?:\d+\.?\d*|\.\d+)$/.test(text)) fail(`malformed numeric telemetry field ${field}`);
    const value = Number(text); if (!Number.isFinite(value)) fail(`nonfinite telemetry field ${field}`); return value;
  };
  for (const row of metrics) {
    if (normalizeUuid(row.uuid) !== normalizeUuid(host.gpu_uuid)) fail("telemetry GPU identity changed");
    for (const field of ["timestamp","pstate","temperature_gpu_c","utilization_gpu_percent",
      "utilization_memory_percent","clock_sm_mhz","clock_memory_mhz","power_draw_w","power_limit_w",
      "pcie_generation","pcie_width","hardware_thermal_slowdown"]) {
      if (!(field in row) || String(row[field]).trim() === "") fail(`missing telemetry field ${field}`);
    }
    metricTimes.push(parseTime(row.captured_at,"telemetry metric"));
    if (!/^P\d+$/i.test(String(row.pstate))) inconclusive.push("unsupported GPU P-state telemetry");
    const numeric={};
    for (const field of ["temperature_gpu_c","utilization_gpu_percent","utilization_memory_percent",
      "clock_sm_mhz","clock_memory_mhz","power_draw_w","power_limit_w","pcie_generation","pcie_width"]) {
      const value = numericMetric(row,field); numeric[field]=value;
      if (value !== null && value < 0) fail(`negative telemetry field ${field}`);
    }
    if ((numeric.utilization_gpu_percent !== null && numeric.utilization_gpu_percent > 100) ||
        (numeric.utilization_memory_percent !== null && numeric.utilization_memory_percent > 100)) {
      fail("telemetry utilization exceeds 100 percent");
    }
    if (numeric.clock_sm_mhz === 0 || numeric.clock_memory_mhz === 0 ||
        numeric.power_limit_w === 0 || numeric.pcie_generation === 0 || numeric.pcie_width === 0) {
      fail("invalid zero telemetry clock, power limit, or PCIe state");
    }
    if (numeric.power_draw_w !== null && numeric.power_limit_w !== null &&
        numeric.power_draw_w > numeric.power_limit_w) fail("telemetry power draw exceeds power limit");
    if (beforeLimit !== null && numeric.power_limit_w !== null && numeric.power_limit_w !== beforeLimit) {
      fail("telemetry power limit changed during run");
    }
    const slowdown = String(row.hardware_thermal_slowdown).trim().toLowerCase();
    if (["n/a","[n/a]","not supported","[not supported]"].includes(slowdown)) {
      inconclusive.push("unsupported telemetry thermal slowdown state");
    } else if (!["not active","0"].includes(slowdown)) fail("GPU thermal slowdown contaminated measurement");
  }
  for (let i=1;i<metricTimes.length;++i) {
    if (metricTimes[i] <= metricTimes[i-1]) fail("telemetry timestamps are not strictly monotonic");
    if (metricTimes[i]-metricTimes[i-1] > interval.maximum_sampling_gap_ms) inconclusive.push("telemetry sampling gap exceeded");
  }
  if (!metricTimes.length || metricTimes[0] > start || metricTimes.at(-1) < end) {
    inconclusive.push("telemetry does not cover complete benchmark interval");
  }
  const processFile=path.join(telemetry,"gpu-processes.jsonl");
  const processes = interval.process_telemetry_status === "supported" ? readJsonLines(processFile) : [];
  const processTimes = [];
  const processIdentities=new Set();
  let benchmarkProcessObserved=false;
  for (const row of processes) {
    processTimes.push(parseTime(row.captured_at,"process telemetry"));
    if (row.no_processes === true) {
      exactKeys(row,["captured_at","gpu_uuid","pid","process_name","used_gpu_memory","no_processes"],
        "empty process telemetry row");
      if (row.gpu_uuid !== "" || row.pid !== "" || row.process_name !== "" || row.used_gpu_memory !== "") {
        fail("inconsistent empty process telemetry row");
      }
      continue;
    }
    exactKeys(row,["captured_at","nvidia_timestamp","gpu_uuid","pid","process_name","used_gpu_memory","no_processes"],
      "process telemetry row");
    if (row.no_processes !== false || !/^[1-9][0-9]*$/.test(String(row.pid)) ||
        !Number.isSafeInteger(Number(row.pid))) fail("malformed process telemetry PID");
    if (normalizeUuid(row.gpu_uuid) !== normalizeUuid(host.gpu_uuid)) {
      fail("process telemetry GPU identity changed");
    }
    const executable = path.basename(String(row.process_name)).toLowerCase();
    if (executable !== "s10_sift1m.exe") fail(`unexpected GPU compute process: ${executable}`);
    if (Number(row.pid) !== interval.benchmark_pid) fail("foreign GPU compute process PID");
    const identity=`${row.captured_at}|${row.pid}`;
    if (processIdentities.has(identity)) fail("duplicate GPU compute-process identity");
    processIdentities.add(identity); benchmarkProcessObserved=true;
  }
  for (let i=1;i<processTimes.length;++i) {
    if (processTimes[i] <= processTimes[i-1]) fail("process telemetry timestamps are not strictly monotonic");
    if (processTimes[i]-processTimes[i-1] > interval.maximum_sampling_gap_ms) inconclusive.push("process telemetry sampling gap exceeded");
  }
  if (!processTimes.length || processTimes[0] > start || processTimes.at(-1) < end) {
    inconclusive.push("process telemetry does not cover complete benchmark interval");
  }
  if (interval.process_telemetry_status === "supported" && !benchmarkProcessObserved) {
    fail("benchmark GPU compute process was not observed");
  }
  if (interval.process_telemetry_status === "unsupported") {
    inconclusive.push("NVIDIA GPU compute-process telemetry is unsupported");
  }
  return {status:inconclusive.length ? "INCONCLUSIVE" : "ACCEPTED",
    reasons:[...new Set(inconclusive)],metric_samples:metrics.length,process_samples:processes.length};
}

function compare(reference, candidate, identity) {
  if (!reference || !candidate) fail(`missing comparison ${identity}`);
  const low = reference.ci_low_ns / candidate.ci_high_ns;
  const high = reference.ci_high_ns / candidate.ci_low_ns;
  return {ratio:reference.median_ns / candidate.median_ns, ci_low:low, ci_high:high,
    outcome:low > 1 ? "supported" : high < 1 ? "rejected" : "inconclusive"};
}
function publishAcceptance(directory, value) {
  const destination = path.join(directory,"acceptance.json");
  if (fs.existsSync(destination) || fs.readdirSync(directory).some(name => name.startsWith("acceptance.json.tmp-"))) {
    fail("stale acceptance artifact collision");
  }
  const temporary = path.join(directory,`acceptance.json.tmp-${process.pid}`);
  try {
    fs.writeFileSync(temporary,JSON.stringify(value,null,2)+"\n",{encoding:"utf8",flag:"wx"});
    fs.renameSync(temporary,destination);
  } catch (error) {
    try { fs.rmSync(temporary,{force:true}); } catch {}
    throw error;
  }
}

function analyze(directory) {
  if (fs.existsSync(path.join(directory,"acceptance.json")) ||
      fs.readdirSync(directory).some(name => name.startsWith("acceptance.json.tmp-"))) {
    fail("output directory contains stale acceptance state");
  }
  const manifest = readStrictJson(path.join(directory,"manifest.json"));
  if (manifest.schema_version !== "S10-BENCHMARK-MANIFEST-v1" ||
      manifest.sample_role !== "final" || manifest.working_tree_state !== "clean" ||
      manifest.measurement_status !== "valid-unreviewed" || manifest.fast_math !== "disabled" ||
      manifest.v2_block_size !== 256 || manifest.v3_block_size !== 256 ||
      manifest.warmups !== 10 || manifest.repetitions !== 100 ||
      !/^[0-9a-f]{64}$/.test(manifest.source_manifest_sha256) ||
      !/^[0-9a-f]{64}$/.test(manifest.config_sha256) ||
      !/^[0-9a-f]{64}$/.test(manifest.sift_base_sha256) ||
      !/^[0-9a-f]{64}$/.test(manifest.sift_query_sha256) ||
      !/^[0-9a-f]{64}$/.test(manifest.correctness_ledger_sha256) ||
      !/^[0-9a-f]{64}$/.test(manifest.benchmark_binary_sha256) ||
      !/^[0-9a-f]{64}$/.test(manifest.artifact_manifest_sha256) ||
      !/^[0-9a-f]{64}$/.test(manifest.numerical_build_identity_sha256)) {
    fail("invalid S10 benchmark manifest");
  }
  const rows = parseCsv(path.join(directory, "raw_samples.csv"));
  validateRows(rows);
  if (new Set(rows.map(row => row.revision)).size !== 1 ||
      rows[0].revision !== manifest.revision ||
      new Set(rows.map(row => row.tree_state)).size !== 1 ||
      rows[0].tree_state !== manifest.working_tree_state) fail("sample identity mismatch");
  const summaries = summarize(rows);
  validateMatrix(summaries);
  const fields = ["variant","boundary","N","D","query_ordinal","block_size",
    "sample_count","percentile_method","median_ns","p5_ns","p95_ns",
    "ci_low_ns","ci_high_ns"];
  fs.writeFileSync(path.join(directory, "summary.csv"), fields.join(",") + "\n" +
    summaries.map(row => fields.map(field => row[field]).join(",")).join("\n") + "\n");
  fs.writeFileSync(path.join(directory, "summary.json"), JSON.stringify({
    schema_version:"S10-SUMMARY-v1",
    uncertainty:"deterministic percentile bootstrap, 2000 resamples, 95% median interval",
    summaries}, null, 2) + "\n");

  const find = (variant, boundary, N) => summaries.find(row =>
    row.variant === variant && row.boundary === boundary && row.N === N);
  const comparisons = [];
  for (const N of [1024, 16384, 262144, 1000000]) {
    const cpu = find("cpu-f32", "CPU-E2E-RAW", N);
    const v0Kernel = find("v0", "BND-KERNEL", N);
    for (const variant of ["v0", "v1", "v2", "v3"]) {
      comparisons.push({kind:"cpu-to-gpu-raw", N, variant,
        ...compare(cpu, find(variant, "BND-E2E-RAW", N), `${variant} raw N=${N}`)});
      if (variant !== "v0") comparisons.push({kind:"v0-to-variant-kernel", N, variant,
        ...compare(v0Kernel, find(variant, "BND-KERNEL", N), `${variant} kernel N=${N}`)});
    }
  }
  fs.writeFileSync(path.join(directory, "comparisons.json"), JSON.stringify({
    schema_version:"S10-COMPARISONS-v1", comparisons}, null, 2) + "\n");
  const environment = analyzeEnvironment(directory, manifest);
  publishAcceptance(directory,{
    schema_version:"S10-EXPERIMENT-ACCEPTANCE-v1", measurement_run_status:"valid",
    experiment_status:environment.status, telemetry_status:environment.status,
    telemetry_reasons:environment.reasons,
    revision:manifest.revision, correctness_ledger_sha256:manifest.correctness_ledger_sha256,
    artifact_manifest_sha256:manifest.artifact_manifest_sha256,
    numerical_build_identity_sha256:manifest.numerical_build_identity_sha256
  });
}

function selfTest() {
  let duplicateRejected=false;
  try { parseStrictJson('{"status":"valid","status":"accepted"}',"duplicate-key self-test"); }
  catch { duplicateRejected=true; }
  if (!duplicateRejected) fail("duplicate JSON key self-test failed");
  if (percentile([1,2,3,4,5], 0.5) !== 3 ||
      percentile([0,10], 0.05) !== 0.5) fail("percentile self-test failed");
  const first = bootstrapMedian([1,2,3,4,5], "s10");
  const second = bootstrapMedian([1,2,3,4,5], "s10");
  if (first[0] !== second[0] || first[1] !== second[1]) {
    fail("bootstrap determinism self-test failed");
  }
  const rows = expectedMatrixRows.map(row => ({schema_version:"S10-RAW-SAMPLE-v1",
    revision:"r",tree_state:"clean",sample_role:"final",boundary:row.boundary,
    variant:row.variant,N:String(row.N),D:"128",query_ordinal:"0",
    block_size:String(row.block_size),run_order:String(row.run_order),
    iteration:String(row.iteration),warmup:"false",elapsed_ns:"1",timer:row.timer,
    status:"completed"}));
  validateRows(rows);
  const rejected = mutate => {
    const candidate = rows.map(row => ({...row})); mutate(candidate);
    try { validateRows(candidate); } catch { return true; } return false;
  };
  const checks = [
    r => r.pop(),
    r => { r[1] = {...r[0]}; },
    r => { [r[0],r[1]] = [r[1],r[0]]; },
    r => { r[0].iteration = "100"; },
    r => { r[100].block_size = "128"; },
    r => { r[100].timer = "cuda_event"; },
    r => { r[100].run_order = "4"; },
    r => { r[0].N = "16384"; },
    r => { r[100].boundary = "BND-KERNEL"; },
    r => { r[0].elapsed_ns = "-1"; }
  ];
  if (!checks.every(check => rejected(check))) fail("raw matrix negative self-test failed");
  const summaries = summarize(rows);
  validateMatrix(summaries);
  const temp = fs.mkdtempSync(path.join(os.tmpdir(),"s10-analysis-self-test-"));
  try {
    const capture = path.join(temp,"environment-capture"), telemetry = path.join(capture,"telemetry");
    fs.mkdirSync(telemetry,{recursive:true});
    const stableHash = "a".repeat(64);
    const artifactNames = ["s10_sift1m.exe","s10_sift1m_io.lib","s6_cpu_baseline.lib",
      "s6_cpu_evidence.lib","s7_cuda_v0.lib","s7_cuda_v1.lib","s7_cuda_v2.lib","s7_cuda_v3.lib"];
    const sources = ["src/cpu_baseline.cpp","src/cpu_evidence.cpp","benchmarks/s10_sift1m.cpp",
      "benchmarks/s10_sift1m_loader.cpp","src/cuda_v0.cu","src/cuda_v1.cu","src/cuda_v2.cu","src/cuda_v3.cu"];
    const compileIdentity = sources.map(source => ({source,command:source.endsWith(".cu")
      ? `nvcc ${source} --ftz=false --fmad=true --prec-div=true --prec-sqrt=true -Xcompiler=/fp:strict`
      : `cl ${source} /fp:strict`}));
    const compileCommands = compileIdentity.map(row => ({file:`C:/repo/${row.source}`,command:row.command}));
    const times = ["2026-01-01T00:00:00.500Z","2026-01-01T00:00:01.500Z",
      "2026-01-01T00:00:02.500Z","2026-01-01T00:00:03.500Z"];
    const metricAt = captured_at => ({captured_at,timestamp:"2026/01/01 00:00:00.000",
      uuid:"GPU-1",pstate:"P0",temperature_gpu_c:"50",utilization_gpu_percent:"1",
      utilization_memory_percent:"1",clock_sm_mhz:"1000",clock_memory_mhz:"5000",
      power_draw_w:"100",power_limit_w:"200",pcie_generation:"4",pcie_width:"16",
      hardware_thermal_slowdown:"Not Active"});
    const emptyProcessAt = captured_at => ({captured_at,gpu_uuid:"",pid:"",process_name:"",
      used_gpu_memory:"",no_processes:true});
    const processAt = (captured_at,pid="456",process_name="s10_sift1m.exe") => ({captured_at,
      nvidia_timestamp:"2026/01/01 00:00:00.000",gpu_uuid:"GPU-1",pid,process_name,
      used_gpu_memory:"100",no_processes:false});
    const nvidiaXml = ({uuid="GPU-1",name="gpu",driver="1",pids=[],duplicate=false}={}) => {
      const gpu = `<gpu><product_name>${name}</product_name><uuid>${uuid}</uuid><processes>` +
        pids.map(pid=>`<process_info><pid>${pid}</pid></process_info>`).join("") +
        `</processes></gpu>`;
      return `<?xml version="1.0"?><nvidia_smi_log><driver_version>${driver}</driver_version>${gpu}` +
        `${duplicate ? gpu : ""}</nvidia_smi_log>\n`;
    };
    const writeJsonLines = (file,values) => fs.writeFileSync(file,values.map(JSON.stringify).join("\n")+"\n");
    const writeFixture = () => {
      fs.mkdirSync(telemetry,{recursive:true});
      const artifacts = artifactNames.map((name,index) => ({name,bytes:index+1,
        sha256:(index+1).toString(16).padStart(64,"0")}));
      const artifact = {schema_version:"S10-ARTIFACT-MANIFEST-v1",artifacts};
      fs.writeFileSync(path.join(capture,"artifact-manifest.json"),JSON.stringify(artifact,null,2)+"\n");
      fs.writeFileSync(path.join(capture,"artifact-post-manifest.json"),JSON.stringify({
        schema_version:"S10-POST-RUN-ARTIFACT-MANIFEST-v1",artifacts},null,2)+"\n");
      fs.writeFileSync(path.join(capture,"compile_commands.json"),JSON.stringify(compileCommands,null,2)+"\n");
      const numerical = {schema_version:"S10-NUMERICAL-BUILD-IDENTITY-v1",build_type:"Release",
        generator:"Ninja",cuda_architecture:"86",compiler_id:"MSVC",compiler_version:"1",
        compiler_path:"cl",cuda_compiler_version:"1",cuda_compiler_path:"nvcc",
        cuda_toolkit_version:"1",build_flags:"strict",source_manifest_sha256:stableHash,
        compile_commands:compileIdentity};
      fs.writeFileSync(path.join(capture,"numerical-build-identity.json"),JSON.stringify(numerical,null,2)+"\n");
      fs.writeFileSync(path.join(capture,"CMakeCache.txt"),[
        "CMAKE_BUILD_TYPE:STRING=Release","CMAKE_CUDA_ARCHITECTURES:STRING=86",
        "CMAKE_CXX_COMPILER:FILEPATH=cl","CMAKE_CUDA_COMPILER:FILEPATH=nvcc",""
      ].join("\n"));
      fs.writeFileSync(path.join(capture,"nvidia-smi-before.xml"),nvidiaXml());
      fs.writeFileSync(path.join(capture,"nvidia-smi-after.xml"),nvidiaXml());
      const snapshot = (when,xml) => ({schema_version:"S10-GPU-SNAPSHOT-v1",captured_at:when,
        xml_sha256:sha256File(path.join(capture,xml)),driver_version:"1",gpu_name:"gpu",
        gpu_uuid:"GPU-1",pstate:"P0",temperature_gpu:"50 C",clock_sm:"1000 MHz",
        clock_memory:"5000 MHz",power_draw:"100 W",power_limit:"200 W",
        hardware_thermal_slowdown:"Not Active",processes:[]});
      fs.writeFileSync(path.join(capture,"gpu-before.json"),JSON.stringify(snapshot(times[0],"nvidia-smi-before.xml")));
      fs.writeFileSync(path.join(capture,"gpu-after.json"),JSON.stringify(snapshot(times[3],"nvidia-smi-after.xml")));
      fs.writeFileSync(path.join(capture,"benchmark-interval.json"),JSON.stringify({
        schema_version:"S10-BENCHMARK-INTERVAL-v1",benchmark_started_at:"2026-01-01T00:00:01.000Z",
        benchmark_ended_at:"2026-01-01T00:00:03.000Z",benchmark_pid:456,benchmark_exit_code:0,
        telemetry_pid:123,telemetry_exit_code:0,telemetry_exited_before_benchmark_end:false,
        telemetry_exited_before_stop_signal:false,process_telemetry_status:"supported",
        maximum_sampling_gap_ms:samplingGapPolicyMs}));
      fs.writeFileSync(path.join(telemetry,"telemetry-errors.log"),"");
      writeJsonLines(path.join(telemetry,"gpu-telemetry.jsonl"),times.map(metricAt));
      writeJsonLines(path.join(telemetry,"gpu-processes.jsonl"),[
        emptyProcessAt(times[0]),processAt(times[1]),processAt(times[2]),emptyProcessAt(times[3])]);
      const artifactManifestHash = sha256File(path.join(capture,"artifact-manifest.json"));
      const numericalHash = sha256File(path.join(capture,"numerical-build-identity.json"));
      const manifest = {revision:"r",source_manifest_sha256:stableHash,config_sha256:stableHash,
        sift_base_sha256:stableHash,sift_query_sha256:stableHash,gpu_name:"gpu",gpu_uuid:"GPU-1",
        cuda_architecture:"86",build_flags:"strict",numerical_build_identity_sha256:numericalHash,
        artifact_manifest_sha256:artifactManifestHash,benchmark_binary_sha256:artifacts[0].sha256};
      const host = {revision:"r",working_tree_state:"clean",operating_system:"os",processor:"cpu",
        compiler_id:"MSVC",compiler_version:"1",compiler_path:"cl",cuda_compiler_path:"nvcc",
        cuda_toolkit_version:"1",driver_version:"1",gpu_name:"gpu",gpu_uuid:"GPU-1",
        build_type:"Release",cuda_architecture:"86",build_flags:"strict",source_manifest_sha256:stableHash,
        config_sha256:stableHash,sift_base_sha256:stableHash,sift_query_sha256:stableHash,
        numerical_build_identity_sha256:numericalHash,artifact_manifest_sha256:artifactManifestHash,
        benchmark_binary_sha256:artifacts[0].sha256};
      fs.writeFileSync(path.join(capture,"host-environment.json"),JSON.stringify(host));
      return manifest;
    };
    let manifest = writeFixture();
    if (analyzeEnvironment(temp,manifest).status !== "ACCEPTED") fail("valid environment self-test failed");
    const environmentRejected = mutation => {
      manifest = writeFixture(); mutation();
      try { analyzeEnvironment(temp,manifest); } catch { return true; } return false;
    };
    const environmentInconclusive = mutation => {
      manifest = writeFixture(); mutation();
      return analyzeEnvironment(temp,manifest).status === "INCONCLUSIVE";
    };
    const replaceMetricRows = mutation => {
      const values = times.map(metricAt); mutation(values);
      writeJsonLines(path.join(telemetry,"gpu-telemetry.jsonl"),values);
    };
    const replaceProcessRows = mutation => {
      const values=[emptyProcessAt(times[0]),processAt(times[1]),processAt(times[2]),emptyProcessAt(times[3])];
      mutation(values); writeJsonLines(path.join(telemetry,"gpu-processes.jsonl"),values);
    };
    const mutateInterval = mutation => {
      const intervalPath=path.join(capture,"benchmark-interval.json");
      const interval=JSON.parse(fs.readFileSync(intervalPath)); mutation(interval);
      fs.writeFileSync(intervalPath,JSON.stringify(interval));
    };
    const replaceAfterXml = value => {
      const xmlPath=path.join(capture,"nvidia-smi-after.xml"); fs.writeFileSync(xmlPath,value);
      const snapshotPath=path.join(capture,"gpu-after.json");
      const snapshot=JSON.parse(fs.readFileSync(snapshotPath)); snapshot.xml_sha256=sha256File(xmlPath);
      fs.writeFileSync(snapshotPath,JSON.stringify(snapshot));
    };
    if (!environmentRejected(() => fs.writeFileSync(path.join(telemetry,"telemetry-errors.log"),"error")) ||
        !environmentRejected(() => replaceProcessRows(values => { values[1]=processAt(times[1],"999"); })) ||
        !environmentRejected(() => replaceProcessRows(values => { values[1]=processAt(times[1],"456","other.exe"); })) ||
        !environmentRejected(() => replaceProcessRows(values => { values[1]=processAt(times[1],"malformed"); })) ||
        !environmentRejected(() => replaceProcessRows(values => {
          values.splice(0,values.length,...times.map(emptyProcessAt)); })) ||
        !environmentRejected(() => replaceProcessRows(values => { values.splice(2,0,{...values[1]}); })) ||
        !environmentRejected(() => replaceMetricRows(values => { values[1].hardware_thermal_slowdown="Active"; })) ||
        !environmentRejected(() => replaceMetricRows(values => { values[1].uuid="GPU-2"; })) ||
        !environmentRejected(() => replaceMetricRows(values => { values[1].power_draw_w="malformed"; })) ||
        !environmentRejected(() => replaceMetricRows(values => { values[1].power_draw_w="300"; })) ||
        !environmentRejected(() => mutateInterval(value => { value.telemetry_exited_before_benchmark_end=true; })) ||
        !environmentRejected(() => mutateInterval(value => { value.telemetry_exited_before_stop_signal=true; })) ||
        !environmentRejected(() => mutateInterval(value => { value.telemetry_exit_code=1; })) ||
        !environmentRejected(() => mutateInterval(value => { value.maximum_sampling_gap_ms=86400000;
          replaceMetricRows(rows => rows.splice(1,2)); replaceProcessRows(rows => rows.splice(1,2)); })) ||
        !environmentRejected(() => mutateInterval(value => { value.maximum_sampling_gap_ms=2499; })) ||
        !environmentRejected(() => mutateInterval(value => { delete value.maximum_sampling_gap_ms; })) ||
        !environmentRejected(() => fs.writeFileSync(path.join(capture,"artifact-manifest.json"),"arbitrary nonempty content")) ||
        !environmentRejected(() => fs.writeFileSync(path.join(capture,"numerical-build-identity.json"),"arbitrary nonempty content")) ||
        !environmentRejected(() => fs.writeFileSync(path.join(capture,"compile_commands.json"),"arbitrary nonempty content")) ||
        !environmentRejected(() => fs.writeFileSync(path.join(capture,"CMakeCache.txt"),"arbitrary nonempty content")) ||
        !environmentRejected(() => fs.writeFileSync(path.join(capture,"gpu-before.json"),"arbitrary nonempty content")) ||
        !environmentRejected(() => replaceAfterXml("corrupted")) ||
        !environmentRejected(() => replaceAfterXml(nvidiaXml({uuid:"GPU-2"}))) ||
        !environmentRejected(() => replaceAfterXml(nvidiaXml({duplicate:true}))) ||
        !environmentRejected(() => { const postPath=path.join(capture,"artifact-post-manifest.json");
          const post=JSON.parse(fs.readFileSync(postPath)); post.artifacts[0].sha256="f".repeat(64);
          fs.writeFileSync(postPath,JSON.stringify(post)); }) ||
        !environmentInconclusive(() => replaceMetricRows(values => values.splice(1))) ||
        !environmentInconclusive(() => replaceMetricRows(values => values.pop())) ||
        !environmentInconclusive(() => replaceMetricRows(values => { values[1].power_draw_w="N/A"; })) ||
        !environmentInconclusive(() => replaceMetricRows(values => {
          values[2].captured_at="2026-01-01T00:00:04.250Z";
          values[3].captured_at="2026-01-01T00:00:05.000Z"; })) ||
        !environmentInconclusive(() => { mutateInterval(value => { value.process_telemetry_status="unsupported"; });
          fs.writeFileSync(path.join(telemetry,"gpu-processes.jsonl"),"");
          fs.writeFileSync(path.join(telemetry,"telemetry-errors.log"),
            "2026-01-01T00:00:01Z nvidia-smi process telemetry query failed\n"); })) {
      fail("environment/telemetry negative self-test failed");
    }
    const acceptanceDirectory = path.join(temp,"acceptance-publication");
    fs.mkdirSync(acceptanceDirectory);
    publishAcceptance(acceptanceDirectory,{experiment_status:"ACCEPTED"});
    let staleRejected=false;
    try { publishAcceptance(acceptanceDirectory,{experiment_status:"ACCEPTED"}); } catch { staleRejected=true; }
    const interruptedDirectory = path.join(temp,"interrupted-publication");
    fs.mkdirSync(interruptedDirectory);
    fs.writeFileSync(path.join(interruptedDirectory,"acceptance.json.tmp-stale"),"interrupted");
    let interruptedRejected=false;
    try { publishAcceptance(interruptedDirectory,{experiment_status:"ACCEPTED"}); } catch { interruptedRejected=true; }
    if (!staleRejected || !interruptedRejected) fail("atomic acceptance negative self-test failed");
  } finally { fs.rmSync(temp,{recursive:true,force:true}); }
}

const args = process.argv.slice(2);
if (args.length === 1 && args[0] === "--self-test") selfTest();
else if (args.length === 2 && args[0] === "--final") analyze(args[1]);
else fail("usage: analyze-s10-sift1m.mjs --self-test | --final DIRECTORY");
