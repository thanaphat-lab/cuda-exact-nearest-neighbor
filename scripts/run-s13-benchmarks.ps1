param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('SelfTest', 'Final')]
    [string]$Mode,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedRevision,
    [string]$EnvironmentApproval
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$configPath = Join-Path $repositoryRoot 'benchmarks/config/s13-minimal-v2.json'
$config = Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json

function Require-Value([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Add-CommandRecord([string]$Path, [string]$Stage, [string[]]$Command) {
    [ordered]@{ captured_at = [DateTimeOffset]::Now.ToString('o'); stage = $Stage;
        command = $Command } | ConvertTo-Json -Compress |
        Add-Content -LiteralPath $Path -Encoding utf8
}

function Assert-S13PreparedMatrixSummary(
    [string]$MatrixText,
    [object[]]$CandidateCounts,
    [object[]]$Dimensions
) {
    $caseLines = @($MatrixText -split '\r?\n' | Where-Object { $_ -match '^case=' })
    $expectedCaseCount = $CandidateCounts.Count * $Dimensions.Count
    Require-Value ($caseLines.Count -eq $expectedCaseCount) `
        "Frozen V2 matrix reported $($caseLines.Count) case rows; expected $expectedCaseCount."
    foreach ($candidateCount in $CandidateCounts) {
        foreach ($dimension in $Dimensions) {
            $casePattern = "^case=N$candidateCount-D$dimension(?:\s|$)"
            $matchingRows = @($caseLines | Where-Object { $_ -match $casePattern })
            Require-Value ($matchingRows.Count -eq 1) `
                "Frozen V2 matrix must report exactly one N=$candidateCount D=$dimension row."
            Require-Value ($matchingRows[0] -match `
                '(?:^|\s)prepared_v2=passed(?:\s|$)') `
                "Frozen V2 matrix N=$candidateCount D=$dimension did not pass the prepared V2 path."
        }
    }
}

Require-Value ($config.schema_version -eq 'S13-EXPERIMENT-CONFIG-v1') `
    'Unsupported S13 experiment configuration schema.'
Require-Value ([int]$config.sampling.warmups -eq 10 -and `
    [int]$config.sampling.measured_repetitions -eq 100) `
    'S13 requires exactly 10 warm-ups and 100 measured repetitions.'
Require-Value (($config.workload.candidate_counts -join ',') -eq `
    '1024,16384,262144,1048576' -and `
    ($config.workload.dimensions -join ',') -eq '8,32,128') `
    'S13 workload matrix differs from the approved 12 cases.'
Require-Value ($config.break_even.persistent_resource_destruction -match `
    'excluded.*not complete lifetime') `
    'S13 must disclose that break-even excludes persistent-resource destruction.'

$freezePath = Join-Path $repositoryRoot $config.frozen_v2_configuration.path
Require-Value (Test-Path -LiteralPath $freezePath) 'The immutable S7 V2 freeze is missing.'
$freezeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $freezePath).Hash.ToLowerInvariant()
Require-Value ($freezeHash -eq $config.frozen_v2_configuration.sha256) `
    'The immutable S7 V2 freeze hash does not match the S13 declaration.'

$policyPath = Join-Path $repositoryRoot `
    'docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md'
$policyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $policyPath).Hash.ToLowerInvariant()
Require-Value ($policyHash -eq $config.approved_numerical_policy.sha256) `
    'Approved numerical-policy hash mismatch.'

if ($Mode -eq 'SelfTest') {
    $validRows = [System.Collections.Generic.List[string]]::new()
    foreach ($candidateCount in $config.workload.candidate_counts) {
        foreach ($dimension in $config.workload.dimensions) {
            $validRows.Add(
                "case=N$candidateCount-D$dimension prepared_v2=passed nn_verdict=accepted-unambiguous")
        }
    }
    $validMatrix = $validRows -join "`n"
    Assert-S13PreparedMatrixSummary $validMatrix `
        $config.workload.candidate_counts $config.workload.dimensions

    $missingCaseRejected = $false
    try {
        Assert-S13PreparedMatrixSummary `
            (($validRows | Select-Object -SkipLast 1) -join "`n") `
            $config.workload.candidate_counts $config.workload.dimensions
    } catch { $missingCaseRejected = $true }
    Require-Value $missingCaseRejected `
        'S13 matrix regression test accepted a missing workload.'

    $failedMarkerRejected = $false
    try {
        Assert-S13PreparedMatrixSummary `
            ($validMatrix -replace 'prepared_v2=passed', 'prepared_v2=failed') `
            $config.workload.candidate_counts $config.workload.dimensions
    } catch { $failedMarkerRejected = $true }
    Require-Value $failedMarkerRejected `
        'S13 matrix regression test accepted a failed prepared V2 marker.'

    $metadataMarkerRejected = $false
    try {
        Assert-S13PreparedMatrixSummary `
            ($validMatrix -replace 'prepared_v2=passed', `
                'prepared_production_path_status=passed') `
            $config.workload.candidate_counts $config.workload.dimensions
    } catch { $metadataMarkerRejected = $true }
    Require-Value $metadataMarkerRejected `
        'S13 matrix regression test accepted the per-case metadata marker in the summary.'

    Write-Output 'S13 orchestration self-test: passed'
    return
}

Require-Value (-not [string]::IsNullOrWhiteSpace($BuildDirectory)) `
    'Final mode requires BuildDirectory.'
