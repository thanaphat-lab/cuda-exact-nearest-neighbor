param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('SelfTest', 'Correctness', 'Diagnostic', 'Formal')]
    [string]$Mode,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$DatasetRoot,
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedRevision,
    [string]$ExperimentId,
    [string]$ExperimentPolicyApproval,
    [string]$CorrectnessGate,
    [string]$TuningFreeze,
    [string]$DiagnosticSummary,
    [switch]$GpuExecutionAuthorization,
    [switch]$DiagnosticExecutionAuthorization,
    [switch]$FormalBenchmarkAuthorization
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$configPath = Join-Path $root 'benchmarks/config/v5-batch-optimization.json'
$config = Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json
$node = (Get-Command node.exe -ErrorAction Stop).Source
$proposedPolicyHash = (& $node -e `
    "const fs=require('fs'),c=JSON.parse(fs.readFileSync(process.argv[1],'utf8')),h=require('crypto').createHash('sha256').update(JSON.stringify(c.proposed_policy)).digest('hex');process.stdout.write(h)" `
    $configPath).Trim()
if ($LASTEXITCODE -ne 0 -or $proposedPolicyHash -notmatch '^[0-9a-f]{64}$') {
    throw 'Could not compute the V5 proposed-policy identity.'
}

function Require-V5([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Assert-V5FreshOutput([string]$Path) {
    Require-V5 (-not (Test-Path -LiteralPath $Path)) `
        'V5 output directory must not already exist; historical evidence is immutable.'
}
function Assert-V5Config($Value) {
    Require-V5 ($Value.schema_version -eq 'V5-BATCH-OPTIMIZATION-CONFIG-v1-PROPOSED' -and
        $Value.experiment_policy_status -eq 'PROPOSED / NOT OWNER-APPROVED' -and
        ($Value.workload.candidate_counts -join ',') -eq '1024,16384,262144,1000000' -and
        ($Value.workload.batch_sizes -join ',') -eq '8,32,128' -and
        [int]$Value.workload.dimension -eq 128 -and
        [int]$Value.workload.query_buffer_count -eq 128 -and
        ($Value.candidate_space.query_tiles -join ',') -eq '1,4,8' -and
        ($Value.candidate_space.block_sizes -join ',') -eq '128,256,512' -and
        [int]$Value.sampling.warmups -eq 10 -and
        [int]$Value.sampling.measured_repetitions -eq 100 -and
        [int]$Value.sampling.bootstrap_resamples -eq 2000 -and
        $Value.sampling.percentile_method -eq 'linear_n_minus_1' -and
        [int]$Value.environment_policy.telemetry_query_timeout_ms -eq 900 -and
        [int]$Value.environment_policy.telemetry_interval_ms -eq 1000 -and
        [int]$Value.environment_policy.maximum_accepted_start_gap_ms -eq 3000 -and
        $Value.fast_math -eq 'disabled') 'V5 configuration contract mismatch.'
}
function Get-V5PolicyApproval([string]$Path, [string]$ConfigHash) {
    Require-V5 (-not [string]::IsNullOrWhiteSpace($Path) -and
        (Test-Path -LiteralPath $Path -PathType Leaf)) `
        'Diagnostic/Formal execution requires an Owner policy-approval file.'
    $approval = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    Require-V5 ($approval.schema_version -eq 'V5-EXPERIMENT-POLICY-APPROVAL-v1' -and
        $approval.status -eq 'OWNER-APPROVED' -and
        $approval.approved_by_role -eq 'Project Owner' -and
        $approval.experiment_config_sha256 -eq $ConfigHash -and
        $approval.approved_proposed_policy_sha256 -eq $script:proposedPolicyHash) `
        'Owner policy approval does not bind the exact V5 proposed config/policy.'
    return $approval
}
function Assert-V5CompileCommand([string]$Command, [string]$Label) {
    foreach ($flag in @('--ftz=false','--fmad=true','--prec-div=true',
            '--prec-sqrt=true','-Xcompiler=/fp:strict')) {
        Require-V5 ($Command.Contains($flag)) "required numerical flag $flag is missing for $Label"
    }
    foreach ($forbidden in @('--use_fast_math','--ftz=true','--fmad=false',
            '--prec-div=false','--prec-sqrt=false')) {
        Require-V5 (-not $Command.Contains($forbidden)) "forbidden numerical flag $forbidden appears for $Label"
    }
}
function Wait-V5File([string]$Path, [int]$TimeoutMilliseconds) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.ElapsedMilliseconds -lt $TimeoutMilliseconds) {
        if (Test-Path -LiteralPath $Path -PathType Leaf) { return }
        Start-Sleep -Milliseconds 50
    }
    throw "Timed out waiting for $Path"
}
function Invoke-V5Native([string]$FilePath, [string[]]$Arguments) {
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$FilePath failed with exit code $LASTEXITCODE" }
}
function Start-V5Process([string]$FilePath, [string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FilePath
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    foreach ($argument in $Arguments) { [void]$info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    $started = $process.Start()
    Require-V5 $started "failed to start $FilePath"
    return $process
}

Assert-V5Config $config
$configHash = (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash.ToLowerInvariant()

if ($Mode -eq 'SelfTest') {
    Require-V5 (-not $GpuExecutionAuthorization -and
        -not $DiagnosticExecutionAuthorization -and
        -not $FormalBenchmarkAuthorization) `
        'Execution authorization switches are invalid in SelfTest mode.'
    $ordinals = for ($k = 0; $k -lt 128; ++$k) { [math]::Floor($k * 9999 / 127) }
    Require-V5 ($ordinals.Count -eq 128 -and $ordinals[0] -eq 0 -and
        $ordinals[127] -eq 9999 -and (@($ordinals | Sort-Object -Unique)).Count -eq 128) `
        'V5 query-selection self-test failed.'
    $rejected = $false
    try { Get-V5PolicyApproval (Join-Path $root `
        'benchmarks/config/v5-experiment-policy-approval.template.json') $configHash | Out-Null }
    catch { $rejected = $true }
    Require-V5 $rejected 'Inert V5 policy-approval template was accepted.'
    Write-Output 'V5 orchestration self-test: passed'
    return
}

