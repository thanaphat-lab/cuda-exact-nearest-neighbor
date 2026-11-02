param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$preflightDirectory = Join-Path $OutputDirectory 'environment-capture'
New-Item -ItemType Directory -Force -Path $preflightDirectory | Out-Null

@{
    schema_version = 'S7-HOST-ENVIRONMENT-v1'
    captured_at = (Get-Date).ToString('o')
    timezone = [System.TimeZoneInfo]::Local.Id
    operating_system = [System.Environment]::OSVersion.VersionString
    os_build = [System.Environment]::OSVersion.Version.ToString()
    machine_name = [System.Environment]::MachineName
    processor_count = [System.Environment]::ProcessorCount
    is_64_bit_os = [System.Environment]::Is64BitOperatingSystem
} | ConvertTo-Json | Set-Content -LiteralPath `
    (Join-Path $preflightDirectory 'host-environment.json') -Encoding utf8

$nvidiaSmi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
if ($null -ne $nvidiaSmi) {
    & $nvidiaSmi.Source -q -x | Set-Content -LiteralPath `
        (Join-Path $preflightDirectory 'nvidia-smi-before.xml') -Encoding utf8
} else {
    'nvidia-smi unavailable; GPU clock/temperature/power/PCIe metadata blocked.' |
        Set-Content -LiteralPath `
            (Join-Path $preflightDirectory 'nvidia-smi-unavailable.txt') -Encoding utf8
}

$MyInvocation.Line | Set-Content -LiteralPath `
    (Join-Path $preflightDirectory 'orchestration-command.txt') -Encoding utf8
$benchmark = Join-Path $BuildDirectory 's7_benchmark.exe'
if (-not (Test-Path -LiteralPath $benchmark)) {
    throw "Benchmark executable not found: $benchmark"
}

if ($DryRun) {
    & $benchmark --dry-run $OutputDirectory
} else {
    $ctest = Join-Path $BuildDirectory 'ctest.exe'
    if (-not (Test-Path -LiteralPath $ctest)) {
        $cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
        if (Test-Path -LiteralPath $cachePath) {
            $cmakeEntry = Get-Content -LiteralPath $cachePath |
                Where-Object { $_ -like 'CMAKE_COMMAND:INTERNAL=*' } |
                Select-Object -First 1
            if ($null -ne $cmakeEntry) {
                $cmakePath = $cmakeEntry.Substring($cmakeEntry.IndexOf('=') + 1)
                $ctest = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
            }
        }
    }
    if (-not (Test-Path -LiteralPath $ctest)) {
        throw 'ctest.exe could not be located from the build directory or CMake cache.'
    }
    & $ctest --test-dir $BuildDirectory --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "Pre-tuning correctness/regression tests failed with exit code $LASTEXITCODE"
    }

    $qualificationDirectory = Join-Path $OutputDirectory 'candidate-qualification'
    New-Item -ItemType Directory -Force -Path $qualificationDirectory | Out-Null
    $candidateCounts = @(1024, 16384, 262144, 1048576)
    $dimensions = @(8, 32, 128)
    foreach ($blockSize in @(128, 256, 512)) {
        $candidateFreeze = Join-Path $qualificationDirectory "block-$blockSize.csv"
        $lines = [System.Collections.Generic.List[string]]::new()
        $lines.Add('schema_version,variant,N,D,block_size')
        foreach ($variant in @('v2', 'v3')) {
            foreach ($candidateCount in $candidateCounts) {
                foreach ($dimension in $dimensions) {
                    $lines.Add("S7-TUNING-FREEZE-v1,$variant,$candidateCount,$dimension,$blockSize")
                }
            }
        }
        $lines | Set-Content -LiteralPath $candidateFreeze -Encoding utf8
        foreach ($variant in @('v2', 'v3')) {
            $matrixExecutable = Join-Path $BuildDirectory "s7_cuda_${variant}_matrix_tests.exe"
            $matrixOutput = Join-Path $qualificationDirectory "$variant-block-$blockSize-matrix.txt"
            $reportRoot = Join-Path $qualificationDirectory "$variant-block-$blockSize-reports"
            & $matrixExecutable $matrixOutput $reportRoot `
                (Join-Path $BuildDirectory "evidence/cuda-$variant/binary.manifest.txt") `
                (Join-Path $BuildDirectory "evidence/cuda-$variant/functional.txt") `
                $candidateFreeze
            if ($LASTEXITCODE -ne 0) {
                throw "Candidate qualification failed for $variant block $blockSize"
            }
        }
    }

    $tuningDirectory = Join-Path $OutputDirectory 'tuning'
    & $benchmark --tune $tuningDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "Tuning failed with exit code $LASTEXITCODE"
    }
    $freeze = Join-Path $tuningDirectory 'tuning-freeze.csv'

    $correctnessDirectory = Join-Path $OutputDirectory 'correctness-after-freeze'
    New-Item -ItemType Directory -Force -Path $correctnessDirectory | Out-Null
    $v2Output = Join-Path $correctnessDirectory 'cuda-v2-matrix.txt'
    $v2Reports = Join-Path $correctnessDirectory 'cuda-v2-reports'
    & (Join-Path $BuildDirectory 's7_cuda_v2_matrix_tests.exe') `
        $v2Output $v2Reports `
        (Join-Path $BuildDirectory 'evidence/cuda-v2/binary.manifest.txt') `
        (Join-Path $BuildDirectory 'evidence/cuda-v2/functional.txt') $freeze
    if ($LASTEXITCODE -ne 0) {
        throw "Frozen V2 correctness matrix failed with exit code $LASTEXITCODE"
    }
    $v3Output = Join-Path $correctnessDirectory 'cuda-v3-matrix.txt'
    $v3Reports = Join-Path $correctnessDirectory 'cuda-v3-reports'
    & (Join-Path $BuildDirectory 's7_cuda_v3_matrix_tests.exe') `
        $v3Output $v3Reports `
        (Join-Path $BuildDirectory 'evidence/cuda-v3/binary.manifest.txt') `
        (Join-Path $BuildDirectory 'evidence/cuda-v3/functional.txt') $freeze
    if ($LASTEXITCODE -ne 0) {
        throw "Frozen V3 correctness matrix failed with exit code $LASTEXITCODE"
    }

    $gate = Join-Path $correctnessDirectory 'correctness-gate.json'
    $tuningManifest = Get-Content -Raw -LiteralPath `
        (Join-Path $tuningDirectory 'manifest.json') | ConvertFrom-Json
    $gateStatus = if ($tuningManifest.working_tree_state -eq 'clean') {
        'passed-clean-candidate'
    } else {
        'passed-local-provisional'
    }
    @{
        schema_version = 'S7-CORRECTNESS-GATE-v1'
        status = $gateStatus
        revision = $tuningManifest.revision
        working_tree_state = $tuningManifest.working_tree_state
        v2_matrix = $v2Output
        v3_matrix = $v3Output
        freeze = $freeze
        acceptance_note = 'Dirty-worktree evidence is provisional; clean-revision rerun required.'
    } | ConvertTo-Json | Set-Content -LiteralPath $gate -Encoding utf8

    $finalDirectory = Join-Path $OutputDirectory 'final'
    & $benchmark --run $finalDirectory $gate $freeze
}
if ($LASTEXITCODE -ne 0) {
    throw "Benchmark runner failed with exit code $LASTEXITCODE"
}

if ($DryRun) {
    node (Join-Path $PSScriptRoot 'analyze-s7-benchmarks.mjs') $OutputDirectory
} else {
    node (Join-Path $PSScriptRoot 'analyze-s7-benchmarks.mjs') `
        (Join-Path $OutputDirectory 'tuning')
    if ($LASTEXITCODE -eq 0) {
        node (Join-Path $PSScriptRoot 'analyze-s7-benchmarks.mjs') `
            (Join-Path $OutputDirectory 'final')
    }
}
if ($LASTEXITCODE -ne 0) { throw "Benchmark analysis failed with exit code $LASTEXITCODE" }

if ($null -ne $nvidiaSmi) {
    & $nvidiaSmi.Source -q -x | Set-Content -LiteralPath `
        (Join-Path $preflightDirectory 'nvidia-smi-after.xml') -Encoding utf8
}
