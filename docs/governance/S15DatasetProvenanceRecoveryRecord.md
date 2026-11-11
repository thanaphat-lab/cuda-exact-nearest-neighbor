# S15 Public SIFT1M — Dataset Provenance Recovery Record

**Status:** ACCEPTED — direct evidence verified and accepted by the Project Owner\
**Acceptance date:** 2026-11-12 (Asia/Bangkok)\
**Accepting role:** Project Owner\
**Scope:** Prospective S15 provenance recovery only; do not alter S10–S14 historical evidence.  
**Target repository:** `D:\ku\Parallel Project`  
**Source revision at reported investigation:** `bc7748a4eb973808015c0e14e06f3ec505bb5059`  
**Evidence basis:** Direct verification of the identified Codex execution transcript, its SHA-256 and cited records, plus corroboration against the retained external dataset files; Project Owner acceptance was explicitly supplied on 2026-11-12 (Asia/Bangkok).

## 1. Purpose and provenance boundary

Record the original acquisition and extraction invocations recovered from the machine-generated Codex execution transcript for the INRIA TexMex public SIFT1M dataset. These invocations are historical observations, **not instructions to re-run**, and their recorded times are local execution observations rather than upstream timestamps.

The executable identity (absolute binary path, version, and binary hash) of `curl.exe` and `tar` was not captured at acquisition time and must be reported as **UNKNOWN**; a current installation cannot retroactively prove which binary was used.

## 2. Verified primary evidence

- Transcript: `C:\Users\Zajow\.codex\sessions\2026\11\05\rollout-2026-11-05T19-24-17-01a1abaa-c15d-7331-a991-b95dc6dab965.jsonl`
- Transcript SHA-256 (verified): `4bf27af5bbb5bc47233bc7e1042ece9b711e82d00111f3036d89bcdcce2851b9`
- Acquisition invocation: line 136; completion: line 165
- Archive inspection and SHA-256: lines 168–170
- Extraction invocation: line 213; completion: lines 218–220
- Archive member comparison: lines 226–228

The transcript exists at the recorded path. Its SHA-256 and the cited acquisition, archive-inspection, extraction and archive-member-comparison records were verified directly before Project Owner acceptance.

## 3. Acquisition

**Source URL:** `ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz`  
**Method / tool:** FTP, `curl.exe` invoked from PowerShell  
**Observed invocation time (UTC):** `2026-11-05T12:31:31.930Z`  
**Observed successful completion time (UTC):** `2026-11-05T12:33:55.215Z`  
**Accepted `DatasetAcquiredAt` semantics:** the observed successful-completion time above; **not** server-issued acquisition time.\
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
**Extracted-file creation time (UTC, verified):** `2026-11-05T12:48:39Z`\
**Recorded exit code:** `0`  
**Verified result:** all four extracted SHA-256 values matched their corresponding archive members (`MATCH_EXTRACTED=True`).

Exact recovered PowerShell command (historical record; **do not execute**):

```powershell
$target='D:\datasets\sift1m'; $archive=Join-Path $target 'sift.tar.gz'; $members=@('sift_base.fvecs','sift_query.fvecs','sift_learn.fvecs','sift_groundtruth.ivecs'); foreach($name in $members){ if(Test-Path -LiteralPath (Join-Path $target $name)){ throw "Refusing to overwrite existing file: $name" } }; tar -xzf $archive -C $target --strip-components=1; if($LASTEXITCODE -ne 0){ throw "tar extraction failed with exit code $LASTEXITCODE" }; Get-ChildItem -LiteralPath $target -File | Sort-Object Name | Select-Object Name,Length,LastWriteTimeUtc
```

## 5. Required S15 dataset identities

| Dataset file | Bytes | SHA-256 |
|---|---:|---|
| `sift_base.fvecs` | `516000000` | `21f66e2975057b5728ba56de1c825bac4f4d89d596609ae985741c6242631816` |
| `sift_query.fvecs` | `5160000` | `f7fc9be140accdfd64116c2fa2365ecdb69b8f084970c6b0532db5ff79ac8fdc` |

These identities matched the transcript, the retained external dataset files and the approved S15 configuration. Supplemental extracted-file hashes remain in the existing environment capture and transcript and are not duplicated here.

## 6. Verified corroborating evidence

- Archive NTFS creation timestamp `2026-11-05T12:31:47.1503303Z`, during the acquisition window.
- Extracted-file NTFS creation timestamp `2026-11-05T12:48:39Z`, during the extraction window.
- Transcript output includes the post-acquisition archive hash and post-extraction archive-member comparisons; the privileged invocations are preserved in the cited transcript records.
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

## 8. Accepted semantics and next gate

The Project Owner accepted this prospective S15 recovery on 2026-11-12 (Asia/Bangkok) with the following confirmed semantics:

1. `DatasetAcquiredAt` is `2026-11-05T12:33:55.215Z`, the observed successful completion time of the recovered acquisition invocation. It is not a server-issued timestamp.
2. The source URL is `ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz`; acquisition used the recovered PowerShell invocation of `curl.exe` over FTP.
3. Extraction used the recovered PowerShell invocation of `tar`; all four extracted SHA-256 values matched their archive members.
4. Historical executable paths, versions and binary hashes for `curl.exe` and `tar`, and the server-issued acquisition timestamp, remain explicitly `UNKNOWN`.
5. This acceptance is prospective for S15 only. It does not rewrite or retrospectively strengthen S10–S14 evidence.

The next S15 gate is to supply these accepted semantics through the required provenance parameters of `scripts/run-s15-public-sift1m.ps1`, then perform fresh clean-revision integrity, correctness and bounded non-performance preflight verification. Those checks remain runtime evidence and are not passed by this documentation acceptance.

**Approval state:** Accepted by the Project Owner on 2026-11-12 (Asia/Bangkok).\
**Formal performance benchmark:** Not authorized by this record; separate explicit Project Owner authorization remains required.