foreach ($value in @($BuildDirectory,$OutputDirectory,$DatasetRoot,$ExpectedRevision,$ExperimentId)) {
    Require-V5 (-not [string]::IsNullOrWhiteSpace($value)) `
        'V5 execution requires build/output/dataset/revision/experiment identity.'
}
Require-V5 $GpuExecutionAuthorization `
    'V5 correctness/diagnostic/formal execution requires explicit -GpuExecutionAuthorization.'
if ($Mode -eq 'Diagnostic') {
    Require-V5 ($DiagnosticExecutionAuthorization -and -not $FormalBenchmarkAuthorization) `
        'Diagnostic mode requires -DiagnosticExecutionAuthorization only.'
} elseif ($Mode -eq 'Formal') {
    Require-V5 ($FormalBenchmarkAuthorization -and -not $DiagnosticExecutionAuthorization) `
        'Formal mode requires -FormalBenchmarkAuthorization only.'
} else {
    Require-V5 (-not $DiagnosticExecutionAuthorization -and -not $FormalBenchmarkAuthorization) `
        'Correctness mode does not accept diagnostic/formal authorization.'
}

$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$dataset = (Resolve-Path -LiteralPath $DatasetRoot).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
Assert-V5FreshOutput $output
$relativeDataset = [IO.Path]::GetRelativePath($root,$dataset)
Require-V5 ($relativeDataset -ne '.' -and
    ($relativeDataset.StartsWith('..' + [IO.Path]::DirectorySeparatorChar) -or
     [IO.Path]::IsPathRooted($relativeDataset))) `
    'V5 DatasetRoot must remain outside the repository.'
$relativeOutput = [IO.Path]::GetRelativePath($root,$output)
if ($relativeOutput -eq '.' -or
        ((-not $relativeOutput.StartsWith('..' + [IO.Path]::DirectorySeparatorChar)) -and
         (-not [IO.Path]::IsPathRooted($relativeOutput)))) {
    & git -C $root check-ignore --quiet -- $relativeOutput
    Require-V5 ($LASTEXITCODE -eq 0) `
        'Repository-contained V5 output must be ignored.'
}
$head = (& git -C $root rev-parse HEAD).Trim()
Require-V5 ($LASTEXITCODE -eq 0 -and $head -eq $ExpectedRevision.ToLowerInvariant()) `
    'ExpectedRevision does not match HEAD.'
$tree = @(& git -C $root status --porcelain=v1 --untracked-files=all)
Require-V5 ($LASTEXITCODE -eq 0 -and $tree.Count -eq 0) `
    'V5 evidence requires a clean worktree.'

$binary = Join-Path $build 'v5_batch_benchmark.exe'
$analyzer = Join-Path $root 'scripts/analyze-v5-batch-benchmarks.mjs'
$collector = Join-Path $root 'scripts/capture-s14-telemetry.ps1'
$compileCommands = Join-Path $build 'compile_commands.json'
$cpuManifest = Join-Path $build 'evidence/v5_batch_benchmark.manifest.txt'
$fixtureEvidence = Join-Path $build 'evidence/s6_cpu_fixtures.status.txt'
foreach ($required in @($binary,$analyzer,$collector,$compileCommands,$cpuManifest,$fixtureEvidence,
        (Join-Path $dataset 'sift_base.fvecs'),(Join-Path $dataset 'sift_query.fvecs'))) {
    Require-V5 (Test-Path -LiteralPath $required -PathType Leaf) "required V5 input missing: $required"
}
$compile = Get-Content -Raw -LiteralPath $compileCommands | ConvertFrom-Json
$v5Compile = $compile | Where-Object { $_.file.Replace('\','/').EndsWith('src/cuda_v5.cu') } |
    Select-Object -First 1
$benchmarkCompile = $compile | Where-Object { $_.file.Replace('\','/').EndsWith('benchmarks/v5_batch_benchmark.cpp') } |
    Select-Object -First 1
Require-V5 ($null -ne $v5Compile -and $null -ne $benchmarkCompile) `
    'V5 compile-command identity is incomplete.'
Assert-V5CompileCommand ([string]$v5Compile.command) 'src/cuda_v5.cu'
Require-V5 ([string]$benchmarkCompile.command -match '/fp:strict' -and
    [string]$benchmarkCompile.command -notmatch '/fp:fast') `
    'V5 benchmark host compile policy is not strict.'
$buildFlags = ([string]$v5Compile.command + ' || ' + [string]$benchmarkCompile.command)
$cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt')
$cudaArchitecture = (($cache | Where-Object { $_ -match '^CMAKE_CUDA_ARCHITECTURES:' }) -split '=',2)[1]
Require-V5 (-not [string]::IsNullOrWhiteSpace($cudaArchitecture)) `
    'V5 build requires an explicit CUDA architecture.'

$policyHash = ''
if ($Mode -in @('Diagnostic','Formal')) {
    Get-V5PolicyApproval $ExperimentPolicyApproval $configHash | Out-Null
    $policyHash = (Get-FileHash -LiteralPath $ExperimentPolicyApproval -Algorithm SHA256).Hash.ToLowerInvariant()
}
$correctnessHash = ''
if ($Mode -in @('Diagnostic','Formal')) {
    Require-V5 (-not [string]::IsNullOrWhiteSpace($CorrectnessGate) -and
        (Test-Path -LiteralPath $CorrectnessGate -PathType Leaf)) `
        'Diagnostic/Formal mode requires a V5 correctness gate.'
    $gate = Get-Content -Raw -LiteralPath $CorrectnessGate | ConvertFrom-Json
    Require-V5 ($gate.schema_version -eq 'V5-CORRECTNESS-GATE-v1' -and
        $gate.status -eq 'passed-clean-candidate' -and $gate.performance_authorized -eq $true -and
        $gate.revision -eq $head -and $gate.experiment_config_sha256 -eq $configHash) `
        'V5 correctness gate status/revision/config identity mismatch.'
    $correctnessHash = (Get-FileHash -LiteralPath $CorrectnessGate -Algorithm SHA256).Hash.ToLowerInvariant()
}
if ($Mode -eq 'Formal') {
    Require-V5 (-not [string]::IsNullOrWhiteSpace($TuningFreeze) -and
        (Test-Path -LiteralPath $TuningFreeze -PathType Leaf) -and
        -not [string]::IsNullOrWhiteSpace($DiagnosticSummary) -and
        (Test-Path -LiteralPath $DiagnosticSummary -PathType Leaf)) `
        'Formal V5 mode requires the frozen CSV and analyzer diagnostic summary.'
    $diagnostic = Get-Content -Raw -LiteralPath $DiagnosticSummary | ConvertFrom-Json
    $freezeRows = @(Import-Csv -LiteralPath $TuningFreeze)
    Require-V5 ($diagnostic.schema_version -eq 'V5-DIAGNOSTIC-SUMMARY-v1' -and
        $diagnostic.revision -eq $head -and
        $diagnostic.experiment_config_sha256 -eq $configHash -and
        $diagnostic.policy_approval_sha256 -eq $policyHash -and
        $diagnostic.correctness_gate_sha256 -eq $correctnessHash -and
        @($diagnostic.selected).Count -eq 12 -and $freezeRows.Count -eq 12) `
        'Formal V5 diagnostic selection identity/completeness mismatch.'
    foreach ($selected in @($diagnostic.selected)) {
        $matches = @($freezeRows | Where-Object {
            [uint64]$_.N -eq [uint64]$selected.N -and
            [uint64]$_.B -eq [uint64]$selected.B -and
            [uint32]$_.query_tile -eq [uint32]$selected.query_tile -and
            [uint32]$_.block_size -eq [uint32]$selected.block_size -and
            [double]$_.diagnostic_median_ns -eq [double]$selected.diagnostic_median_ns
        })
        Require-V5 ($matches.Count -eq 1) `
            'Formal V5 freeze differs from an analyzer-selected diagnostic winner.'
    }
}

New-Item -ItemType Directory -Path $output | Out-Null
Copy-Item -LiteralPath $configPath -Destination (Join-Path $output 'experiment-config.json')
if ($Mode -in @('Diagnostic','Formal')) {
    Copy-Item -LiteralPath $ExperimentPolicyApproval -Destination (Join-Path $output 'policy-approval.json')
    Copy-Item -LiteralPath $CorrectnessGate -Destination (Join-Path $output 'correctness-gate.json')
}

$common = @('--experiment-id',$ExperimentId,'--experiment-config',$configPath,
    '--config-sha256',$configHash,'--expected-revision',$head,
    '--policy-approval-sha256',$policyHash,'--correctness-gate-sha256',$correctnessHash,
    '--cpu-manifest',$cpuManifest,'--fixture-evidence',$fixtureEvidence,
    '--captured-at',[DateTimeOffset]::Now.ToString('o'),'--timezone','Asia/Bangkok',
    '--cuda-architecture',$cudaArchitecture,'--build-flags',$buildFlags)
if ($Mode -eq 'Correctness') {
    Invoke-V5Native $binary (@('--correctness',$output,$dataset) + $common)
    Write-Output "V5 correctness artifacts written to $output"
    return
}

$nvidiaSmi = (Get-Command nvidia-smi.exe -ErrorAction Stop).Source
$gpuRows = @(& $nvidiaSmi '--query-gpu=name,uuid,driver_version,memory.total' '--format=csv,noheader,nounits')
Require-V5 ($LASTEXITCODE -eq 0 -and $gpuRows.Count -eq 1) `
    'V5 environment gate requires exactly one queryable GPU.'
$gpuParts = $gpuRows[0] -split ',\s*',4
Require-V5 ($gpuParts.Count -eq 4) 'V5 GPU identity query was malformed.'
$processRows = @(& $nvidiaSmi '--query-compute-apps=pid,process_name' '--format=csv,noheader,nounits' |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
Require-V5 ($LASTEXITCODE -eq 0 -and $processRows.Count -eq 0) `
    'Unauthorized compute process exists before V5 measurement.'
[ordered]@{
    schema_version = 'V5-ENVIRONMENT-v1'; status = 'accepted'; revision = $head
    experiment_config_sha256 = $configHash; fast_math = 'disabled'
    cuda_architecture = $cudaArchitecture; build_flags = $buildFlags
    gpu_name = $gpuParts[0]; gpu_uuid = $gpuParts[1]
    driver_version = $gpuParts[2]; memory_total_mib = $gpuParts[3]
    pre_measurement_compute_process_count = 0
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'environment.json') -Encoding utf8

$telemetry = Join-Path $output 'telemetry'
$stopSignal = Join-Path $output 'stop-telemetry.signal'
$pwsh = (Get-Process -Id $PID).Path
$collectorArguments = @('-NoProfile','-File',$collector,'-Mode','Capture',
    '-NvidiaSmi',$nvidiaSmi,'-OutputDirectory',$telemetry,'-StopSignal',$stopSignal,
    '-IntervalMilliseconds','1000','-QueryTimeoutMilliseconds','900')
$collectorProcess = Start-V5Process $pwsh $collectorArguments
try {
    Wait-V5File (Join-Path $telemetry 'telemetry-ready.json') 10000
    $measurementStartedAt = [DateTimeOffset]::Now
    if ($Mode -eq 'Diagnostic') {
        Invoke-V5Native $binary (@('--diagnostic',$output,$dataset) + $common +
            @('--correctness-gate',$CorrectnessGate))
    } else {
        $freezeHash = (Get-FileHash -LiteralPath $TuningFreeze -Algorithm SHA256).Hash.ToLowerInvariant()
        Copy-Item -LiteralPath $TuningFreeze -Destination (Join-Path $output 'tuning-freeze.csv')
        Copy-Item -LiteralPath $DiagnosticSummary -Destination `
            (Join-Path $output 'diagnostic-summary.json')
        Invoke-V5Native $binary (@('--formal',$output,$dataset,$TuningFreeze,$freezeHash) +
            $common + @('--correctness-gate',$CorrectnessGate))
    }
    $measurementEndedAt = [DateTimeOffset]::Now
    [ordered]@{
        schema_version = 'V5-BENCHMARK-INTERVAL-v1'
        phase = $Mode.ToLowerInvariant()
        started_at = $measurementStartedAt.ToString('o')
        ended_at = $measurementEndedAt.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath `
        (Join-Path $output 'benchmark-interval.json') -Encoding utf8
} finally {
    New-Item -ItemType File -Path $stopSignal -Force | Out-Null
    if (-not $collectorProcess.WaitForExit(10000)) {
        $collectorProcess.Kill($true)
        throw 'V5 telemetry collector did not stop cleanly.'
    }
    Require-V5 ($collectorProcess.ExitCode -eq 0) 'V5 telemetry collector failed.'
}

if ($Mode -eq 'Diagnostic') {
    Invoke-V5Native $node @($analyzer,'--diagnostic',$output)
} else {
    Invoke-V5Native $node @($analyzer,'--formal',$output)
}
$finalHead = (& git -C $root rev-parse HEAD).Trim()
$finalTree = @(& git -C $root status --porcelain=v1 --untracked-files=all)
Require-V5 ($LASTEXITCODE -eq 0 -and $finalHead -eq $head -and $finalTree.Count -eq 0 -and
    (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $configHash) `
    'V5 revision/worktree/config identity drifted during execution.'
Write-Output "V5 $Mode artifacts written to $output"
