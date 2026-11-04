param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('Tune','Final')]
    [string]$Mode,
    [Parameter(Mandatory=$true)]
    [string]$BuildDirectory,
    [Parameter(Mandatory=$true)]
    [string]$OutputDirectory,
    [Parameter(Mandatory=$true)]
    [string]$ExpectedRevision,
    [Parameter(Mandatory=$true)]
    [string]$S7BlockFreeze,
    [string]$TransposeFreeze,
    [string]$CorrectnessReference
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = (Resolve-Path $BuildDirectory).Path
$config = Join-Path $root 'benchmarks/config/s8-transpose-ab-20261104-01.json'
$configObject = Get-Content -Raw -LiteralPath $config | ConvertFrom-Json
$benchmark = Join-Path $build 's8_benchmark.exe'
$analyzer = Join-Path $root 'scripts/analyze-s8-benchmarks.mjs'
if (-not (Test-Path -LiteralPath $benchmark)) { throw 's8_benchmark.exe is missing' }
if (-not (Test-Path -LiteralPath $S7BlockFreeze)) { throw 'S7 block freeze is missing' }
if ($Mode -eq 'Final') {
    if (-not (Test-Path -LiteralPath $TransposeFreeze)) { throw 'transpose freeze is required' }
    if (-not (Test-Path -LiteralPath $CorrectnessReference)) {
        throw 'matching correctness evidence is required'
    }
}
$actualRevision = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualRevision -ne $ExpectedRevision) {
    throw 'repository revision does not match ExpectedRevision'
}
$treeState = & git -C $root status --porcelain=v1 --untracked-files=all
if ($LASTEXITCODE -ne 0 -or $treeState) { throw 'S8 measurements require a clean worktree' }
$configHash = (Get-FileHash -LiteralPath $config -Algorithm SHA256).Hash.ToLowerInvariant()
$correctnessGate = $null
if ($Mode -eq 'Final') {
    $correctnessGate = Get-Content -Raw -LiteralPath $CorrectnessReference | ConvertFrom-Json
    if ($correctnessGate.status -ne 'passed-clean-candidate' -or
        $correctnessGate.revision -ne $ExpectedRevision -or
        $correctnessGate.working_tree_state -ne 'clean' -or
        $correctnessGate.experiment_config_sha256 -ne $configHash) {
        throw 'correctness gate identity/status does not qualify the final run'
    }
}
$timestamp = [DateTimeOffset]::Now.ToString('o')
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$environmentDirectory = Join-Path $OutputDirectory 'environment-capture'
New-Item -ItemType Directory -Force -Path $environmentDirectory | Out-Null
[ordered]@{
    schema_version = 'S8-HOST-ENVIRONMENT-v1'
    captured_at = $timestamp
    revision = $actualRevision
    working_tree_state = 'clean'
    operating_system = [Environment]::OSVersion.VersionString
    processor = [Environment]::GetEnvironmentVariable('PROCESSOR_IDENTIFIER')
    machine_name = [Environment]::MachineName
    powershell = $PSVersionTable.PSVersion.ToString()
    config_sha256 = $configHash
} | ConvertTo-Json | Set-Content -LiteralPath `
    (Join-Path $environmentDirectory 'host-environment.json') -Encoding utf8
$MyInvocation.Line | Set-Content -LiteralPath `
    (Join-Path $environmentDirectory 'orchestration-command.txt') -Encoding utf8
$nvidiaSmi = Get-Command nvidia-smi -ErrorAction Stop
$existingCompute = @(& $nvidiaSmi.Source `
    '--query-compute-apps=process_name' '--format=csv,noheader,nounits' |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
if ($existingCompute.Count -ne 0) {
    throw "unauthorized GPU compute process present before measurement: $($existingCompute -join ', ')"
}
$arguments = @(
    $(if ($Mode -eq 'Tune') { '--tune' } else { '--run' }),
    $OutputDirectory,
    $S7BlockFreeze,
    '--experiment-id', 'S8-TRANSPOSE-AB-20261104-01',
    '--experiment-config', $config,
    '--config-sha256', $configHash,
    '--expected-revision', $ExpectedRevision,
    '--tuning-seed', '11820040416388919760',
    '--final-seed', '589684135938649225',
    '--captured-at', $timestamp,
    '--timezone', 'SE-Asia-Standard-Time',
    '--cuda-architecture', 'owner-approved-build',
    '--build-flags', 'Release-fast-math-disabled'
)
if ($Mode -eq 'Final') { $arguments += @('--correctness-reference', $CorrectnessReference) }
$telemetryDirectory = Join-Path $environmentDirectory 'telemetry'
$telemetryStop = Join-Path $environmentDirectory 'stop-telemetry.signal'
$telemetryScript = Join-Path $PSScriptRoot 'capture-s7-telemetry.ps1'
$pwsh = (Get-Process -Id $PID).Path
$quote = { param([string]$Value) '"' + $Value.Replace('"', '\"') + '"' }
$telemetryArgs = '-NoProfile -File {0} -NvidiaSmi {1} -OutputDirectory {2} ' +
    '-StopSignal {3} -IntervalMilliseconds {4}'
$telemetryArgs = $telemetryArgs -f (& $quote $telemetryScript),
    (& $quote $nvidiaSmi.Source), (& $quote $telemetryDirectory),
    (& $quote $telemetryStop),
    [string]$configObject.environment_policy.telemetry_interval_ms
$telemetryProcess = Start-Process -FilePath $pwsh -ArgumentList $telemetryArgs `
    -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds ([int]$configObject.environment_policy.telemetry_interval_ms + 250)
if ($telemetryProcess.HasExited) { throw 'telemetry helper failed during startup' }
try {
    & $benchmark @arguments
    if ($LASTEXITCODE -ne 0) { throw 'S8 benchmark execution failed' }
    if ($Mode -eq 'Tune') {
        & node $analyzer --tuning $OutputDirectory
    } else {
        & node $analyzer --final $OutputDirectory --freeze $TransposeFreeze
    }
    if ($LASTEXITCODE -ne 0) { throw 'S8 analysis failed' }
} finally {
    New-Item -ItemType File -Force -Path $telemetryStop | Out-Null
    if (-not $telemetryProcess.WaitForExit(15000)) {
        Stop-Process -Id $telemetryProcess.Id -Force
        $telemetryProcess.WaitForExit()
    }
}
$telemetryErrors = @(Get-Content -LiteralPath `
    (Join-Path $telemetryDirectory 'telemetry-errors.log') | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_)
    })
if ($telemetryErrors.Count -ne 0) { throw 'telemetry capture reported errors' }
