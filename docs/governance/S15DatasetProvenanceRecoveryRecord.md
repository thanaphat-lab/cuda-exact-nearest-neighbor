# S15 Public SIFT1M — Dataset Provenance Recovery Record

**Status:** PROPOSED — pending direct evidence verification and Project Owner acceptance  
**Scope:** Prospective S15 provenance recovery only; do not alter S10–S14 historical evidence.  
**Target repository:** `D:\ku\Parallel Project`  
**Source revision at reported investigation:** `bc7748a4eb973808015c0e14e06f3ec505bb5059`  
**Evidence basis:** Owner-supplied Codex investigation summary in conversation, **not independently revalidated against local transcript or files by this document's author**.

## 1. Purpose and provenance boundary

Record the original acquisition and extraction invocations recovered from the machine-generated Codex execution transcript for the INRIA TexMex public SIFT1M dataset. These invocations are historical observations, **not instructions to re-run**, and their recorded times are local execution observations rather than upstream timestamps.

The executable identity (absolute binary path, version, and binary hash) of `curl.exe` and `tar` was not captured at acquisition time and must be reported as **UNKNOWN**; a current installation cannot retroactively prove which binary was used.

## 2. Primary evidence (requires verification on Owner's machine)

- Transcript: `C:\Users\Zajow\.codex\sessions\2026\11\05\rollout-2026-11-05T19-24-17-01a1abaa-c15d-7331-a991-b95dc6dab965.jsonl`
- Transcript SHA-256 (reported): `4bf27af5bbb5bc47233bc7e1042ece9b711e82d00111f3036d89bcdcce2851b9`
- Acquisition invocation: line 136; completion: line 165
- Archive inspection and SHA-256: lines 168–170
- Extraction invocation: line 213; completion: lines 218–220
- Archive member comparison: lines 226–228

The reported SHA-256 and line references must be verified directly before this record is treated as accepted evidence.

## 3. Acquisition

**Source URL:** `ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz`  
**Method / tool:** FTP, `curl.exe` invoked from PowerShell  
**Observed invocation time (UTC):** `2026-11-05T12:31:31.930Z`  
**Observed successful completion time (UTC):** `2026-11-05T12:33:55.215Z`  
**Proposed `DatasetAcquiredAt` semantics:** the observed successful-completion time above; **not** server-issued acquisition time.  
**Recorded exit code:** `0`  
**FTP response:** `226`  
**Downloaded archive bytes:** `168280445`  
**Archive SHA-256:** `92f1270c5e3a0cb46b89983e72b0511e4df065c31a9fa0276d8c9b1fca5bc81a`  
**Upstream/local MD5, reported equal:** `b23d1b3b2ee8469d819b61ca900ef0ed`

Exact recovered PowerShell command (historical record; **do not execute**):

```powershell
$target = 'D:\datasets\sift1m'; $partial = Join-Path $target 'sift.tar.gz.part'; $archive = Join-Path $target 'sift.tar.gz'; if (Test-Path -LiteralPath $target) { throw "Refusing to reuse existing dataset directory: $target" }; New-Item -ItemType Directory -Path $target -Force | Out-Null; curl.exe --fail --show-error --location --ftp-pasv --remove-on-error --remote-time --output $partial --write-out "FTP_RESPONSE=%{response_code}`nREMOTE_IP=%{remote_ip}`nSIZE_DOWNLOAD=%{size_download}`nTIME_TOTAL=%{time_total}`n" 'ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz'; if ($LASTEXITCODE -ne 0) { throw "FTP acquisition failed with curl exit code $LASTEXITCODE" }; Move-Item -LiteralPath $partial -Destination $archive; Get-Item -LiteralPath $archive | Select-Object FullName,Length,LastWriteTimeUtc
```

## 4. Extraction

**Tool:** `tar` invoked from PowerShell  
**Observed invocation time (UTC):** `2026-11-05T12:46:57.913Z`  
**Observed successful completion time (UTC):** `2026-11-05T12:48:41.704Z`  
**Extracted-file creation time (UTC, reported):** `2026-11-05T12:48:39Z`  
**Recorded exit code:** `0`  
**Reported verification:** all four extracted SHA-256 values matched their corresponding archive members (`MATCH_EXTRACTED=True`).

Exact recovered PowerShell command (historical record; **do not execute**):

```powershell
$target='D:\datasets\sift1m'; $archive=Join-Path $target 'sift.tar.gz'; $members=@('sift_base.fvecs','sift_query.fvecs','sift_learn.fvecs','sift_groundtruth.ivecs'); foreach($name in $members){ if(Test-Path -LiteralPath (Join-Path $target $name)){ throw "Refusing to overwrite existing file: $name" } }; tar -xzf $archive -C $target --strip-components=1; if($LASTEXITCODE -ne 0){ throw "tar extraction failed with exit code $LASTEXITCODE" }; Get-ChildItem -LiteralPath $target -File | Sort-Object Name | Select-Object Name,Length,LastWriteTimeUtc
```

## 5. Required S15 dataset identities

| Dataset file | Bytes | SHA-256 |
|---|---:|---|
| `sift_base.fvecs` | `516000000` | `21f66e2975057b5728ba56de1c825bac4f4d89d596609ae985741c6242631816` |
| `sift_query.fvecs` | `5160000` | `f7fc9be140accdfd64116c2fa2365ecdb69b8f084970c6b0532db5ff79ac8fdc` |

These identities were reported as matched during the clean S15 verification. Other supplemental extracted-file hashes are reportedly in the existing environment capture, not reproduced here without their exact values.

## 6. Corroborating evidence (reported)

- Archive NTFS creation timestamp `2026-11-05T12:31:47.1503303Z`, during the acquisition window.
- Extracted-file NTFS creation timestamp `2026-11-05T12:48:39Z`, during the extraction window.
- Sandbox audit output reportedly includes post-acquisition archive hashes and post-extraction comparisons, but the privileged invocations are in the transcript.
- `docs/experiments/S10-SIFT1M-Exploratory/ExploratoryResultsReport.md` supports source URL/extracted hashes.
- Approved S15 design and Owner approval record establish required dataset identities and provenance fields.

## 7. Known unknowns

| Field | Recorded value | Treatment |
|---|---|---|
| `curl.exe` absolute executable path | UNKNOWN | Do not infer from current `Get-Command` |
| `curl.exe` version and binary hash | UNKNOWN | Historical version unavailable |
| `tar` absolute executable path | UNKNOWN | Do not infer from current `Get-Command` |
| `tar` implementation, version and binary hash | UNKNOWN | Historical implementation unavailable |
| Server-issued acquisition timestamp | UNKNOWN | Observed process completion is not upstream issuance |

## 8. Acceptance and next gate

This record is a **proposed prospective recovery**. Before accepting it for S15:

1. Verify the transcript exists and its SHA-256 matches the reported identity, then verify the command and result lines match verbatim.
2. Confirm archive and extracted dataset identities against the approved S15 configuration using locally stored evidence.
3. Review `scripts/run-s15-public-sift1m.ps1` for its actual dataset provenance input schema; create or update the required structured provenance input **without weakening validation**.
4. Review and approve the final source-linked provenance entry; preserve `UNKNOWN` fields explicitly.
5. Commit any approved S15-only documentation/metadata changes, then perform the required clean-revision integrity checks.
6. Obtain **separate explicit Project Owner authorization** before any formal performance benchmark.

**Approval state:** Not asserted by this file.  
**Formal performance benchmark:** Not authorized by this record.
