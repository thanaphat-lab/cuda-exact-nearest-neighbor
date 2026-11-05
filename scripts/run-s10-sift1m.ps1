param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('Correctness','Final','SelfTest')]
    [string]$Mode,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$DatasetRoot,
    [string]$ExpectedRevision,
    [string]$CorrectnessReference
)

$ErrorActionPreference = 'Stop'
$S10SamplingGapPolicyMilliseconds = 2500
function Assert-S10CompileCommand {
    param([string]$Command, [bool]$Cuda, [string]$Label)
    if ($Command -match '(?i)--use_fast_math|-ffast-math|/fp:fast|--ftz=true|--prec-div=false|--prec-sqrt=false') {
        throw "unexpected relaxed/fast-math flag for $Label"
    }
    if ($Cuda) {
        foreach ($flag in @('--ftz=false','--fmad=true','--prec-div=true','--prec-sqrt=true','-Xcompiler=/fp:strict')) {
            if (-not $Command.Contains($flag)) { throw "required CUDA numerical flag $flag is missing for $Label" }
        }
    } elseif (-not $Command.Contains('/fp:strict')) {
        throw "required CPU /fp:strict flag is missing for $Label"
    }
}
function Assert-S10FreshOutput {
    param([string]$Path)
    if (Test-Path -LiteralPath $Path) {
        throw "S10 output directory must be fresh and absent: $Path"
    }
}
function Assert-S10IdentityEqual {
    param($Expected, $Actual, [string]$Phase)
    $expectedJson = $Expected | ConvertTo-Json -Depth 8 -Compress
    $actualJson = $Actual | ConvertTo-Json -Depth 8 -Compress
    if ($expectedJson -cne $actualJson) { throw "S10 identity drift detected $Phase" }
}
if ($Mode -eq 'SelfTest') {
    Assert-S10CompileCommand 'cl /O2 /fp:strict source.cpp' $false 'cpu-valid'
    Assert-S10CompileCommand 'nvcc --ftz=false --fmad=true --prec-div=true --prec-sqrt=true -Xcompiler=/fp:strict source.cu' $true 'cuda-valid'
    foreach ($invalid in @('cl /fp:fast source.cpp','cl /O2 source.cpp','nvcc --use_fast_math source.cu',
            'nvcc --ftz=true --fmad=true --prec-div=true --prec-sqrt=true -Xcompiler=/fp:strict source.cu',
            'nvcc --ftz=false --fmad=true --prec-div=false --prec-sqrt=true -Xcompiler=/fp:strict source.cu')) {
        $rejected = $false
        try { Assert-S10CompileCommand $invalid $invalid.StartsWith('nvcc') 'negative' }
        catch { $rejected = $true }
        if (-not $rejected) { throw "compile-policy negative self-test accepted: $invalid" }
    }
    $collision = [IO.Path]::GetTempFileName()
    try {
        $rejected = $false
        try { Assert-S10FreshOutput $collision } catch { $rejected = $true }
        if (-not $rejected) { throw 'stale output collision self-test was accepted' }
    } finally { Remove-Item -LiteralPath $collision -Force -ErrorAction SilentlyContinue }
    $identity = [ordered]@{ revision='r'; config='c'; artifacts=@('a') }
    Assert-S10IdentityEqual $identity ([ordered]@{ revision='r'; config='c'; artifacts=@('a') }) 'self-test-valid'
    $rejected = $false
    try { Assert-S10IdentityEqual $identity ([ordered]@{ revision='r'; config='changed'; artifacts=@('a') }) 'self-test-mutation' }
    catch { $rejected = $true }
    if (-not $rejected) { throw 'identity mutation self-test was accepted' }
    return
}
foreach ($requiredParameter in @($BuildDirectory,$OutputDirectory,$DatasetRoot,$ExpectedRevision)) {
    if ([string]::IsNullOrWhiteSpace($requiredParameter)) { throw 'normal S10 modes require all path/revision parameters' }
}
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$dataset = (Resolve-Path -LiteralPath $DatasetRoot).Path
$relativeDataset = [IO.Path]::GetRelativePath($root, $dataset)
if ($relativeDataset -eq '.' -or
    (-not $relativeDataset.StartsWith('..' + [IO.Path]::DirectorySeparatorChar)) -and
    (-not [IO.Path]::IsPathRooted($relativeDataset))) {
    throw 'DatasetRoot must be outside the repository'
}
$outputFull = [IO.Path]::GetFullPath($OutputDirectory)
Assert-S10FreshOutput $outputFull
$relativeOutput = [IO.Path]::GetRelativePath($root, $outputFull)
if ($relativeOutput -eq '.' -or
    ((-not $relativeOutput.StartsWith('..' + [IO.Path]::DirectorySeparatorChar)) -and
     (-not [IO.Path]::IsPathRooted($relativeOutput)))) {
    & git -C $root check-ignore --quiet -- $relativeOutput
    if ($LASTEXITCODE -ne 0) {
        throw 'repository-contained OutputDirectory must be covered by .gitignore'
    }
}

