param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('SelfTest', 'Verify', 'Final')]
    [string]$Mode,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedRevision,
    [string]$ExperimentId,
    [string]$EnvironmentApproval
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$configPath = Join-Path $repositoryRoot 'benchmarks/config/s14-batch-v4.json'
$config = Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json

function Require-Value([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Add-CommandRecord([string]$Path, [string]$Stage, [string[]]$Command) {
    [ordered]@{ captured_at = [DateTimeOffset]::Now.ToString('o'); stage = $Stage;
        command = $Command } | ConvertTo-Json -Compress |
        Add-Content -LiteralPath $Path -Encoding utf8
}

function Assert-S14Matrix([string]$MatrixText) {
    $caseLines = @($MatrixText -split '\r?\n' | Where-Object { $_ -match '^case=' })
    Require-Value ($caseLines.Count -eq 36) `
        "S14 distinct-query matrix reported $($caseLines.Count) cases; expected 36."
    foreach ($candidateCount in $config.workload.candidate_counts) {
        foreach ($dimension in $config.workload.dimensions) {
            foreach ($batchSize in $config.workload.batch_sizes) {
                $pattern = "^case=N$candidateCount-D$dimension-B$batchSize(?:\s|$)"
                $rows = @($caseLines | Where-Object { $_ -match $pattern })
                Require-Value ($rows.Count -eq 1) `
                    "S14 matrix must contain exactly one N=$candidateCount D=$dimension B=$batchSize case."
                foreach ($marker in @('distinct_queries=passed', 'cpu_batch=passed',
                        'sequential_prepared_v2=passed', 'v4_batch=passed',
                        'bitwise_equivalence=passed', 'float64=passed')) {
                    Require-Value ($rows[0] -match "(?:^|\s)$marker(?:\s|$)") `
                        "S14 matrix case N=$candidateCount D=$dimension B=$batchSize lacks $marker."
                }
            }
        }
    }
}

Require-Value ($config.schema_version -eq 'S14-BATCH-EXPERIMENT-CONFIG-v1') `
    'Unsupported S14 experiment configuration schema.'
Require-Value (($config.workload.candidate_counts -join ',') -eq `
    '1024,16384,262144,1048576' -and `
    ($config.workload.dimensions -join ',') -eq '8,32,128' -and `
    ($config.workload.batch_sizes -join ',') -eq '1,8,32') `
    'S14 workload matrix differs from the approved 36 configurations.'
Require-Value ([int]$config.sampling.warmups -eq 10 -and `
    [int]$config.sampling.measured_repetitions -eq 100) `
    'S14 requires exactly 10 warm-ups and 100 measured repetitions.'
Require-Value ($config.workload.generator_id -eq 'S14-DISTINCT-BATCH-v1') `
    'S14 distinct-query generator identity mismatch.'
Require-Value ($config.kernel_only -match 'omitted') `
    'S14 minimal design must omit kernel-only timing.'
Require-Value ($config.break_even.persistent_resource_destruction -match `
    'excluded.*not complete lifetime') `
    'S14 must disclose excluded persistent-resource destruction.'

$freezePath = Join-Path $repositoryRoot $config.frozen_v2_configuration.path
Require-Value (Test-Path -LiteralPath $freezePath) 'The immutable S7 V2 freeze is missing.'
$freezeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $freezePath).Hash.ToLowerInvariant()
Require-Value ($freezeHash -eq $config.frozen_v2_configuration.sha256) `
    'The immutable S7 V2 freeze hash does not match the S14 declaration.'
$policyPath = Join-Path $repositoryRoot `
    'docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md'
$policyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $policyPath).Hash.ToLowerInvariant()
Require-Value ($policyHash -eq $config.approved_numerical_policy.sha256) `
    'Approved numerical-policy hash mismatch.'

if ($Mode -eq 'SelfTest') {
    $validRows = foreach ($candidateCount in $config.workload.candidate_counts) {
        foreach ($dimension in $config.workload.dimensions) {
            foreach ($batchSize in $config.workload.batch_sizes) {
                "case=N$candidateCount-D$dimension-B$batchSize distinct_queries=passed cpu_batch=passed sequential_prepared_v2=passed v4_batch=passed bitwise_equivalence=passed float64=passed"
            }
        }
    }
    Assert-S14Matrix ($validRows -join "`n")
    $rejected = $false
    try { Assert-S14Matrix (($validRows | Select-Object -SkipLast 1) -join "`n") }
    catch { $rejected = $true }
    Require-Value $rejected 'S14 matrix self-test accepted a missing case.'
    $rejected = $false
    try { Assert-S14Matrix (($validRows -replace 'v4_batch=passed', 'v4_batch=failed') -join "`n") }
    catch { $rejected = $true }
    Require-Value $rejected 'S14 matrix self-test accepted a failed V4 marker.'
    Write-Output 'S14 batch orchestration self-test: passed'
    return
}

foreach ($value in @($BuildDirectory, $OutputDirectory, $ExpectedRevision, $ExperimentId)) {
    Require-Value (-not [string]::IsNullOrWhiteSpace($value)) `
        'Verify/Final modes require build, output, revision, and experiment ID.'
}
$buildRoot = (Resolve-Path $BuildDirectory).Path
$head = (& git -C $repositoryRoot rev-parse HEAD).Trim()
Require-Value ($LASTEXITCODE -eq 0 -and $head -eq $ExpectedRevision.ToLowerInvariant()) `
    "Expected revision $ExpectedRevision does not match HEAD $head."
$treeEntries = @(& git -C $repositoryRoot status --porcelain=v1 --untracked-files=all)
Require-Value ($treeEntries.Count -eq 0) `
    'S14 clean verification and performance require a clean worktree.'
$outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputDirectory))
}
Require-Value (-not (Test-Path -LiteralPath $outputRoot)) `
    "S14 output directory already exists: $outputRoot"

$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
$ninjaPath = Join-Path $buildRoot 'build.ninja'
Require-Value (Test-Path -LiteralPath $cachePath) 'CMakeCache.txt is mandatory.'
Require-Value (Test-Path -LiteralPath $ninjaPath) 'build.ninja is mandatory.'
$cache = Get-Content -LiteralPath $cachePath
$buildType = ($cache | Where-Object { $_ -match '^CMAKE_BUILD_TYPE:' } |
    Select-Object -First 1) -replace '^.*=', ''
$cudaArchitecture = ($cache | Where-Object { $_ -match '^CMAKE_CUDA_ARCHITECTURES:' } |
    Select-Object -First 1) -replace '^.*=', ''
Require-Value ($buildType -eq 'Release') 'S14 requires a Release build.'
Require-Value (-not [string]::IsNullOrWhiteSpace($cudaArchitecture)) `
    'CUDA architecture is missing from CMakeCache.txt.'
$buildNinja = Get-Content -Raw -LiteralPath $ninjaPath
Require-Value ($buildNinja -notmatch '(?i)--use_fast_math|-use_fast_math') `
    'Fast-math flag detected in build.ninja.'

$benchmark = Join-Path $buildRoot 's14_batch_benchmark.exe'
Require-Value (Test-Path -LiteralPath $benchmark) 's14_batch_benchmark.exe is missing.'
$configHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $configPath).Hash.ToLowerInvariant()
$capturedAt = [DateTimeOffset]::Now.ToString('o')
$timezone = [System.TimeZoneInfo]::Local.Id
$buildFlags = "Release;CUDA_ARCH=$cudaArchitecture;fast_math=disabled;/fp:strict"
$streamPolicy = 'default CUDA stream; synchronous public APIs; one benchmark process'
$commonOptions = @(
    '--experiment-id', $ExperimentId,
    '--experiment-config', $configPath,
    '--config-sha256', $configHash,
    '--expected-revision', $head,
    '--final-seed', [string]$config.workload.final_seed,
    '--captured-at', $capturedAt,
    '--timezone', $timezone,
    '--generator-id', [string]$config.workload.generator_id,
    '--cuda-architecture', $cudaArchitecture,
    '--build-flags', $buildFlags,
    '--stream-policy', $streamPolicy,
    '--freeze-sha256', $freezeHash
)

$environmentDirectory = Join-Path $outputRoot 'environment-capture'
$correctnessDirectory = Join-Path $outputRoot 'correctness'
$finalDirectory = Join-Path $outputRoot 'final'
New-Item -ItemType Directory -Path $environmentDirectory | Out-Null
New-Item -ItemType Directory -Path $correctnessDirectory | Out-Null
$commandsPath = Join-Path $environmentDirectory 'commands.jsonl'
Set-Content -LiteralPath $commandsPath -Value $null -Encoding utf8

$cmakeEntry = $cache | Where-Object { $_ -like 'CMAKE_COMMAND:INTERNAL=*' } |
    Select-Object -First 1
Require-Value ($null -ne $cmakeEntry) 'CMake command is missing from CMakeCache.txt.'
$cmakePath = $cmakeEntry.Substring($cmakeEntry.IndexOf('=') + 1)
$ctest = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
Require-Value (Test-Path -LiteralPath $ctest) 'ctest.exe could not be located.'
$ctestArguments = @('--test-dir', $buildRoot, '--output-on-failure', '-R',
    '^(s14\.cpu_batch\.functional|s7\.cuda_v2\.(functional|required_matrix)|s14\.cuda_v4_batch\.required_matrix)$')
Add-CommandRecord $commandsPath 's14-prerequisite-ctest' (@($ctest) + $ctestArguments)
& $ctest @ctestArguments
if ($LASTEXITCODE -ne 0) { throw 'S14 prerequisite correctness tests failed.' }
Add-CommandRecord $commandsPath 's14-distinct-batch-correctness' `
    (@($benchmark, '--verify', $correctnessDirectory, $freezePath) + $commonOptions)
& $benchmark --verify $correctnessDirectory $freezePath @commonOptions
if ($LASTEXITCODE -ne 0) { throw 'S14 distinct-query correctness failed.' }
$matrixPath = Join-Path $correctnessDirectory 'distinct-batch-matrix.txt'
Assert-S14Matrix (Get-Content -Raw -LiteralPath $matrixPath)
$gatePath = Join-Path $correctnessDirectory 'correctness-gate.json'
Require-Value (Test-Path -LiteralPath $gatePath) 'S14 correctness gate is missing.'

if ($Mode -eq 'Verify') {
    Write-Output "S14 clean correctness verification passed: $gatePath"
    return
}

Require-Value (-not [string]::IsNullOrWhiteSpace($EnvironmentApproval)) `
    'Final mode requires an Owner environment approval.'
$approvalPath = (Resolve-Path $EnvironmentApproval).Path
$approval = Get-Content -Raw -LiteralPath $approvalPath | ConvertFrom-Json
Require-Value ($approval.schema_version -eq 'S7-ENVIRONMENT-APPROVAL-v1') `
    'Unsupported environment approval schema.'
Require-Value ($approval.experiment_id -eq $ExperimentId) `
    'Environment approval experiment ID mismatch.'
Require-Value ($approval.owner_environment_ready -eq $true -and `
    $approval.power_clock_settings_unchanged -eq $true) `
    'Owner environment readiness and unchanged power/clock attestations are required.'

$nvidiaSmi = Get-Command nvidia-smi -ErrorAction Stop
$beforeXmlPath = Join-Path $environmentDirectory 'nvidia-smi-before.xml'
& $nvidiaSmi.Source -q -x | Set-Content -LiteralPath $beforeXmlPath -Encoding utf8
Require-Value ($LASTEXITCODE -eq 0) 'nvidia-smi preflight capture failed.'
[xml]$beforeXml = Get-Content -Raw -LiteralPath $beforeXmlPath
$gpu = @($beforeXml.nvidia_smi_log.gpu)[0]
Require-Value ($null -ne $gpu) 'No NVIDIA GPU was present in preflight capture.'
Require-Value ([string]$gpu.uuid -eq [string]$approval.expected_gpu_uuid) `
    'Owner-approved GPU UUID does not match the observed GPU.'
$approvedProcessNames = @($approval.approved_gpu_process_names | ForEach-Object {
    ([IO.Path]::GetFileName([string]$_)).ToLowerInvariant()
})
$preflightProcesses = @($gpu.processes.process_info | ForEach-Object {
    ([IO.Path]::GetFileName([string]$_.process_name)).ToLowerInvariant()
} | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
$unauthorizedBefore = @($preflightProcesses | Where-Object { $_ -notin $approvedProcessNames })
Require-Value ($unauthorizedBefore.Count -eq 0) `
    "Unauthorized GPU process before measurement: $($unauthorizedBefore -join ', ')"

Copy-Item -LiteralPath $configPath -Destination `
    (Join-Path $environmentDirectory 'experiment-config.json')
Copy-Item -LiteralPath $freezePath -Destination `
    (Join-Path $environmentDirectory 'v2-tuning-freeze.csv')
Copy-Item -LiteralPath $approvalPath -Destination `
    (Join-Path $environmentDirectory 'environment-approval.json')
Copy-Item -LiteralPath $cachePath -Destination `
    (Join-Path $environmentDirectory 'CMakeCache.txt')
Copy-Item -LiteralPath $ninjaPath -Destination `
    (Join-Path $environmentDirectory 'build.ninja')
$MyInvocation.Line | Set-Content -LiteralPath `
    (Join-Path $environmentDirectory 'orchestration-command.txt') -Encoding utf8
$computer = Get-CimInstance Win32_ComputerSystem
$processor = Get-CimInstance Win32_Processor | Select-Object -First 1
$operatingSystem = Get-CimInstance Win32_OperatingSystem
$cxxCompiler = ($cache | Where-Object { $_ -match '^CMAKE_CXX_COMPILER:' } |
    Select-Object -First 1) -replace '^.*=', ''
$cudaCompiler = ($cache | Where-Object { $_ -match '^CMAKE_CUDA_COMPILER:' } |
    Select-Object -First 1) -replace '^.*=', ''
[ordered]@{
    schema_version = 'S14-BATCH-ENVIRONMENT-v1'; experiment_id = $ExperimentId
    captured_at = $capturedAt; timezone = $timezone; revision = $head
    working_tree_state = 'clean'; gpu_name = [string]$gpu.product_name
    gpu_uuid = [string]$gpu.uuid; driver_version = [string]$beforeXml.nvidia_smi_log.driver_version
    gpu_memory = [string]$gpu.fb_memory_usage.total
    operating_system = $operatingSystem.Caption; os_version = $operatingSystem.Version
    os_build = $operatingSystem.BuildNumber; cpu = $processor.Name
    processor_count = [Environment]::ProcessorCount
    host_memory_bytes = [string][uint64]$computer.TotalPhysicalMemory
    cxx_compiler = $cxxCompiler
    cxx_compiler_file_version = if (Test-Path -LiteralPath $cxxCompiler) {
        (Get-Item -LiteralPath $cxxCompiler).VersionInfo.FileVersion
    } else { 'unknown' }
    cuda_compiler = $cudaCompiler
    cuda_compiler_file_version = if (Test-Path -LiteralPath $cudaCompiler) {
        (Get-Item -LiteralPath $cudaCompiler).VersionInfo.FileVersion
    } else { 'unknown' }
    build_type = $buildType
    cuda_architecture = $cudaArchitecture; build_flags = $buildFlags
    fast_math = 'disabled-verified'; config_sha256 = $configHash
    freeze_sha256 = $freezeHash
    dataset_validation = 'included in CPU batch E2E; excluded from prepared GPU lanes'
    persistent_resource_destruction = 'excluded; not complete lifetime cost'
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath `
    (Join-Path $environmentDirectory 'environment.json') -Encoding utf8

$telemetryDirectory = Join-Path $environmentDirectory 'telemetry'
$telemetryStop = Join-Path $environmentDirectory 'stop-telemetry.signal'
$telemetryScript = Join-Path $PSScriptRoot 'capture-s7-telemetry.ps1'
$telemetryErrors = Join-Path $environmentDirectory 'telemetry-launch-errors.log'
$pwsh = (Get-Process -Id $PID).Path
$quote = { param([string]$Value) '"' + $Value.Replace('"', '\"') + '"' }
$telemetryArguments = '-NoProfile -File {0} -NvidiaSmi {1} -OutputDirectory {2} ' +
    '-StopSignal {3} -IntervalMilliseconds {4}'
$telemetryArguments = $telemetryArguments -f (& $quote $telemetryScript),
    (& $quote $nvidiaSmi.Source), (& $quote $telemetryDirectory),
    (& $quote $telemetryStop), [string]$config.environment_policy.telemetry_interval_ms
$telemetryProcess = Start-Process -FilePath $pwsh -ArgumentList $telemetryArguments `
    -RedirectStandardError $telemetryErrors -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds ([int]$config.environment_policy.telemetry_interval_ms + 250)
Require-Value (-not $telemetryProcess.HasExited) 'Telemetry helper failed during startup.'

New-Item -ItemType Directory -Path $finalDirectory | Out-Null
try {
    Add-CommandRecord $commandsPath 's14-final-benchmark' `
        (@($benchmark, '--run', $finalDirectory, $freezePath) + $commonOptions +
            @('--correctness-reference', $gatePath))
    & $benchmark --run $finalDirectory $freezePath @commonOptions `
        --correctness-reference $gatePath
    if ($LASTEXITCODE -ne 0) { throw 'S14 final benchmark execution failed.' }
    Copy-Item -LiteralPath $gatePath -Destination `
        (Join-Path $finalDirectory 'correctness-gate.json')
} finally {
    New-Item -ItemType File -Path $telemetryStop | Out-Null
    if (-not $telemetryProcess.WaitForExit(15000)) {
        Stop-Process -Id $telemetryProcess.Id -Force
        $telemetryProcess.WaitForExit()
    }
    & $nvidiaSmi.Source -q -x | Set-Content -LiteralPath `
        (Join-Path $environmentDirectory 'nvidia-smi-after.xml') -Encoding utf8
}

$captureErrors = @(Get-Content -LiteralPath `
    (Join-Path $telemetryDirectory 'telemetry-errors.log') | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_)
    })
Require-Value ($captureErrors.Count -eq 0) `
    "Telemetry capture errors: $($captureErrors -join '; ')"
$processRows = @(Get-Content -LiteralPath `
    (Join-Path $telemetryDirectory 'gpu-processes.jsonl') | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_)
    } | ForEach-Object { $_ | ConvertFrom-Json })
$internalProcesses = @('s14_batch_benchmark.exe', 's7_cuda_v2_tests.exe',
    's7_cuda_v2_matrix_tests.exe', 's14_cuda_v4_batch_matrix_tests.exe')
$unauthorizedDuring = @($processRows | Where-Object { -not $_.no_processes } |
    ForEach-Object { ([IO.Path]::GetFileName([string]$_.process_name)).ToLowerInvariant() } |
    Where-Object { $_ -notin $approvedProcessNames -and $_ -notin $internalProcesses } |
    Sort-Object -Unique)
$metricRows = @(Get-Content -LiteralPath `
    (Join-Path $telemetryDirectory 'gpu-telemetry.jsonl') | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_)
    } | ForEach-Object { $_ | ConvertFrom-Json })
Require-Value ($metricRows.Count -gt 0) 'Telemetry produced no metric samples.'
$thermalRows = @($metricRows | Where-Object {
    ([string]$_.hardware_thermal_slowdown).Trim().ToLowerInvariant() -notin `
        @('not active', 'no', '0', 'n/a')
})
$environmentStatus = if ($unauthorizedDuring.Count -eq 0 -and $thermalRows.Count -eq 0) {
    'accepted-for-analysis'
} else { 'retain-samples-mark-hypotheses-inconclusive' }
[ordered]@{
    schema_version = 'S14-BATCH-ENVIRONMENT-STATUS-v1'; status = $environmentStatus
    unauthorized_gpu_processes = $unauthorizedDuring
    hardware_thermal_slowdown_samples = $thermalRows.Count
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath `
    (Join-Path $finalDirectory 'environment-status.json') -Encoding utf8

$analyzer = Join-Path $PSScriptRoot 'analyze-s14-batch-benchmarks.mjs'
Add-CommandRecord $commandsPath 's14-analysis' @('node', $analyzer, $finalDirectory)
node $analyzer $finalDirectory
if ($LASTEXITCODE -ne 0) { throw 'S14 batch benchmark analysis failed.' }