Require-Value (-not [string]::IsNullOrWhiteSpace($OutputDirectory)) `
    'Final mode requires OutputDirectory.'
Require-Value (-not [string]::IsNullOrWhiteSpace($ExpectedRevision)) `
    'Final mode requires ExpectedRevision.'
Require-Value (-not [string]::IsNullOrWhiteSpace($EnvironmentApproval)) `
    'Final mode requires an Owner environment approval.'

$buildRoot = (Resolve-Path $BuildDirectory).Path
$head = (& git -C $repositoryRoot rev-parse HEAD).Trim()
Require-Value ($LASTEXITCODE -eq 0 -and `
    $head -eq $ExpectedRevision.ToLowerInvariant()) `
    "Expected revision $ExpectedRevision does not match HEAD $head."
$treeEntries = @(& git -C $repositoryRoot status --porcelain=v1 --untracked-files=all)
Require-Value ($treeEntries.Count -eq 0) `
    'Final S13 correctness and performance require a clean worktree.'
Require-Value (-not (Test-Path -LiteralPath $OutputDirectory)) `
    "S13 output directory already exists: $OutputDirectory"

$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
$ninjaPath = Join-Path $buildRoot 'build.ninja'
Require-Value (Test-Path -LiteralPath $cachePath) 'CMakeCache.txt is mandatory.'
Require-Value (Test-Path -LiteralPath $ninjaPath) 'build.ninja is mandatory.'
$cache = Get-Content -LiteralPath $cachePath
$buildType = ($cache | Where-Object { $_ -match '^CMAKE_BUILD_TYPE:' } |
    Select-Object -First 1) -replace '^.*=', ''
$cudaArchitecture = ($cache | Where-Object { $_ -match '^CMAKE_CUDA_ARCHITECTURES:' } |
    Select-Object -First 1) -replace '^.*=', ''
Require-Value ($buildType -eq 'Release') 'S13 requires a Release build.'
Require-Value (-not [string]::IsNullOrWhiteSpace($cudaArchitecture)) `
    'CUDA architecture is missing from CMakeCache.txt.'
$buildNinja = Get-Content -Raw -LiteralPath $ninjaPath
Require-Value ($buildNinja -notmatch '(?i)--use_fast_math|-use_fast_math') `
    'Fast-math flag detected in build.ninja.'

$benchmark = Join-Path $buildRoot 's13_benchmark.exe'
$matrixExecutable = Join-Path $buildRoot 's7_cuda_v2_matrix_tests.exe'
Require-Value (Test-Path -LiteralPath $benchmark) 's13_benchmark.exe is missing.'
Require-Value (Test-Path -LiteralPath $matrixExecutable) `
    's7_cuda_v2_matrix_tests.exe is missing.'

$approvalPath = (Resolve-Path $EnvironmentApproval).Path
$approval = Get-Content -Raw -LiteralPath $approvalPath | ConvertFrom-Json
Require-Value ($approval.schema_version -eq 'S7-ENVIRONMENT-APPROVAL-v1') `
    'Unsupported environment approval schema.'
Require-Value ($approval.experiment_id -eq $config.experiment_id) `
    'Environment approval experiment ID mismatch.'
