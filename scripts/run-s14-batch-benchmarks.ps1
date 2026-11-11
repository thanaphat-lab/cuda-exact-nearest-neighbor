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

function Assert-NonemptyArtifact([string]$Path, [string]$Message) {
    Require-Value ((Test-Path -LiteralPath $Path) -and
        (Get-Item -LiteralPath $Path).Length -gt 0) $Message
}

function Add-CommandRecord([string]$Path, [string]$Stage, [string[]]$Command) {
    [ordered]@{ captured_at = [DateTimeOffset]::Now.ToString('o'); stage = $Stage;
        command = $Command } | ConvertTo-Json -Compress |
        Add-Content -LiteralPath $Path -Encoding utf8
}

function Add-CommandResult([string]$Path, [string]$Stage,
        [DateTimeOffset]$StartedAt, [DateTimeOffset]$EndedAt, [int]$ExitCode,
        [string[]]$Artifacts) {
    $artifactRecords = @($Artifacts | ForEach-Object {
        [ordered]@{
            path = $_
            exists = Test-Path -LiteralPath $_
            length = if (Test-Path -LiteralPath $_) {
                (Get-Item -LiteralPath $_).Length
            } else { $null }
            sha256 = if (Test-Path -LiteralPath $_) {
                (Get-FileHash -Algorithm SHA256 -LiteralPath $_).Hash.ToLowerInvariant()
            } else { $null }
        }
    })
    [ordered]@{
        schema_version = 'S14-COMMAND-RESULT-v1'
        stage = $Stage
        started_at = $StartedAt.ToString('o')
        ended_at = $EndedAt.ToString('o')
        exit_code = $ExitCode
        artifacts = $artifactRecords
    } | ConvertTo-Json -Compress -Depth 6 |
        Add-Content -LiteralPath $Path -Encoding utf8
}

function New-Process([string]$FilePath, [string[]]$Arguments,
        [string]$StandardErrorPath = '') {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FilePath
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    if (-not [string]::IsNullOrWhiteSpace($StandardErrorPath)) {
        $info.RedirectStandardError = $true
    }
    foreach ($argument in $Arguments) { [void]$info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    if (-not $process.Start()) { throw "Failed to start $FilePath." }
    return $process
}

function New-OrchestrationRecord([string[]]$EffectiveArguments,
        [string]$ResolvedBuildDirectory, [string]$ResolvedOutputDirectory,
        [string]$ResolvedApprovalPath, [string]$ConfigSha256,
        [string]$ApprovalSha256) {
    [ordered]@{
        schema_version = 'S14-ORCHESTRATION-COMMAND-v1'
        captured_at = [DateTimeOffset]::Now.ToString('o')
        executable = (Get-Process -Id $PID).Path
        script = $PSCommandPath
        arguments = $EffectiveArguments
        working_directory = (Get-Location).Path
        resolved_build_directory = $ResolvedBuildDirectory
        resolved_output_directory = $ResolvedOutputDirectory
        resolved_config_path = $configPath
        resolved_environment_approval = $ResolvedApprovalPath
        runner_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $PSCommandPath).Hash.ToLowerInvariant()
        config_sha256 = $ConfigSha256
        environment_approval_sha256 = $ApprovalSha256
    }
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

Require-Value ($config.schema_version -eq 'S14-BATCH-EXPERIMENT-CONFIG-v2') `
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
Require-Value ($config.environment_policy.telemetry_required -eq $true -and
    [int]$config.environment_policy.telemetry_interval_ms -eq 1000 -and
    [int]$config.environment_policy.maximum_telemetry_gap_intervals -eq 3) `
    'S14 telemetry interval and maximum-gap policy mismatch.'
$maximumTelemetryGapMs = [int]$config.environment_policy.telemetry_interval_ms *
    [int]$config.environment_policy.maximum_telemetry_gap_intervals
$telemetryQueryTimeoutMs = [int]$config.environment_policy.telemetry_query_timeout_ms
Require-Value ($telemetryQueryTimeoutMs -gt 0 -and
    $telemetryQueryTimeoutMs -lt [int]$config.environment_policy.telemetry_interval_ms -and
    $telemetryQueryTimeoutMs -lt $maximumTelemetryGapMs) `
    'S14 telemetry query timeout must be positive and below the interval and maximum gap.'
Require-Value ($config.environment_policy.telemetry_incomplete_action -eq
    'retain-samples-and-mark-hypotheses-inconclusive') `
    'S14 incomplete telemetry must retain samples and mark performance inconclusive.'

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
$designPath = Join-Path $repositoryRoot $config.approved_design.path
$designApprovalPath = Join-Path $repositoryRoot $config.approved_design.approval_record
Require-Value ((Test-Path -LiteralPath $designPath) -and
    (Test-Path -LiteralPath $designApprovalPath)) `
    'Approved S14 design or approval record is missing.'
$designHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $designPath).Hash.ToLowerInvariant()
Require-Value ($designHash -eq $config.approved_design.sha256) `
    'Approved S14 design hash mismatch.'
$designApprovalText = Get-Content -Raw -LiteralPath $designApprovalPath
Require-Value ($designApprovalText.Contains($config.approved_design.approval_id) -and
    $designApprovalText.Contains($designHash)) `
    'S14 approval record does not bind the configured design hash and approval ID.'

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
    $record = New-OrchestrationRecord @('-Mode', 'Final', '-BuildDirectory',
        'D:\path with spaces\build') 'D:\path with spaces\build' 'D:\out' '' `
        ('a' * 64) ''
    Require-Value ($record.arguments.Count -eq 4 -and
        $record.arguments[3] -eq 'D:\path with spaces\build' -and
        $record.schema_version -eq 'S14-ORCHESTRATION-COMMAND-v1') `
        'S14 structured orchestration record self-test failed.'
    $temporaryArtifact = [IO.Path]::GetTempFileName()
    try {
        'evidence' | Set-Content -LiteralPath $temporaryArtifact -Encoding utf8
        Assert-NonemptyArtifact $temporaryArtifact 'Nonempty artifact was rejected.'
        $missingRejected = $false
        try { Assert-NonemptyArtifact ($temporaryArtifact + '.missing') 'expected' }
        catch { $missingRejected = $true }
        Require-Value $missingRejected 'Missing prerequisite artifact was accepted.'
    } finally {
        Remove-Item -LiteralPath $temporaryArtifact -Force -ErrorAction SilentlyContinue
    }
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
$commandResultsPath = Join-Path $environmentDirectory 'command-results.jsonl'
Set-Content -LiteralPath $commandResultsPath -Value $null -Encoding utf8
$resolvedApprovalForRecord = ''
$approvalHashForRecord = ''
if (-not [string]::IsNullOrWhiteSpace($EnvironmentApproval) -and
        (Test-Path -LiteralPath $EnvironmentApproval)) {
    $resolvedApprovalForRecord = (Resolve-Path $EnvironmentApproval).Path
    $approvalHashForRecord = (Get-FileHash -Algorithm SHA256 -LiteralPath `
        $resolvedApprovalForRecord).Hash.ToLowerInvariant()
}
$effectiveArguments = @('-Mode', $Mode, '-BuildDirectory', $buildRoot,
    '-OutputDirectory', $outputRoot, '-ExpectedRevision', $head,
    '-ExperimentId', $ExperimentId)
