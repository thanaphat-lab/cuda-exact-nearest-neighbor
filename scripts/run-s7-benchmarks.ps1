param(
    [Parameter(Mandatory = $true)] [string]$BuildDirectory,
    [Parameter(Mandatory = $true)] [string]$OutputDirectory,
    [Parameter(Mandatory = $true)] [string]$ExperimentConfig,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')] [string]$ExpectedRevision,
    [string]$EnvironmentApproval,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$buildRoot = Resolve-Path $BuildDirectory
$configPath = Resolve-Path $ExperimentConfig
$config = Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json

function Require-Value([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Add-CommandRecord([string]$Stage, [string[]]$Command) {
    [ordered]@{ captured_at = (Get-Date).ToString('o'); stage = $Stage; command = $Command } |
        ConvertTo-Json -Compress |
        Add-Content -LiteralPath (Join-Path $preflightDirectory 'commands.jsonl') -Encoding utf8
}

Require-Value ($config.schema_version -eq 'S7-EXPERIMENT-CONFIG-v1') `
    'Unsupported or missing experiment configuration schema.'
Require-Value (-not [string]::IsNullOrWhiteSpace($config.experiment_id)) `
    'Experiment ID is mandatory.'
Require-Value ($config.expected_revision_source -eq 'owner-supplied runner argument') `
    'Experiment configuration must not embed a final Git revision.'
Require-Value ($config.hypotheses.Count -eq 4) 'Exactly four predeclared hypotheses are required.'
Require-Value (($config.hypotheses.id -join ',') -eq 'H1,H2,H3,H4') `
    'Predeclared hypotheses must be H1 through H4 in order.'
Require-Value ([int]$config.sampling.warmups -eq 10 -and `
    [int]$config.sampling.measured_repetitions -eq 100) `
    'Acceptance configuration requires exactly 10 warmups and 100 retained measurements.'
Require-Value ($config.workload.tuning_seed -ne $config.workload.final_seed) `
    'Tuning and final seeds must differ.'

$head = (& git -C $repositoryRoot rev-parse HEAD).Trim()
Require-Value ($LASTEXITCODE -eq 0 -and $head -eq $ExpectedRevision.ToLowerInvariant()) `
    "Owner expected revision $ExpectedRevision does not match HEAD $head."
$treeEntries = @(& git -C $repositoryRoot status --porcelain=v1)
if (-not $DryRun) {
    Require-Value ($treeEntries.Count -eq 0) `
        'Acceptance measurement requires a clean worktree; no dirty-build authorization exists.'
}

$policyPath = Join-Path $repositoryRoot `
    'docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md'
$policyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $policyPath).Hash.ToLowerInvariant()
Require-Value ($policyHash -eq $config.approved_numerical_policy.sha256) `
    'Approved numerical-policy hash mismatch.'

$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
$ninjaPath = Join-Path $buildRoot 'build.ninja'
Require-Value (Test-Path -LiteralPath $cachePath) 'CMakeCache.txt is mandatory.'
Require-Value (Test-Path -LiteralPath $ninjaPath) 'Ninja build evidence is mandatory.'
$cache = Get-Content -LiteralPath $cachePath
$buildType = ($cache | Where-Object { $_ -match '^CMAKE_BUILD_TYPE:' } |
    Select-Object -First 1) -replace '^.*=', ''
$cudaArchitecture = ($cache | Where-Object { $_ -match '^CMAKE_CUDA_ARCHITECTURES:' } |
    Select-Object -First 1) -replace '^.*=', ''
Require-Value ($buildType -eq 'Release') 'S7 acceptance requires a Release build.'
Require-Value (-not [string]::IsNullOrWhiteSpace($cudaArchitecture)) `
    'CUDA architecture is missing from CMakeCache.txt.'
$buildNinjaText = Get-Content -Raw -LiteralPath $ninjaPath
Require-Value ($buildNinjaText -notmatch '(?i)--use_fast_math|-use_fast_math') `
    'Fast-math flag detected in build.ninja.'
$buildFlags = "Release;CUDA_ARCH=$cudaArchitecture;fast_math=disabled;/fp:strict"

$preflightDirectory = Join-Path $OutputDirectory 'environment-capture'
if ((Test-Path -LiteralPath $OutputDirectory) -and -not $DryRun) {
    throw "Output directory already exists: $OutputDirectory"
}
New-Item -ItemType Directory -Force -Path $preflightDirectory | Out-Null
Set-Content -LiteralPath (Join-Path $preflightDirectory 'commands.jsonl') `
    -Value $null -Encoding utf8
Copy-Item -LiteralPath $configPath -Destination `
    (Join-Path $preflightDirectory 'experiment-config.json') -Force
Copy-Item -LiteralPath $cachePath -Destination `
    (Join-Path $preflightDirectory 'CMakeCache.txt') -Force
Copy-Item -LiteralPath $ninjaPath -Destination `
    (Join-Path $preflightDirectory 'build.ninja') -Force
$configHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $configPath).Hash.ToLowerInvariant()

$computer = Get-CimInstance Win32_ComputerSystem
$processor = Get-CimInstance Win32_Processor | Select-Object -First 1
$operatingSystem = Get-CimInstance Win32_OperatingSystem
Require-Value ([uint64]$computer.TotalPhysicalMemory -gt 0) 'Host memory metadata is unavailable.'
Require-Value (-not [string]::IsNullOrWhiteSpace($processor.Name)) 'CPU metadata is unavailable.'
Require-Value (-not [string]::IsNullOrWhiteSpace($operatingSystem.Version)) `
    'Operating-system metadata is unavailable.'

$nvidiaSmi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
Require-Value ($null -ne $nvidiaSmi) 'nvidia-smi is mandatory for S7 acceptance telemetry.'
$beforeXmlPath = Join-Path $preflightDirectory 'nvidia-smi-before.xml'
& $nvidiaSmi.Source -q -x | Set-Content -LiteralPath $beforeXmlPath -Encoding utf8
Require-Value ($LASTEXITCODE -eq 0) 'nvidia-smi preflight capture failed.'
[xml]$beforeXml = Get-Content -Raw -LiteralPath $beforeXmlPath
$gpu = @($beforeXml.nvidia_smi_log.gpu)[0]
Require-Value ($null -ne $gpu) 'No NVIDIA GPU was present in preflight capture.'
$gpuUuid = [string]$gpu.uuid
$gpuName = [string]$gpu.product_name
Require-Value (-not [string]::IsNullOrWhiteSpace($gpuUuid)) 'GPU UUID is mandatory.'
Require-Value (-not [string]::IsNullOrWhiteSpace($gpuName)) 'GPU model is mandatory.'

$approvalHash = 'not-applicable-dry-run'
$approvedProcessNames = @()
if (-not $DryRun) {
    Require-Value (-not [string]::IsNullOrWhiteSpace($EnvironmentApproval)) `
        'Owner environment approval is mandatory for acceptance measurement.'
    $approvalPath = Resolve-Path $EnvironmentApproval
    $approval = Get-Content -Raw -LiteralPath $approvalPath | ConvertFrom-Json
    Require-Value ($approval.schema_version -eq 'S7-ENVIRONMENT-APPROVAL-v1') `
        'Unsupported environment approval schema.'
    Require-Value ($approval.experiment_id -eq $config.experiment_id) `
        'Environment approval experiment ID mismatch.'
    Require-Value ($approval.owner_environment_ready -eq $true -and `
        $approval.power_clock_settings_unchanged -eq $true) `
        'Owner environment readiness and unchanged power/clock attestations are required.'
    Require-Value ($approval.expected_gpu_uuid -eq $gpuUuid) `
        "Approved GPU UUID $($approval.expected_gpu_uuid) does not match $gpuUuid."
    $approvedProcessNames = @($approval.approved_gpu_process_names | ForEach-Object {
        ([string]$_).ToLowerInvariant()
    })
    Copy-Item -LiteralPath $approvalPath -Destination `
        (Join-Path $preflightDirectory 'environment-approval.json') -Force
    $approvalHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $approvalPath).Hash.ToLowerInvariant()
} else {
    [ordered]@{
        schema_version = 'S7-ENVIRONMENT-APPROVAL-v1'; experiment_id = $config.experiment_id
        expected_gpu_uuid = $gpuUuid; approved_gpu_process_names = @()
        owner_environment_ready = $false; power_clock_settings_unchanged = $false
        dry_run_only = $true
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath `
        (Join-Path $preflightDirectory 'environment-approval.json') -Encoding utf8
}

$preflightProcesses = @($gpu.processes.process_info | ForEach-Object {
    [IO.Path]::GetFileName([string]$_.process_name).ToLowerInvariant()
} | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
if (-not $DryRun) {
    $unauthorized = @($preflightProcesses | Where-Object { $_ -notin $approvedProcessNames })
    Require-Value ($unauthorized.Count -eq 0) `
        "Unauthorized GPU processes detected before measurement: $($unauthorized -join ', ')"
}

$capturedAt = (Get-Date).ToString('o')
$timezone = [System.TimeZoneInfo]::Local.Id
$streamPolicy = 'default CUDA stream; synchronous one-shot public API; one benchmark process'
[ordered]@{
    schema_version = 'S7-HOST-ENVIRONMENT-v2'; experiment_id = $config.experiment_id
    captured_at = $capturedAt; timezone = $timezone
    operating_system = $operatingSystem.Caption; os_version = $operatingSystem.Version
    os_build = $operatingSystem.BuildNumber; machine_name = [System.Environment]::MachineName
    cpu = $processor.Name; processor_count = [System.Environment]::ProcessorCount
    host_memory_bytes = [string][uint64]$computer.TotalPhysicalMemory
    gpu_name = $gpuName; gpu_uuid = $gpuUuid; gpu_memory = [string]$gpu.fb_memory_usage.total
    driver_version = [string]$beforeXml.nvidia_smi_log.driver_version
    wddm_or_tcc = [string]$gpu.driver_model.current_dm
    pcie_current_generation = [string]$gpu.pci.pci_gpu_link_info.pcie_gen.current_link_gen
    pcie_current_width = [string]$gpu.pci.pci_gpu_link_info.link_widths.current_link_width
    power_limit = [string]$gpu.gpu_power_readings.current_power_limit
    preflight_temperature = [string]$gpu.temperature.gpu_temp
    preflight_gpu_processes = $preflightProcesses
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath `
    (Join-Path $preflightDirectory 'host-environment.json') -Encoding utf8

[ordered]@{
    schema_version = 'S7-RUN-METADATA-v1'; experiment_id = $config.experiment_id
    experiment_config_sha256 = $configHash
    owner_expected_revision = $ExpectedRevision.ToLowerInvariant(); observed_head = $head
    working_tree_state = if ($treeEntries.Count -eq 0) { 'clean' } else { 'dirty-provisional' }
    approved_design = $config.approved_design; approved_numerical_policy_sha256 = $policyHash
    captured_at = $capturedAt; timezone = $timezone; operator = [System.Environment]::UserName
    build_type = $buildType; cuda_architecture = $cudaArchitecture; build_flags = $buildFlags
    build_ninja_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $ninjaPath).Hash.ToLowerInvariant()
    cmake_cache_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $cachePath).Hash.ToLowerInvariant()
    fast_math = 'disabled-verified-no-fast-math-flag'; generator_id = $config.workload.generator_id
    stream_policy = $streamPolicy; environment_approval_sha256 = $approvalHash
    approved_gpu_process_names = $approvedProcessNames
    telemetry_interval_ms = [int]$config.environment_policy.telemetry_interval_ms
    dry_run = [bool]$DryRun
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath `
    (Join-Path $preflightDirectory 'run-metadata.json') -Encoding utf8

$MyInvocation.Line | Set-Content -LiteralPath `
    (Join-Path $preflightDirectory 'orchestration-command.txt') -Encoding utf8
Add-CommandRecord 'orchestration' @($MyInvocation.Line)

$benchmark = Join-Path $buildRoot 's7_benchmark.exe'
Require-Value (Test-Path -LiteralPath $benchmark) "Benchmark executable not found: $benchmark"
$benchmarkOptions = @(
    '--experiment-id', [string]$config.experiment_id, '--experiment-config', [string]$configPath,
    '--config-sha256', $configHash, '--expected-revision', $ExpectedRevision.ToLowerInvariant(),
    '--tuning-seed', [string]$config.workload.tuning_seed,
    '--final-seed', [string]$config.workload.final_seed, '--captured-at', $capturedAt,
    '--timezone', $timezone, '--approved-design', [string]$config.approved_design,
    '--generator-id', [string]$config.workload.generator_id,
    '--cuda-architecture', $cudaArchitecture, '--build-flags', $buildFlags,
    '--stream-policy', $streamPolicy
)

$telemetryDirectory = Join-Path $preflightDirectory 'telemetry'
$telemetryStop = Join-Path $preflightDirectory 'stop-telemetry.signal'
$telemetryScript = Join-Path $PSScriptRoot 'capture-s7-telemetry.ps1'
if (Test-Path -LiteralPath $telemetryStop) { Remove-Item -LiteralPath $telemetryStop }
$pwsh = (Get-Process -Id $PID).Path
$quoteArgument = { param([string]$Value) '"' + $Value.Replace('"', '\"') + '"' }
$telemetryArgumentLine = '-NoProfile -File {0} -NvidiaSmi {1} -OutputDirectory {2} ' +
    '-StopSignal {3} -IntervalMilliseconds {4}'
$telemetryArgumentLine = $telemetryArgumentLine -f `
    (& $quoteArgument $telemetryScript), (& $quoteArgument $nvidiaSmi.Source), `
    (& $quoteArgument $telemetryDirectory), (& $quoteArgument $telemetryStop), `
    [string]$config.environment_policy.telemetry_interval_ms
$telemetryLaunchErrors = Join-Path $preflightDirectory 'telemetry-launch-errors.log'
$telemetryProcess = Start-Process -FilePath $pwsh -ArgumentList $telemetryArgumentLine `
    -RedirectStandardError $telemetryLaunchErrors -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds ([int]$config.environment_policy.telemetry_interval_ms + 250)
Require-Value (-not $telemetryProcess.HasExited) `
    "Telemetry helper exited during startup: $(Get-Content -Raw -LiteralPath $telemetryLaunchErrors)"
Require-Value (Test-Path -LiteralPath (Join-Path $telemetryDirectory 'gpu-telemetry.jsonl')) `
    'Telemetry helper did not create its GPU telemetry artifact.'

try {
    if ($DryRun) {
        Add-CommandRecord 'benchmark-dry-run' (@($benchmark, '--dry-run', $OutputDirectory) + $benchmarkOptions)
        & $benchmark --dry-run $OutputDirectory @benchmarkOptions
        if ($LASTEXITCODE -ne 0) { throw "Benchmark dry-run failed with exit code $LASTEXITCODE" }
    } else {
        $ctest = Join-Path $buildRoot 'ctest.exe'
        if (-not (Test-Path -LiteralPath $ctest)) {
            $cmakeEntry = $cache | Where-Object { $_ -like 'CMAKE_COMMAND:INTERNAL=*' } |
                Select-Object -First 1
            $cmakePath = $cmakeEntry.Substring($cmakeEntry.IndexOf('=') + 1)
            $ctest = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
        }
        Require-Value (Test-Path -LiteralPath $ctest) 'ctest.exe could not be located.'
        Add-CommandRecord 'ctest' @($ctest, '--test-dir', [string]$buildRoot, '--output-on-failure')
        & $ctest --test-dir $buildRoot --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Pre-tuning tests failed with exit code $LASTEXITCODE" }

        $qualificationDirectory = Join-Path $OutputDirectory 'candidate-qualification'
        New-Item -ItemType Directory -Force -Path $qualificationDirectory | Out-Null
        foreach ($blockSize in @(128, 256, 512)) {
            $candidateFreeze = Join-Path $qualificationDirectory "block-$blockSize.csv"
            $lines = [System.Collections.Generic.List[string]]::new()
            $lines.Add('schema_version,variant,N,D,block_size')
            foreach ($variant in @('v2', 'v3')) {
                foreach ($candidateCount in @(1024, 16384, 262144, 1048576)) {
                    foreach ($dimension in @(8, 32, 128)) {
                        $lines.Add("S7-TUNING-FREEZE-v1,$variant,$candidateCount,$dimension,$blockSize")
                    }
                }
            }
            $lines | Set-Content -LiteralPath $candidateFreeze -Encoding utf8
            foreach ($variant in @('v2', 'v3')) {
                $matrixExecutable = Join-Path $buildRoot "s7_cuda_${variant}_matrix_tests.exe"
                $matrixOutput = Join-Path $qualificationDirectory "$variant-block-$blockSize-matrix.txt"
                $reportRoot = Join-Path $qualificationDirectory "$variant-block-$blockSize-reports"
                $matrixArgs = @($matrixOutput, $reportRoot,
                    (Join-Path $buildRoot "evidence/cuda-$variant/binary.manifest.txt"),
                    (Join-Path $buildRoot "evidence/cuda-$variant/functional.txt"), $candidateFreeze)
                Add-CommandRecord "qualify-$variant-$blockSize" (@($matrixExecutable) + $matrixArgs)
                & $matrixExecutable @matrixArgs
                if ($LASTEXITCODE -ne 0) { throw "Candidate qualification failed for $variant/$blockSize" }
            }
        }

        $tuningDirectory = Join-Path $OutputDirectory 'tuning'
        Add-CommandRecord 'tuning' (@($benchmark, '--tune', $tuningDirectory) + $benchmarkOptions)
        & $benchmark --tune $tuningDirectory @benchmarkOptions
        if ($LASTEXITCODE -ne 0) { throw "Tuning failed with exit code $LASTEXITCODE" }
        $freeze = Join-Path $tuningDirectory 'tuning-freeze.csv'

        $correctnessDirectory = Join-Path $OutputDirectory 'correctness-after-freeze'
        New-Item -ItemType Directory -Force -Path $correctnessDirectory | Out-Null
        foreach ($variant in @('v2', 'v3')) {
            $matrixExecutable = Join-Path $buildRoot "s7_cuda_${variant}_matrix_tests.exe"
            $matrixOutput = Join-Path $correctnessDirectory "cuda-$variant-matrix.txt"
            $reportRoot = Join-Path $correctnessDirectory "cuda-$variant-reports"
            $matrixArgs = @($matrixOutput, $reportRoot,
                (Join-Path $buildRoot "evidence/cuda-$variant/binary.manifest.txt"),
                (Join-Path $buildRoot "evidence/cuda-$variant/functional.txt"), $freeze)
            Add-CommandRecord "correctness-after-freeze-$variant" (@($matrixExecutable) + $matrixArgs)
            & $matrixExecutable @matrixArgs
            if ($LASTEXITCODE -ne 0) { throw "Frozen $variant correctness matrix failed" }
        }

        $gate = Join-Path $correctnessDirectory 'correctness-gate.json'
        [ordered]@{
            schema_version = 'S7-CORRECTNESS-GATE-v1'; status = 'passed-clean-candidate'
            revision = $head; working_tree_state = 'clean'; experiment_id = $config.experiment_id
            experiment_config_sha256 = $configHash
            v2_matrix = Join-Path $correctnessDirectory 'cuda-v2-matrix.txt'
            v3_matrix = Join-Path $correctnessDirectory 'cuda-v3-matrix.txt'; freeze = $freeze
        } | ConvertTo-Json | Set-Content -LiteralPath $gate -Encoding utf8

        $finalDirectory = Join-Path $OutputDirectory 'final'
        Add-CommandRecord 'final' (@($benchmark, '--run', $finalDirectory, $gate, $freeze) + $benchmarkOptions)
        & $benchmark --run $finalDirectory $gate $freeze @benchmarkOptions
        if ($LASTEXITCODE -ne 0) { throw "Final benchmark failed with exit code $LASTEXITCODE" }
        Copy-Item -LiteralPath $gate -Destination (Join-Path $finalDirectory 'correctness-gate.json')
    }
} finally {
    New-Item -ItemType File -Force -Path $telemetryStop | Out-Null
    if (-not $telemetryProcess.WaitForExit(15000)) {
        Stop-Process -Id $telemetryProcess.Id -Force
        $telemetryProcess.WaitForExit()
    }
    & $nvidiaSmi.Source -q -x | Set-Content -LiteralPath `
        (Join-Path $preflightDirectory 'nvidia-smi-after.xml') -Encoding utf8
}

$telemetryErrors = @(Get-Content -LiteralPath `
    (Join-Path $telemetryDirectory 'telemetry-errors.log') | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_)
    })
Require-Value ($telemetryErrors.Count -eq 0) `
    "Telemetry capture reported errors: $($telemetryErrors -join '; ')"
$metricRows = @(Get-Content -LiteralPath (Join-Path $telemetryDirectory 'gpu-telemetry.jsonl') |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
Require-Value ($metricRows.Count -gt 0) 'Continuous GPU telemetry produced no samples.'

$analyzer = Join-Path $PSScriptRoot 'analyze-s7-benchmarks.mjs'
if ($DryRun) {
    Add-CommandRecord 'analyze-dry-run' @('node', $analyzer, $OutputDirectory, '--shared-root', $OutputDirectory)
    node $analyzer $OutputDirectory --shared-root $OutputDirectory
} else {
    foreach ($phase in @('tuning', 'final')) {
        $phaseDirectory = Join-Path $OutputDirectory $phase
        Add-CommandRecord "analyze-$phase" @('node', $analyzer, $phaseDirectory, '--shared-root', $OutputDirectory)
    }
    foreach ($phase in @('tuning', 'final')) {
        $phaseDirectory = Join-Path $OutputDirectory $phase
        node $analyzer $phaseDirectory --shared-root $OutputDirectory
        if ($LASTEXITCODE -ne 0) { break }
    }
}
if ($LASTEXITCODE -ne 0) { throw "Benchmark analysis failed with exit code $LASTEXITCODE" }