$config = Join-Path $root 'benchmarks/config/s10-sift1m.json'
$benchmark = Join-Path $build 's10_sift1m.exe'
$analyzer = Join-Path $root 'scripts/analyze-s10-sift1m.mjs'
$base = Join-Path $dataset 'sift_base.fvecs'
$query = Join-Path $dataset 'sift_query.fvecs'
foreach ($required in @($config, $benchmark, $analyzer, $base, $query)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "required S10 file is missing: $required"
    }
}

$actualRevision = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualRevision -ne $ExpectedRevision) {
    throw 'repository revision does not match ExpectedRevision'
}
$treeState = & git -C $root status --porcelain=v1 --untracked-files=all
if ($LASTEXITCODE -ne 0 -or $treeState) {
    throw 'S10 correctness and performance evidence require a clean worktree'
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$environmentDirectory = Join-Path $OutputDirectory 'environment-capture'
$provenanceDirectory = Join-Path $OutputDirectory 'provenance'
New-Item -ItemType Directory -Force -Path $environmentDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $provenanceDirectory | Out-Null

$configHash = (Get-FileHash -LiteralPath $config -Algorithm SHA256).Hash.ToLowerInvariant()
$baseHash = (Get-FileHash -LiteralPath $base -Algorithm SHA256).Hash.ToLowerInvariant()
$queryHash = (Get-FileHash -LiteralPath $query -Algorithm SHA256).Hash.ToLowerInvariant()
$baseBytes = (Get-Item -LiteralPath $base).Length
$queryBytes = (Get-Item -LiteralPath $query).Length
if ($baseBytes -ne 516000000) { throw 'sift_base.fvecs has an invalid canonical byte size' }
if ($queryBytes -ne 5160000) { throw 'sift_query.fvecs has an invalid canonical byte size' }
$cachePath = Join-Path $build 'CMakeCache.txt'
$compileCommandsPath = Join-Path $build 'compile_commands.json'
$sourceIdentityPath = Join-Path $build 'generated/s6/source_identity.txt'
foreach ($requiredBuildFile in @($cachePath, $compileCommandsPath, $sourceIdentityPath)) {
    if (-not (Test-Path -LiteralPath $requiredBuildFile -PathType Leaf)) {
        throw "required build identity file is missing: $requiredBuildFile"
    }
}
$cache = Get-Content -LiteralPath $cachePath
$cacheValue = { param([string]$Name)
    $line = $cache | Where-Object { $_ -match "^$([regex]::Escape($Name))(:[^=]+)?=" } | Select-Object -First 1
    if (-not $line) { return '' }
    return ($line -split '=',2)[1]
}
$cudaArchitecture = (($cache | Where-Object {
    $_ -match '^CMAKE_CUDA_ARCHITECTURES:'
}) -split '=', 2)[1]
$buildType = (($cache | Where-Object {
    $_ -match '^CMAKE_BUILD_TYPE:'
}) -split '=', 2)[1]
if ([string]::IsNullOrWhiteSpace($cudaArchitecture) -or $buildType -ne 'Release') {
    throw 'S10 requires an explicit CUDA architecture and Release build'
}
$compilerPath = & $cacheValue 'CMAKE_CXX_COMPILER'
$cudaCompilerPath = & $cacheValue 'CMAKE_CUDA_COMPILER'
$generator = & $cacheValue 'CMAKE_GENERATOR'
$cxxCompilerFile = Get-ChildItem (Join-Path $build 'CMakeFiles') -Recurse -Filter 'CMakeCXXCompiler.cmake' | Select-Object -First 1
$cudaCompilerFile = Get-ChildItem (Join-Path $build 'CMakeFiles') -Recurse -Filter 'CMakeCUDACompiler.cmake' | Select-Object -First 1
if (-not $cxxCompilerFile -or -not $cudaCompilerFile) { throw 'CMake compiler identity is missing' }
$cxxIdentity = Get-Content -Raw -LiteralPath $cxxCompilerFile.FullName
$cudaIdentity = Get-Content -Raw -LiteralPath $cudaCompilerFile.FullName
$extractCmakeSet = { param([string]$Text,[string]$Name)
    $pattern = 'set\({0} "([^"]+)"\)' -f [regex]::Escape($Name)
    $match = [regex]::Match($Text, $pattern)
    if (-not $match.Success) { throw "missing CMake identity field $Name" }
    return $match.Groups[1].Value
}
$compilerId = & $extractCmakeSet $cxxIdentity 'CMAKE_CXX_COMPILER_ID'
$compilerVersion = & $extractCmakeSet $cxxIdentity 'CMAKE_CXX_COMPILER_VERSION'
$cudaCompilerVersion = & $extractCmakeSet $cudaIdentity 'CMAKE_CUDA_COMPILER_VERSION'
$cudaToolkitVersion = & $extractCmakeSet $cudaIdentity 'CMAKE_CUDA_COMPILER_TOOLKIT_VERSION'
$compileRecords = Get-Content -Raw -LiteralPath $compileCommandsPath | ConvertFrom-Json
$requiredSources = @('src/cpu_baseline.cpp','src/cpu_evidence.cpp','benchmarks/s10_sift1m.cpp',
    'benchmarks/s10_sift1m_loader.cpp','src/cuda_v0.cu','src/cuda_v1.cu','src/cuda_v2.cu','src/cuda_v3.cu')
$effectiveCommands = @()
foreach ($source in $requiredSources) {
    $record = $compileRecords | Where-Object { $_.file.Replace('\','/').EndsWith($source) } | Select-Object -First 1
    if (-not $record) { throw "compile command is missing for $source" }
    $command = [string]$record.command
    Assert-S10CompileCommand $command $source.EndsWith('.cu') $source
    $effectiveCommands += [ordered]@{ source = $source; command = $command }
}
$buildFlags = 'Release;fp=strict;cuda=ftz-false,fmad-true,prec-div=true,prec-sqrt=true;fast-math=disabled'
$sourceIdentity = Get-Content -LiteralPath $sourceIdentityPath
$sourceManifestHash = (($sourceIdentity | Where-Object { $_ -like 'source_manifest_sha256=*' }) -split '=',2)[1]
if ($sourceManifestHash.Length -ne 64) { throw 'source manifest identity is malformed' }
$numericalIdentityPath = Join-Path $environmentDirectory 'numerical-build-identity.json'
[ordered]@{ schema_version='S10-NUMERICAL-BUILD-IDENTITY-v1'; build_type=$buildType;
    generator=$generator; cuda_architecture=$cudaArchitecture; compiler_id=$compilerId;
    compiler_version=$compilerVersion; compiler_path=$compilerPath;
    cuda_compiler_version=$cudaCompilerVersion; cuda_compiler_path=$cudaCompilerPath;
    cuda_toolkit_version=$cudaToolkitVersion; build_flags=$buildFlags;
    source_manifest_sha256=$sourceManifestHash; compile_commands=$effectiveCommands } |
    ConvertTo-Json -Depth 6 -Compress | Set-Content -LiteralPath $numericalIdentityPath -Encoding utf8
$numericalBuildIdentityHash = (Get-FileHash $numericalIdentityPath -Algorithm SHA256).Hash.ToLowerInvariant()
$artifactNames = @('s10_sift1m.exe','s10_sift1m_io.lib','s6_cpu_baseline.lib','s6_cpu_evidence.lib',
    's7_cuda_v0.lib','s7_cuda_v1.lib','s7_cuda_v2.lib','s7_cuda_v3.lib')
$artifactFiles = foreach ($name in $artifactNames) {
    $path = Join-Path $build $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "project artifact is missing: $name" }
    [ordered]@{ name=$name; bytes=(Get-Item $path).Length;
        sha256=(Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$artifactManifestPath = Join-Path $environmentDirectory 'artifact-manifest.json'
[ordered]@{ schema_version='S10-ARTIFACT-MANIFEST-v1'; artifacts=$artifactFiles } |
    ConvertTo-Json -Depth 5 -Compress | Set-Content -LiteralPath $artifactManifestPath -Encoding utf8
$artifactManifestHash = (Get-FileHash $artifactManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$benchmarkHash = ($artifactFiles | Where-Object { $_.name -eq 's10_sift1m.exe' }).sha256
function Get-S10ArtifactSnapshot {
    return @($artifactNames | ForEach-Object {
        $name = $_
        $path = Join-Path $build $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "project artifact disappeared: $name" }
        [ordered]@{ name=$name; bytes=(Get-Item -LiteralPath $path).Length;
            sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}
function Assert-S10RuntimeArtifacts {
    param([string]$Phase)
    Assert-S10IdentityEqual $initialIdentity.artifacts (Get-S10ArtifactSnapshot) $Phase
}
function Get-S10IdentitySnapshot {
    $currentRevision = (& git -C $root rev-parse HEAD).Trim()
    $currentTree = @(& git -C $root status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0) { throw 'cannot revalidate Git identity' }
    $currentTreeState = if ($currentTree.Count -eq 0) { 'clean' } else { 'dirty' }
    return [ordered]@{
        revision=$currentRevision; working_tree_state=$currentTreeState
        source_identity_sha256=(Get-FileHash -LiteralPath $sourceIdentityPath -Algorithm SHA256).Hash.ToLowerInvariant()
        config_sha256=(Get-FileHash -LiteralPath $config -Algorithm SHA256).Hash.ToLowerInvariant()
        base_sha256=(Get-FileHash -LiteralPath $base -Algorithm SHA256).Hash.ToLowerInvariant()
        query_sha256=(Get-FileHash -LiteralPath $query -Algorithm SHA256).Hash.ToLowerInvariant()
        numerical_build_identity_sha256=(Get-FileHash -LiteralPath $numericalIdentityPath -Algorithm SHA256).Hash.ToLowerInvariant()
        artifacts=(Get-S10ArtifactSnapshot)
    }
}
$initialIdentity = Get-S10IdentitySnapshot
if ($initialIdentity.revision -ne $ExpectedRevision -or $initialIdentity.working_tree_state -ne 'clean') {
    throw 'initial S10 identity snapshot is not the expected clean revision'
}

if ($Mode -eq 'Final') {
    if (-not (Test-Path -LiteralPath $CorrectnessReference -PathType Leaf)) {
        throw 'matching S10 correctness evidence is required'
    }
    $gate = Get-Content -Raw -LiteralPath $CorrectnessReference | ConvertFrom-Json
    if ($gate.schema_version -ne 'S10-CORRECTNESS-GATE-v2' -or
        $gate.status -ne 'passed-clean-candidate' -or
        $gate.revision -ne $ExpectedRevision -or
        $gate.working_tree_state -ne 'clean' -or
        $gate.source_manifest_sha256 -ne $sourceManifestHash -or
        $gate.experiment_config_sha256 -ne $configHash -or
        $gate.sift_base_sha256 -ne $baseHash -or
        $gate.sift_query_sha256 -ne $queryHash -or
        $gate.cuda_architecture -ne $cudaArchitecture -or
        $gate.build_flags -ne $buildFlags -or
        $gate.benchmark_binary_sha256 -ne $benchmarkHash -or
        $gate.artifact_manifest_sha256 -ne $artifactManifestHash -or
        $gate.numerical_build_identity_sha256 -ne $numericalBuildIdentityHash -or
        $gate.required_cases -ne 20 -or $gate.accepted_cases -ne 20 -or
        $gate.failed_cases -ne 0 -or $gate.blocked_cases -ne 0 -or
        $gate.v2_block_size -ne 256 -or $gate.v3_block_size -ne 256 -or
        -not $gate.performance_authorized) {
        throw 'S10 correctness gate does not qualify the final measurement run'
    }
    $ledgerPath = Join-Path (Split-Path -Parent $CorrectnessReference) 'correctness-cases.jsonl'
    if (-not (Test-Path -LiteralPath $ledgerPath -PathType Leaf) -or
        (Get-FileHash $ledgerPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $gate.correctness_ledger_sha256) {
        throw 'S10 correctness ledger is missing or does not match its gate'
    }
}

$timestamp = [DateTimeOffset]::Now.ToString('o')
$provenanceFiles = @(
    [ordered]@{ name = 'sift_base.fvecs'; bytes = $baseBytes; sha256 = $baseHash },
    [ordered]@{ name = 'sift_query.fvecs'; bytes = $queryBytes; sha256 = $queryHash }
)
foreach ($optionalName in @('sift_learn.fvecs', 'sift_groundtruth.ivecs', 'sift.tar.gz')) {
    $optionalPath = Join-Path $dataset $optionalName
    if (Test-Path -LiteralPath $optionalPath -PathType Leaf) {
        $provenanceFiles += [ordered]@{
            name = $optionalName
            bytes = (Get-Item -LiteralPath $optionalPath).Length
            sha256 = (Get-FileHash -LiteralPath $optionalPath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
}
[ordered]@{
    schema_version = 'S10-DATASET-PROVENANCE-v1'
    dataset = 'INRIA TexMex ANN_SIFT1M'
    homepage = 'http://corpus-texmex.irisa.fr/'
    archive_url = 'ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz'
    captured_at = $timestamp
    hash_algorithm = 'SHA-256'
    redistribution = 'prohibited-by-project-policy'
    files = $provenanceFiles
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath `
    (Join-Path $provenanceDirectory 'dataset-manifest.json') -Encoding utf8
[ordered]@{
    schema_version = 'S10-HOST-ENVIRONMENT-v1'
    captured_at = $timestamp
    revision = $actualRevision
    working_tree_state = 'clean'
    operating_system = [Environment]::OSVersion.VersionString
    processor = [Environment]::GetEnvironmentVariable('PROCESSOR_IDENTIFIER')
    machine_name = [Environment]::MachineName
    powershell = $PSVersionTable.PSVersion.ToString()
    compiler_id = $compilerId
    compiler_version = $compilerVersion
    compiler_path = $compilerPath
    cuda_compiler_version = $cudaCompilerVersion
    cuda_compiler_path = $cudaCompilerPath
    cuda_toolkit_version = $cudaToolkitVersion
    build_type = $buildType
    cuda_architecture = $cudaArchitecture
    build_flags = $buildFlags
    source_manifest_sha256 = $sourceManifestHash
    numerical_build_identity_sha256 = $numericalBuildIdentityHash
    artifact_manifest_sha256 = $artifactManifestHash
    benchmark_binary_sha256 = $benchmarkHash
    config_sha256 = $configHash
    sift_base_sha256 = $baseHash
    sift_query_sha256 = $queryHash
} | ConvertTo-Json | Set-Content -LiteralPath `
    (Join-Path $environmentDirectory 'host-environment.json') -Encoding utf8
$MyInvocation.Line | Set-Content -LiteralPath `
    (Join-Path $environmentDirectory 'orchestration-command.txt') -Encoding utf8
Copy-Item -LiteralPath $cachePath -Destination (Join-Path $environmentDirectory 'CMakeCache.txt') -Force
Copy-Item -LiteralPath $compileCommandsPath -Destination (Join-Path $environmentDirectory 'compile_commands.json') -Force

$arguments = @(
    $(if ($Mode -eq 'Correctness') { '--correctness' } else { '--run' }),
    $OutputDirectory,
    '--experiment-id', 'S10-SIFT1M-PUBLIC-DATASET-v1',
    '--experiment-config', $config,
    '--config-sha256', $configHash,
    '--expected-revision', $ExpectedRevision,
    '--dataset-root', $dataset,
    '--base-sha256', $baseHash,
    '--query-sha256', $queryHash,
    '--captured-at', $timestamp,
    '--timezone', 'SE-Asia-Standard-Time',
    '--cuda-architecture', $cudaArchitecture,
    '--build-flags', $buildFlags,
    '--benchmark-binary-sha256', $benchmarkHash,
    '--artifact-manifest-sha256', $artifactManifestHash,
    '--numerical-build-identity-sha256', $numericalBuildIdentityHash
)

if ($Mode -eq 'Correctness') {
    $cpuManifest = Join-Path $build 'evidence/s6_cpu_matrix_tests.manifest.txt'
    $fixtureEvidence = Join-Path $build 'evidence/s6_cpu_fixtures.status.txt'
    $arguments += @('--cpu-manifest', $cpuManifest, '--fixture-evidence', $fixtureEvidence)
    foreach ($variant in 0..3) {
        $binary = Join-Path $build "evidence/cuda-v$variant/binary.manifest.txt"
        $functional = Join-Path $build "evidence/cuda-v$variant/functional.txt"
        if (-not (Test-Path -LiteralPath $binary -PathType Leaf) -or
            -not (Test-Path -LiteralPath $functional -PathType Leaf)) {
            throw "current-build CUDA V$variant binary/functional evidence is missing"
        }
        $arguments += @("--cuda-v$variant-binary", $binary,
                        "--cuda-v$variant-functional", $functional)
    }
    if (-not (Test-Path -LiteralPath $cpuManifest -PathType Leaf) -or
        -not (Test-Path -LiteralPath $fixtureEvidence -PathType Leaf)) {
        throw 'current-build CPU build/fixture evidence is missing'
    }
    Assert-S10IdentityEqual $initialIdentity (Get-S10IdentitySnapshot) 'immediately before correctness launch'
    Assert-S10RuntimeArtifacts 'at correctness process launch'
    & $benchmark @arguments
    if ($LASTEXITCODE -ne 0) { throw 'S10 correctness execution failed' }
    Assert-S10RuntimeArtifacts 'at correctness process completion'
    $postIdentity = Get-S10IdentitySnapshot
    Assert-S10IdentityEqual $initialIdentity $postIdentity 'after correctness execution'
    [ordered]@{ schema_version='S10-POST-RUN-ARTIFACT-MANIFEST-v1'; artifacts=$postIdentity.artifacts } |
        ConvertTo-Json -Depth 5 -Compress | Set-Content -LiteralPath `
        (Join-Path $environmentDirectory 'artifact-post-manifest.json') -Encoding utf8
    return
}

$arguments += @('--correctness-reference', $CorrectnessReference)
$nvidiaSmi = Get-Command nvidia-smi -ErrorAction Stop
function Get-S10GpuSnapshot {
    param([string]$XmlPath, [string]$CapturedAt)
    [xml]$xml = Get-Content -Raw -LiteralPath $XmlPath
    $device = @($xml.nvidia_smi_log.gpu)[0]
    if ($null -eq $device) { throw 'nvidia-smi XML contains no GPU' }
    $processes = @($device.processes.process_info | ForEach-Object {
        [ordered]@{ pid=[string]$_.pid; process_name=[string]$_.process_name }
    })
    return [ordered]@{ schema_version='S10-GPU-SNAPSHOT-v1'; captured_at=$CapturedAt;
        xml_sha256=(Get-FileHash -LiteralPath $XmlPath -Algorithm SHA256).Hash.ToLowerInvariant();
        driver_version=[string]$xml.nvidia_smi_log.driver_version;
        gpu_name=[string]$device.product_name; gpu_uuid=[string]$device.uuid;
        pstate=[string]$device.performance_state; temperature_gpu=[string]$device.temperature.gpu_temp;
        clock_sm=[string]$device.clocks.sm_clock; clock_memory=[string]$device.clocks.mem_clock;
        power_draw=[string]$device.gpu_power_readings.instant_power_draw;
        power_limit=[string]$device.gpu_power_readings.current_power_limit;
        hardware_thermal_slowdown=[string]$device.clocks_event_reasons.clocks_event_reason_hw_thermal_slowdown;
        processes=$processes }
}
$beforeXmlPath = Join-Path $environmentDirectory 'nvidia-smi-before.xml'
& $nvidiaSmi.Source -q -x | Set-Content -LiteralPath $beforeXmlPath -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw 'nvidia-smi baseline capture failed' }
$beforeSnapshot = Get-S10GpuSnapshot $beforeXmlPath ([DateTimeOffset]::UtcNow.ToString('o'))
$beforeSnapshot | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath `
    (Join-Path $environmentDirectory 'gpu-before.json') -Encoding utf8
$gpuName = $beforeSnapshot.gpu_name
$gpuUuid = $beforeSnapshot.gpu_uuid
$driverVersion = $beforeSnapshot.driver_version
if ([string]::IsNullOrWhiteSpace($gpuName) -or [string]::IsNullOrWhiteSpace($gpuUuid) -or
    [string]::IsNullOrWhiteSpace($driverVersion)) { throw 'GPU/driver identity is incomplete' }
$hostPath = Join-Path $environmentDirectory 'host-environment.json'
$hostEnvironment = Get-Content -Raw -LiteralPath $hostPath | ConvertFrom-Json
$hostEnvironment | Add-Member -NotePropertyName gpu_name -NotePropertyValue $gpuName -Force
$hostEnvironment | Add-Member -NotePropertyName gpu_uuid -NotePropertyValue $gpuUuid -Force
$hostEnvironment | Add-Member -NotePropertyName driver_version -NotePropertyValue $driverVersion -Force
$hostEnvironment | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $hostPath -Encoding utf8
$processQuery = '--query-compute-apps=timestamp,gpu_uuid,pid,process_name,used_gpu_memory'
$baselineProcessOutput = @(& $nvidiaSmi.Source $processQuery '--format=csv,noheader,nounits' 2>&1)
$baselineProcessExitCode = $LASTEXITCODE
$baselineProcessRows = @($baselineProcessOutput | Where-Object {
    -not [string]::IsNullOrWhiteSpace([string]$_)
})
$unsupportedProcessTelemetry = @($baselineProcessRows | Where-Object {
    [string]$_ -match '(?i)n/a|not supported|unsupported|not a valid field'
}).Count -ne 0
if ($baselineProcessExitCode -ne 0 -and -not $unsupportedProcessTelemetry) {
    throw 'nvidia-smi baseline process query failed'
}
$processTelemetrySupported = -not $unsupportedProcessTelemetry
if ($processTelemetrySupported) {
    foreach ($processRow in $baselineProcessRows) {
        $parts = [string]$processRow -split ',\s*', 5
        if ($parts.Count -ne 5) { throw 'malformed nvidia-smi baseline process row' }
    }
    if ($baselineProcessRows.Count -ne 0) {
        throw "unauthorized GPU compute process present before measurement: $($baselineProcessRows -join ', ')"
    }
}
$telemetryDirectory = Join-Path $environmentDirectory 'telemetry'
$telemetryStop = Join-Path $environmentDirectory 'stop-telemetry.signal'
$telemetryScript = Join-Path $PSScriptRoot 'capture-s7-telemetry.ps1'
$pwsh = (Get-Process -Id $PID).Path
$quote = { param([string]$Value) '"' + $Value.Replace('"', '\"') + '"' }
$telemetryArgs = '-NoProfile -File {0} -NvidiaSmi {1} -OutputDirectory {2} ' +
    '-StopSignal {3} -IntervalMilliseconds 1000'
$telemetryArgs = $telemetryArgs -f (& $quote $telemetryScript),
    (& $quote $nvidiaSmi.Source), (& $quote $telemetryDirectory),
    (& $quote $telemetryStop)
$telemetryProcess = Start-Process -FilePath $pwsh -ArgumentList $telemetryArgs `
    -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 1250
if ($telemetryProcess.HasExited) { throw 'telemetry helper failed during startup' }
$benchmarkStartedAt = $null
$benchmarkEndedAt = $null
$benchmarkProcess = $null
$benchmarkPid = $null
$benchmarkExitCode = $null
$telemetryExitedBeforeEnd = $false
$telemetryExitedBeforeStopSignal = $false
try {
    Assert-S10IdentityEqual $initialIdentity (Get-S10IdentitySnapshot) 'immediately before performance launch'
    Assert-S10RuntimeArtifacts 'at performance process launch'
    $benchmarkStartedAt = [DateTimeOffset]::UtcNow
    $benchmarkStartInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $benchmarkStartInfo.FileName = $benchmark
    $benchmarkStartInfo.UseShellExecute = $false
    foreach ($argument in $arguments) {
        [void]$benchmarkStartInfo.ArgumentList.Add([string]$argument)
    }
    $benchmarkProcess = [System.Diagnostics.Process]::new()
    $benchmarkProcess.StartInfo = $benchmarkStartInfo
    if (-not $benchmarkProcess.Start()) { throw 'S10 benchmark process failed to start' }
    $benchmarkPid = $benchmarkProcess.Id
    $benchmarkProcess.WaitForExit()
    $benchmarkExitCode = $benchmarkProcess.ExitCode
    $benchmarkEndedAt = [DateTimeOffset]::UtcNow
    Assert-S10RuntimeArtifacts 'at performance process completion'
    $telemetryExitedBeforeEnd = $telemetryProcess.HasExited
    if ($benchmarkExitCode -ne 0) {
        throw "S10 benchmark execution failed with exit code $benchmarkExitCode"
    }
    if (-not $telemetryExitedBeforeEnd) {
        Start-Sleep -Milliseconds 1250
        $telemetryExitedBeforeStopSignal = $telemetryProcess.HasExited
    }
} finally {
    New-Item -ItemType File -Force -Path $telemetryStop | Out-Null
    if (-not $telemetryProcess.WaitForExit(15000)) {
        Stop-Process -Id $telemetryProcess.Id -Force
        $telemetryProcess.WaitForExit()
    }
}
if ($telemetryExitedBeforeEnd -or $telemetryExitedBeforeStopSignal -or $telemetryProcess.ExitCode -ne 0) {
    throw 'telemetry helper terminated early or returned failure'
}
$postIdentity = Get-S10IdentitySnapshot
Assert-S10IdentityEqual $initialIdentity $postIdentity 'after performance execution'
[ordered]@{ schema_version='S10-POST-RUN-ARTIFACT-MANIFEST-v1'; artifacts=$postIdentity.artifacts } |
    ConvertTo-Json -Depth 5 -Compress | Set-Content -LiteralPath `
    (Join-Path $environmentDirectory 'artifact-post-manifest.json') -Encoding utf8
[ordered]@{ schema_version='S10-BENCHMARK-INTERVAL-v1';
    benchmark_started_at=$benchmarkStartedAt.ToString('o'); benchmark_ended_at=$benchmarkEndedAt.ToString('o');
    benchmark_pid=$benchmarkPid; benchmark_exit_code=$benchmarkExitCode;
    telemetry_pid=$telemetryProcess.Id; telemetry_exit_code=$telemetryProcess.ExitCode;
    telemetry_exited_before_benchmark_end=$telemetryExitedBeforeEnd;
    telemetry_exited_before_stop_signal=$telemetryExitedBeforeStopSignal;
    process_telemetry_status=$(if ($processTelemetrySupported) { 'supported' } else { 'unsupported' });
    maximum_sampling_gap_ms=$S10SamplingGapPolicyMilliseconds } |
    ConvertTo-Json -Compress | Set-Content -LiteralPath `
    (Join-Path $environmentDirectory 'benchmark-interval.json') -Encoding utf8
$afterXmlPath = Join-Path $environmentDirectory 'nvidia-smi-after.xml'
& $nvidiaSmi.Source -q -x | Set-Content -LiteralPath $afterXmlPath -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw 'nvidia-smi post-run capture failed' }
$afterSnapshot = Get-S10GpuSnapshot $afterXmlPath ([DateTimeOffset]::UtcNow.ToString('o'))
if ($afterSnapshot.gpu_uuid -ne $gpuUuid -or $afterSnapshot.driver_version -ne $driverVersion) {
    throw 'GPU UUID or driver changed during measurement'
}
$afterSnapshot | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath `
    (Join-Path $environmentDirectory 'gpu-after.json') -Encoding utf8
$telemetryErrors = @(Get-Content -LiteralPath `
    (Join-Path $telemetryDirectory 'telemetry-errors.log') | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_)
    })
if ($processTelemetrySupported -and $telemetryErrors.Count -ne 0) {
    throw 'telemetry capture reported errors'
}
if (-not $processTelemetrySupported -and
    @($telemetryErrors | Where-Object { $_ -notmatch '(?i)GPU process telemetry|process telemetry query failed' }).Count -ne 0) {
    throw 'telemetry capture reported non-process errors'
}
& node $analyzer --final $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'S10 analysis or telemetry acceptance failed' }