Require-Value ($approval.owner_environment_ready -eq $true -and `
    $approval.power_clock_settings_unchanged -eq $true) `
    'Owner environment readiness and unchanged power/clock attestations are required.'

$nvidiaSmi = Get-Command nvidia-smi -ErrorAction Stop
$beforeXmlPath = Join-Path $OutputDirectory 'environment-capture/nvidia-smi-before.xml'
$environmentDirectory = Join-Path $OutputDirectory 'environment-capture'
$correctnessDirectory = Join-Path $OutputDirectory 'correctness'
$finalDirectory = Join-Path $OutputDirectory 'final'
New-Item -ItemType Directory -Path $environmentDirectory | Out-Null
New-Item -ItemType Directory -Path $correctnessDirectory | Out-Null
New-Item -ItemType Directory -Path $finalDirectory | Out-Null
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
$unauthorizedBefore = @($preflightProcesses | Where-Object {
    $_ -notin $approvedProcessNames
})
Require-Value ($unauthorizedBefore.Count -eq 0) `
    "Unauthorized GPU process before measurement: $($unauthorizedBefore -join ', ')"

$capturedAt = [DateTimeOffset]::Now.ToString('o')
$timezone = [System.TimeZoneInfo]::Local.Id
$configHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $configPath).Hash.ToLowerInvariant()
$buildFlags = "Release;CUDA_ARCH=$cudaArchitecture;fast_math=disabled;/fp:strict"
$streamPolicy = 'default CUDA stream; synchronous public APIs; one benchmark process'
$commandsPath = Join-Path $environmentDirectory 'commands.jsonl'
Set-Content -LiteralPath $commandsPath -Value $null -Encoding utf8
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
    schema_version = 'S13-ENVIRONMENT-v1'; experiment_id = $config.experiment_id
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
    build_type = $buildType; cuda_architecture = $cudaArchitecture
    build_flags = $buildFlags; fast_math = 'disabled-verified'
    config_sha256 = $configHash; freeze_sha256 = $freezeHash
    persistent_resource_destruction = 'excluded from construction costs and prepared reuse; not complete lifetime cost'
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

try {
    $cmakeEntry = $cache | Where-Object { $_ -like 'CMAKE_COMMAND:INTERNAL=*' } |
        Select-Object -First 1
    Require-Value ($null -ne $cmakeEntry) 'CMake command is missing from CMakeCache.txt.'
    $cmakePath = $cmakeEntry.Substring($cmakeEntry.IndexOf('=') + 1)
    $ctest = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
    Require-Value (Test-Path -LiteralPath $ctest) 'ctest.exe could not be located.'
    $ctestArguments = @('--test-dir', $buildRoot, '--output-on-failure', '-R',
        '^s7\.cuda_v2\.(functional|required_matrix)$')
    Add-CommandRecord $commandsPath 'v2-prerequisite-ctest' (@($ctest) + $ctestArguments)
    & $ctest @ctestArguments
    if ($LASTEXITCODE -ne 0) { throw 'V2 prerequisite correctness tests failed.' }

    $matrixOutput = Join-Path $correctnessDirectory 'cuda-v2-matrix.txt'
    $matrixReports = Join-Path $correctnessDirectory 'cuda-v2-reports'
    $matrixArguments = @($matrixOutput, $matrixReports,
        (Join-Path $buildRoot 'evidence/cuda-v2/binary.manifest.txt'),
        (Join-Path $buildRoot 'evidence/cuda-v2/functional.txt'), $freezePath)
    Add-CommandRecord $commandsPath 'v2-frozen-correctness' `
        (@($matrixExecutable) + $matrixArguments)
    & $matrixExecutable @matrixArguments
    if ($LASTEXITCODE -ne 0) { throw 'Frozen V2 raw/prepared correctness matrix failed.' }
    $matrixText = Get-Content -Raw -LiteralPath $matrixOutput
    Assert-S13PreparedMatrixSummary $matrixText `
        $config.workload.candidate_counts $config.workload.dimensions

    $gatePath = Join-Path $correctnessDirectory 'correctness-gate.json'
    [ordered]@{
        schema_version = 'S13-CORRECTNESS-GATE-v1'; status = 'passed-clean-candidate'
        revision = $head; working_tree_state = 'clean'; experiment_id = $config.experiment_id
        experiment_config_sha256 = $configHash; freeze_sha256 = $freezeHash
        matrix = $matrixOutput; prepared_production_path_required = $true
    } | ConvertTo-Json | Set-Content -LiteralPath $gatePath -Encoding utf8

    $benchmarkOptions = @(
        '--experiment-id', [string]$config.experiment_id,
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
        '--freeze-sha256', $freezeHash,
        '--correctness-reference', $gatePath
    )
    Add-CommandRecord $commandsPath 's13-final-benchmark' `
        (@($benchmark, '--run', $finalDirectory, $freezePath) + $benchmarkOptions)
    & $benchmark --run $finalDirectory $freezePath @benchmarkOptions
    if ($LASTEXITCODE -ne 0) { throw 'S13 final benchmark execution failed.' }
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
$internalProcesses = @('s13_benchmark.exe', 's7_cuda_v2_tests.exe',
    's7_cuda_v2_matrix_tests.exe')
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
} else {
    'retain-samples-mark-hypotheses-inconclusive'
}
[ordered]@{
    schema_version = 'S13-ENVIRONMENT-STATUS-v1'; status = $environmentStatus
    unauthorized_gpu_processes = $unauthorizedDuring
    hardware_thermal_slowdown_samples = $thermalRows.Count
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath `
    (Join-Path $finalDirectory 'environment-status.json') -Encoding utf8

$analyzer = Join-Path $PSScriptRoot 'analyze-s13-benchmarks.mjs'
Add-CommandRecord $commandsPath 's13-analysis' @('node', $analyzer, $finalDirectory)
node $analyzer $finalDirectory
if ($LASTEXITCODE -ne 0) { throw 'S13 benchmark analysis failed.' }
