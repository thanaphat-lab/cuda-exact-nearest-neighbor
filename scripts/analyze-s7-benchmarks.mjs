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

function main() {
  const root = process.argv[2];
  if (!root) fail("usage: node scripts/analyze-s7-benchmarks.mjs RESULT_DIR");
  const manifestPath = path.join(root, "manifest.json");
  const rawPath = path.join(root, "raw_samples.csv");
  const summaryPath = path.join(root, "summary.csv");
  for (const file of [manifestPath, rawPath, summaryPath, path.join(root, "tuning-freeze.json")]) {
    if (!fs.existsSync(file)) fail(`missing required artifact: ${file}`);
  }
  const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
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

  const hashes = {};
  for (const file of ["manifest.json", "environment.json", "raw_samples.csv", "summary.csv", "summary.json", "tuning-freeze.json", "tuning-freeze.csv", "comparisons.csv", "comparisons.json", "commands.txt"]) {
    const bytes = fs.readFileSync(path.join(root, file));
    hashes[file] = crypto.createHash("sha256").update(bytes).digest("hex");
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