if (-not [string]::IsNullOrWhiteSpace($resolvedApprovalForRecord)) {
    $effectiveArguments += @('-EnvironmentApproval', $resolvedApprovalForRecord)
}
$orchestrationRecord = New-OrchestrationRecord $effectiveArguments $buildRoot `
    $outputRoot $resolvedApprovalForRecord $configHash $approvalHashForRecord
$orchestrationRecord | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath `
    (Join-Path $environmentDirectory 'orchestration-command.json') -Encoding utf8
$displayArguments = @($effectiveArguments | ForEach-Object {
    "'" + ([string]$_).Replace("'", "''") + "'"
})
("& '{0}' {1}" -f $PSCommandPath, ($displayArguments -join ' ')) |
    Set-Content -LiteralPath (Join-Path $environmentDirectory `
        'orchestration-command.txt') -Encoding utf8

$cmakeEntry = $cache | Where-Object { $_ -like 'CMAKE_COMMAND:INTERNAL=*' } |
    Select-Object -First 1
Require-Value ($null -ne $cmakeEntry) 'CMake command is missing from CMakeCache.txt.'
$cmakePath = $cmakeEntry.Substring($cmakeEntry.IndexOf('=') + 1)
$ctest = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
Require-Value (Test-Path -LiteralPath $ctest) 'ctest.exe could not be located.'
$ctestLog = Join-Path $environmentDirectory 'prerequisite-ctest.log'
$ctestJunit = Join-Path $environmentDirectory 'prerequisite-ctest.junit.xml'
$ctestArguments = @('--test-dir', $buildRoot, '--output-on-failure',
    '--output-log', $ctestLog, '--output-junit', $ctestJunit, '-R',
    '^(s14\.cpu_batch\.functional|s7\.cuda_v2\.(functional|required_matrix)|s14\.cuda_v4_batch\.required_matrix)$')
Add-CommandRecord $commandsPath 's14-prerequisite-ctest' (@($ctest) + $ctestArguments)
$ctestStartedAt = [DateTimeOffset]::Now
& $ctest @ctestArguments
$ctestExitCode = $LASTEXITCODE
$ctestEndedAt = [DateTimeOffset]::Now
Add-CommandResult $commandResultsPath 's14-prerequisite-ctest' $ctestStartedAt `
    $ctestEndedAt $ctestExitCode @($ctestLog, $ctestJunit)
Assert-NonemptyArtifact $ctestLog 'S14 prerequisite CTest log is missing or empty.'
Assert-NonemptyArtifact $ctestJunit `
    'S14 prerequisite CTest JUnit report is missing or empty.'
if ($ctestExitCode -ne 0) { throw 'S14 prerequisite correctness tests failed.' }
Add-CommandRecord $commandsPath 's14-distinct-batch-correctness' `
    (@($benchmark, '--verify', $correctnessDirectory, $freezePath) + $commonOptions)
$correctnessStartedAt = [DateTimeOffset]::Now
& $benchmark --verify $correctnessDirectory $freezePath @commonOptions
$correctnessExitCode = $LASTEXITCODE
$correctnessEndedAt = [DateTimeOffset]::Now
Add-CommandResult $commandResultsPath 's14-distinct-batch-correctness' `
    $correctnessStartedAt $correctnessEndedAt $correctnessExitCode @()
if ($correctnessExitCode -ne 0) { throw 'S14 distinct-query correctness failed.' }
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
Copy-Item -LiteralPath $designPath -Destination `
    (Join-Path $environmentDirectory 'approved-design.md')
Copy-Item -LiteralPath $designApprovalPath -Destination `
    (Join-Path $environmentDirectory 'design-approval-record.md')
Copy-Item -LiteralPath $cachePath -Destination `
    (Join-Path $environmentDirectory 'CMakeCache.txt')
Copy-Item -LiteralPath $ninjaPath -Destination `
    (Join-Path $environmentDirectory 'build.ninja')
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
    approved_design_id = [string]$config.approved_design.id
    approved_design_sha256 = $designHash
    design_approval_id = [string]$config.approved_design.approval_id
    dataset_validation = 'included in CPU batch E2E; excluded from prepared GPU lanes'
    persistent_resource_destruction = 'excluded; not complete lifetime cost'
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath `
    (Join-Path $environmentDirectory 'environment.json') -Encoding utf8

$telemetryDirectory = Join-Path $environmentDirectory 'telemetry'
$telemetryStop = Join-Path $environmentDirectory 'stop-telemetry.signal'
$telemetryScript = Join-Path $PSScriptRoot 'capture-s14-telemetry.ps1'
$telemetryErrors = Join-Path $environmentDirectory 'telemetry-launch-errors.log'
$pwsh = (Get-Process -Id $PID).Path
$telemetryArguments = @('-NoProfile', '-File', $telemetryScript,
    '-NvidiaSmi', $nvidiaSmi.Source, '-OutputDirectory', $telemetryDirectory,
    '-StopSignal', $telemetryStop, '-IntervalMilliseconds',
    [string]$config.environment_policy.telemetry_interval_ms,
    '-QueryTimeoutMilliseconds', [string]$telemetryQueryTimeoutMs)
Add-CommandRecord $commandsPath 's14-telemetry-collector' `
    (@($pwsh) + $telemetryArguments)
$telemetryStartedAt = [DateTimeOffset]::Now
$telemetryProcess = New-Process $pwsh $telemetryArguments $telemetryErrors
$readyPath = Join-Path $telemetryDirectory 'telemetry-ready.json'
$readyDeadline = [DateTimeOffset]::Now.AddSeconds(10)
while (-not (Test-Path -LiteralPath $readyPath) -and
        [DateTimeOffset]::Now -lt $readyDeadline -and
        -not $telemetryProcess.HasExited) {
    Start-Sleep -Milliseconds 100
}
if (-not (Test-Path -LiteralPath $readyPath) -or $telemetryProcess.HasExited) {
    New-Item -ItemType File -Force -Path $telemetryStop | Out-Null
    if (-not $telemetryProcess.HasExited -and
            -not $telemetryProcess.WaitForExit(5000)) {
        $telemetryProcess.Kill($true)
        $telemetryProcess.WaitForExit()
    }
    $telemetryProcess.StandardError.ReadToEnd() |
        Set-Content -LiteralPath $telemetryErrors -Encoding utf8
    throw 'Telemetry helper did not produce a complete ready sample before measurement.'
}

New-Item -ItemType Directory -Path $finalDirectory | Out-Null
$benchmarkArguments = @('--run', $finalDirectory, $freezePath) + $commonOptions +
    @('--correctness-reference', $gatePath)
$benchmarkStartedAt = $null
$benchmarkEndedAt = $null
$benchmarkExitCode = $null
$benchmarkProcess = $null
$telemetryExitedBeforeBenchmarkEnd = $false
$telemetryExitedBeforeStopSignal = $false
$telemetryForcedTermination = $false
try {
    Add-CommandRecord $commandsPath 's14-final-benchmark' `
        (@($benchmark) + $benchmarkArguments)
    $benchmarkStartedAt = [DateTimeOffset]::Now
    $benchmarkProcess = New-Process $benchmark $benchmarkArguments
    $benchmarkProcess.WaitForExit()
    $benchmarkExitCode = $benchmarkProcess.ExitCode
    $benchmarkEndedAt = [DateTimeOffset]::Now
    $telemetryExitedBeforeBenchmarkEnd = $telemetryProcess.HasExited
    if (-not $telemetryExitedBeforeBenchmarkEnd) {
        Start-Sleep -Milliseconds ($maximumTelemetryGapMs + 250)
    }
    $telemetryExitedBeforeStopSignal = $telemetryProcess.HasExited
    if ($benchmarkExitCode -eq 0) {
        Copy-Item -LiteralPath $gatePath -Destination `
            (Join-Path $finalDirectory 'correctness-gate.json')
    }
} finally {
    New-Item -ItemType File -Force -Path $telemetryStop | Out-Null
    if (-not $telemetryProcess.WaitForExit(15000)) {
        $telemetryForcedTermination = $true
        $telemetryProcess.Kill($true)
        $telemetryProcess.WaitForExit()
    }
    $telemetryProcess.StandardError.ReadToEnd() |
        Set-Content -LiteralPath $telemetryErrors -Encoding utf8
    & $nvidiaSmi.Source -q -x | Set-Content -LiteralPath `
        (Join-Path $environmentDirectory 'nvidia-smi-after.xml') -Encoding utf8
}
$telemetryExitCode = $telemetryProcess.ExitCode
$telemetryEndedAt = [DateTimeOffset]::Now
Add-CommandResult $commandResultsPath 's14-telemetry-collector' `
    $telemetryStartedAt $telemetryEndedAt $telemetryExitCode @(
        (Join-Path $telemetryDirectory 'telemetry-attempts.jsonl'),
        (Join-Path $telemetryDirectory 'gpu-telemetry.jsonl'),
        (Join-Path $telemetryDirectory 'gpu-processes.jsonl'),
        (Join-Path $telemetryDirectory 'telemetry-errors.log'),
        (Join-Path $telemetryDirectory 'telemetry-session.json'))
Add-CommandResult $commandResultsPath 's14-final-benchmark' $benchmarkStartedAt `
    $benchmarkEndedAt ([int]$benchmarkExitCode) @()
[ordered]@{
    schema_version = 'S14-BENCHMARK-INTERVAL-v1'
    benchmark_started_at = $benchmarkStartedAt.ToString('o')
    benchmark_ended_at = $benchmarkEndedAt.ToString('o')
    benchmark_pid = $benchmarkProcess.Id
    benchmark_exit_code = $benchmarkExitCode
    telemetry_pid = $telemetryProcess.Id
    telemetry_exit_code = $telemetryExitCode
    telemetry_exited_before_benchmark_end = $telemetryExitedBeforeBenchmarkEnd
    telemetry_exited_before_stop_signal = $telemetryExitedBeforeStopSignal
    telemetry_forced_termination = $telemetryForcedTermination
    telemetry_interval_ms = [int]$config.environment_policy.telemetry_interval_ms
    maximum_telemetry_gap_ms = $maximumTelemetryGapMs
    telemetry_query_timeout_ms = $telemetryQueryTimeoutMs
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath `
    (Join-Path $environmentDirectory 'benchmark-interval.json') -Encoding utf8
if ($benchmarkExitCode -ne 0) { throw 'S14 final benchmark execution failed.' }

$analyzer = Join-Path $PSScriptRoot 'analyze-s14-batch-benchmarks.mjs'
Add-CommandRecord $commandsPath 's14-analysis' @('node', $analyzer, $finalDirectory)
$analysisStartedAt = [DateTimeOffset]::Now
node $analyzer $finalDirectory
$analysisExitCode = $LASTEXITCODE
$analysisEndedAt = [DateTimeOffset]::Now
Add-CommandResult $commandResultsPath 's14-analysis' $analysisStartedAt `
    $analysisEndedAt $analysisExitCode @(
        (Join-Path $finalDirectory 'environment-status.json'),
        (Join-Path $finalDirectory 'summary.json'),
        (Join-Path $finalDirectory 'comparisons.json'),
        (Join-Path $finalDirectory 'break-even.json'))
if ($analysisExitCode -ne 0) { throw 'S14 batch benchmark analysis failed.' }
