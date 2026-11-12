param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('SelfTest','Correctness','Preflight','Formal')]
    [string]$Mode,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$DatasetRoot,
    [string]$ExpectedRevision,
    [string]$ExperimentId = 'S15-PUBLIC-SIFT1M',
    [string]$CorrectnessReference,
    [string]$DatasetAcquiredAt,
    [string]$DatasetAcquisitionMethod,
    [string]$DatasetExtractionProcedure,
    [string]$DatasetSourceUrl = 'ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz',
    [switch]$FormalBenchmarkAuthorization
)

$ErrorActionPreference = 'Stop'
$ApprovedBaseHash = '21f66e2975057b5728ba56de1c825bac4f4d89d596609ae985741c6242631816'
$ApprovedQueryHash = 'f7fc9be140accdfd64116c2fa2365ecdb69b8f084970c6b0532db5ff79ac8fdc'
$ApprovedQueries = @(0,322,645,967,1290,1612,1935,2257,2580,2902,3225,3548,
    3870,4193,4515,4838,5160,5483,5805,6128,6450,6773,7096,7418,7741,
    8063,8386,8708,9031,9353,9676,9999)

function Assert-S15FreshOutput([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        throw "S15 output directory must be fresh and absent: $Path"
    }
}

function Assert-S15CompileCommand([string]$Command, [bool]$Cuda, [string]$Label) {
    if ($Command -match '(?i)--use_fast_math|-ffast-math|/fp:fast|--ftz=true|--prec-div=false|--prec-sqrt=false') {
        throw "unexpected relaxed or fast-math flag for $Label"
    }
    if ($Cuda) {
        foreach ($flag in @('--ftz=false','--fmad=true','--prec-div=true',
                '--prec-sqrt=true','-Xcompiler=/fp:strict')) {
            if (-not $Command.Contains($flag)) {
                throw "required CUDA numerical flag $flag is missing for $Label"
            }
        }
    } elseif (-not $Command.Contains('/fp:strict')) {
        throw "required CPU /fp:strict flag is missing for $Label"
    }
}

function Assert-S15Config($Config) {
    if ($Config.schema_version -ne 'S15-PUBLIC-SIFT1M-CONFIG-v1' -or
            $Config.approved_design.synchronized_sha256 -ne '80b3984924d53f83323e85706d8a3138ce047664ec4d8b5649c2efd6393747df' -or
            $Config.workload.dimension -ne 128 -or
            $Config.workload.base_file.bytes -ne 516000000 -or
            $Config.workload.base_file.sha256 -ne $ApprovedBaseHash -or
            $Config.workload.query_file.bytes -ne 5160000 -or
            $Config.workload.query_file.sha256 -ne $ApprovedQueryHash -or
            $Config.sampling.warmups -ne 10 -or
            $Config.sampling.measured_repetitions -ne 100 -or
            $Config.environment_policy.telemetry_interval_ms -ne 1000 -or
            $Config.environment_policy.maximum_accepted_start_gap_ms -ne 3000 -or
            $Config.environment_policy.telemetry_query_timeout_ms -ne 900 -or
            $Config.fast_math -ne 'disabled') {
        throw 'S15 configuration contract mismatch'
    }
    $actualQueries = @($Config.workload.query_ordinals)
    if ($actualQueries.Count -ne 32) { throw 'S15 query list count mismatch' }
    for ($index = 0; $index -lt 32; ++$index) {
        $formula = [math]::Floor($index * 9999 / 31)
        if ($actualQueries[$index] -ne $ApprovedQueries[$index] -or
                $actualQueries[$index] -ne $formula) {
            throw 'S15 query formula/list mismatch'
        }
    }
    $expectedBlocks = [ordered]@{'1024'=256;'16384'=128;'262144'=512;'1000000'=256}
    foreach ($entry in $expectedBlocks.GetEnumerator()) {
        if ($Config.v2_block_mapping.($entry.Key) -ne $entry.Value) {
            throw "S15 V2 block mapping mismatch for N=$($entry.Key)"
        }
    }
}

function Write-S15Json([string]$Path, $Value) {
    $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding utf8
}

function ConvertTo-S15DisplayArgument([string]$Value) {
    return "'" + $Value.Replace("'", "''") + "'"
}

function ConvertTo-S15NativeArgumentLine([object[]]$Values) {
    return (($Values | ForEach-Object {
        '"' + ([string]$_).Replace('"','\"') + '"'
    }) -join ' ')
}

if ($Mode -eq 'SelfTest') {
    Assert-S15CompileCommand 'cl /O2 /fp:strict source.cpp' $false 'cpu-valid'
    Assert-S15CompileCommand 'nvcc --ftz=false --fmad=true --prec-div=true --prec-sqrt=true -Xcompiler=/fp:strict source.cu' $true 'cuda-valid'
    foreach ($invalid in @('cl /fp:fast source.cpp','cl /O2 source.cpp',
            'nvcc --use_fast_math source.cu','nvcc --ftz=true --fmad=true --prec-div=true --prec-sqrt=true -Xcompiler=/fp:strict source.cu')) {
        $rejected = $false
        try { Assert-S15CompileCommand $invalid $invalid.StartsWith('nvcc') 'negative' }
        catch { $rejected = $true }
        if (-not $rejected) { throw "compile-policy corruption was accepted: $invalid" }
    }
    $root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
    $config = Get-Content -Raw -LiteralPath (Join-Path $root 'benchmarks/config/s15-public-sift1m.json') | ConvertFrom-Json
    Assert-S15Config $config
    $config.workload.query_ordinals[1] = 323
    $rejected = $false
    try { Assert-S15Config $config } catch { $rejected = $true }
    if (-not $rejected) { throw 'query-selection corruption was accepted' }
    $collision = [IO.Path]::GetTempFileName()
    try {
        $rejected = $false
        try { Assert-S15FreshOutput $collision } catch { $rejected = $true }
        if (-not $rejected) { throw 'stale output collision was accepted' }
    } finally {
        Remove-Item -LiteralPath $collision -Force -ErrorAction SilentlyContinue
    }
    if ($FormalBenchmarkAuthorization) {
        throw 'FormalBenchmarkAuthorization is invalid in SelfTest mode'
    }
    Write-Output 'S15 orchestration self-test: passed'
    return
}

foreach ($value in @($BuildDirectory,$OutputDirectory,$DatasetRoot,$ExpectedRevision)) {
    if ([string]::IsNullOrWhiteSpace($value)) {
        throw 'normal S15 modes require BuildDirectory, OutputDirectory, DatasetRoot, and ExpectedRevision'
    }
}
foreach ($provenance in @($DatasetAcquiredAt,$DatasetAcquisitionMethod,
        $DatasetExtractionProcedure,$DatasetSourceUrl)) {
    if ([string]::IsNullOrWhiteSpace($provenance)) {
        throw 'normal S15 modes require complete owner-supplied dataset acquisition provenance'
    }
}
$parsedAcquiredAt = [DateTimeOffset]::MinValue
if (-not [DateTimeOffset]::TryParse($DatasetAcquiredAt,[ref]$parsedAcquiredAt)) {
    throw 'DatasetAcquiredAt must be a valid timestamp with provenance meaning supplied by the Owner'
}
if ($Mode -eq 'Formal' -and -not $FormalBenchmarkAuthorization) {
    throw 'Formal S15 execution requires separate Owner authorization and -FormalBenchmarkAuthorization'
}
if ($Mode -ne 'Formal' -and $FormalBenchmarkAuthorization) {
    throw 'FormalBenchmarkAuthorization is accepted only in Formal mode'
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$dataset = (Resolve-Path -LiteralPath $DatasetRoot).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
Assert-S15FreshOutput $output
$relativeDataset = [IO.Path]::GetRelativePath($root,$dataset)
if ($relativeDataset -eq '.' -or
        ((-not $relativeDataset.StartsWith('..' + [IO.Path]::DirectorySeparatorChar)) -and
         (-not [IO.Path]::IsPathRooted($relativeDataset)))) {
    throw 'DatasetRoot must remain outside the repository'
}
$relativeOutput = [IO.Path]::GetRelativePath($root,$output)
if ($relativeOutput -eq '.' -or
        ((-not $relativeOutput.StartsWith('..' + [IO.Path]::DirectorySeparatorChar)) -and
         (-not [IO.Path]::IsPathRooted($relativeOutput)))) {
    & git -C $root check-ignore --quiet -- $relativeOutput
    if ($LASTEXITCODE -ne 0) { throw 'repository-contained S15 output must be ignored' }
}

$actualRevision = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualRevision -ne $ExpectedRevision) {
    throw 'repository revision does not match ExpectedRevision'
}
$tree = @(& git -C $root status --porcelain=v1 --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or $tree.Count -ne 0) {
    throw 'S15 correctness, preflight, and formal evidence require a clean worktree'
}

$configPath = Join-Path $root 'benchmarks/config/s15-public-sift1m.json'
$designPath = Join-Path $root 'docs/design/S15PublicSift1mBenchmarkDesignDecision-v1.0.md'
$approvalPath = Join-Path $root 'docs/governance/S15PublicSift1mBenchmarkApprovalRecord.md'
$policyPath = Join-Path $root 'docs/specifications/S6-CONSOLIDATED-NUMERICAL-POLICY-v1.0.md'
$benchmark = Join-Path $build 's15_public_sift1m_benchmark.exe'
$analyzer = Join-Path $root 'scripts/analyze-s15-public-sift1m.mjs'
$collector = Join-Path $root 'scripts/capture-s14-telemetry.ps1'
$baseFile = Join-Path $dataset 'sift_base.fvecs'
$queryFile = Join-Path $dataset 'sift_query.fvecs'
$cachePath = Join-Path $build 'CMakeCache.txt'
$compileCommandsPath = Join-Path $build 'compile_commands.json'
$sourceIdentityPath = Join-Path $build 'generated/s6/source_identity.txt'
foreach ($required in @($configPath,$designPath,$approvalPath,$policyPath,$benchmark,$analyzer,
        $collector,$baseFile,$queryFile,$cachePath,$compileCommandsPath,$sourceIdentityPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "required S15 input is missing: $required"
    }
}
$config = Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json
Assert-S15Config $config
$configHash = (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ((Get-Item -LiteralPath $baseFile).Length -ne 516000000 -or
        (Get-Item -LiteralPath $queryFile).Length -ne 5160000) {
    throw 'SIFT1M extracted-file byte count mismatch'
}
$baseHash = (Get-FileHash -LiteralPath $baseFile -Algorithm SHA256).Hash.ToLowerInvariant()
$queryHash = (Get-FileHash -LiteralPath $queryFile -Algorithm SHA256).Hash.ToLowerInvariant()
if ($baseHash -ne $ApprovedBaseHash -or $queryHash -ne $ApprovedQueryHash) {
    throw 'SIFT1M extracted-file SHA-256 mismatch'
}

$cache = Get-Content -LiteralPath $cachePath
$cudaArchitecture = (($cache | Where-Object { $_ -match '^CMAKE_CUDA_ARCHITECTURES:' }) -split '=',2)[1]
$buildType = (($cache | Where-Object { $_ -match '^CMAKE_BUILD_TYPE:' }) -split '=',2)[1]
if ([string]::IsNullOrWhiteSpace($cudaArchitecture) -or $buildType -ne 'Release') {
    throw 'S15 requires Release and an explicit CUDA architecture'
}
$compileRecords = Get-Content -Raw -LiteralPath $compileCommandsPath | ConvertFrom-Json
$requiredSources = @('src/cpu_baseline.cpp','src/cpu_evidence.cpp','src/cuda_v2.cu',
    'benchmarks/s10_sift1m_loader.cpp','benchmarks/s15_public_sift1m_benchmark.cpp')
$effectiveCommands = @()
foreach ($source in $requiredSources) {
    $record = $compileRecords | Where-Object { $_.file.Replace('\','/').EndsWith($source) } | Select-Object -First 1
    if (-not $record) { throw "compile command missing for $source" }
    $command = [string]$record.command
    Assert-S15CompileCommand $command $source.EndsWith('.cu') $source
    $effectiveCommands += [ordered]@{source=$source;command=$command}
}
$buildFlags = 'Release;fp=strict;cuda=ftz-false,fmad-true,prec-div=true,prec-sqrt=true;fast-math=disabled'

New-Item -ItemType Directory -Path $output | Out-Null
$capture = Join-Path $output 'environment-capture'
$telemetry = Join-Path $capture 'telemetry'
$results = if ($Mode -eq 'Correctness') { Join-Path $output 'correctness' }
    elseif ($Mode -eq 'Preflight') { Join-Path $output 'preflight-non-performance' }
    else { Join-Path $output 'formal-results' }
New-Item -ItemType Directory -Path $capture | Out-Null
Copy-Item -LiteralPath $configPath -Destination (Join-Path $capture 'experiment-config.json')
Copy-Item -LiteralPath $designPath -Destination (Join-Path $capture 'approved-design.md')
Copy-Item -LiteralPath $approvalPath -Destination (Join-Path $capture 'design-approval-record.md')
Copy-Item -LiteralPath $policyPath -Destination (Join-Path $capture 'numerical-policy.md')
Copy-Item -LiteralPath $cachePath -Destination (Join-Path $capture 'CMakeCache.txt')
Copy-Item -LiteralPath $compileCommandsPath -Destination (Join-Path $capture 'compile_commands.json')
$supplementaryFiles = @()
foreach ($name in @('sift.tar.gz','sift_groundtruth.ivecs','sift_learn.fvecs')) {
    $candidate = Join-Path $dataset $name
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $supplementaryFiles += [ordered]@{name=$name;bytes=(Get-Item $candidate).Length;
            sha256=(Get-FileHash $candidate -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
}
Write-S15Json (Join-Path $capture 'dataset-provenance.json') ([ordered]@{
    schema_version='S15-DATASET-PROVENANCE-v1';dataset='INRIA TexMex ANN_SIFT1M';
    source_url=$DatasetSourceUrl;acquired_at=$DatasetAcquiredAt;
    acquisition_method=$DatasetAcquisitionMethod;extraction_procedure=$DatasetExtractionProcedure;
    base_file=[ordered]@{name='sift_base.fvecs';bytes=516000000;sha256=$baseHash};
    query_file=[ordered]@{name='sift_query.fvecs';bytes=5160000;sha256=$queryHash};
    supplementary_files=$supplementaryFiles;dataset_bytes_redistributed=$false})
$policyHash = (Get-FileHash -LiteralPath $policyPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($policyHash -ne '503aa6b2c97567d814a9316fb0f2f9890d5472aea2bcf1f928ac505c29d1aa39') {
    throw 'approved numerical policy SHA-256 mismatch'
}
$designHash = (Get-FileHash -LiteralPath $designPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($designHash -ne $config.approved_design.synchronized_sha256) {
    throw 'synchronized S15 design SHA-256 mismatch'
}
Write-S15Json (Join-Path $capture 'authority-identities.json') ([ordered]@{
    schema_version='S15-AUTHORITY-IDENTITIES-v1';
    approved_candidate_design_sha256='4d91b8a659b94100654077988491a4a1b00a1405c067bfad508324bd267e0b16';
    synchronized_design_sha256=$designHash;
    approval_record_sha256=(Get-FileHash $approvalPath -Algorithm SHA256).Hash.ToLowerInvariant();
    numerical_policy_sha256=$policyHash;config_sha256=$configHash})

$prerequisiteLog = Join-Path $capture 'prerequisite-ctest.log'
$prerequisiteJunit = Join-Path $capture 'prerequisite-ctest.xml'
$cmakeCommandLine = $cache | Where-Object { $_ -match '^CMAKE_COMMAND:' } | Select-Object -First 1
if (-not $cmakeCommandLine) { throw 'CMake command identity is missing from CMakeCache.txt' }
$cmakeCommand = ($cmakeCommandLine -split '=',2)[1]
$ctest = Join-Path (Split-Path -Parent $cmakeCommand) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) { throw 'ctest.exe could not be located' }
$ctestArguments = @('--test-dir',$build,'-C','Release','--output-on-failure',
    '--output-junit',$prerequisiteJunit,'-R',
    '^(s6\.cpu\.|s7\.cuda_v2\.(functional|required_matrix)|s10\.loader\.fixtures|s15\.)')
& $ctest @ctestArguments 2>&1 | Tee-Object -FilePath $prerequisiteLog
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $prerequisiteJunit)) {
    throw 'S15 prerequisite CTest set failed'
}
Write-S15Json (Join-Path $capture 'prerequisite-artifacts.json') ([ordered]@{
    schema_version='S15-PREREQUISITE-ARTIFACTS-v1'
    command=@($ctest)+$ctestArguments
    text_log=[ordered]@{bytes=(Get-Item $prerequisiteLog).Length;sha256=(Get-FileHash $prerequisiteLog -Algorithm SHA256).Hash.ToLowerInvariant()}
    junit=[ordered]@{bytes=(Get-Item $prerequisiteJunit).Length;sha256=(Get-FileHash $prerequisiteJunit -Algorithm SHA256).Hash.ToLowerInvariant()}
})

$artifactNames = @('s15_public_sift1m_benchmark.exe','s10_sift1m_io.lib',
    's6_cpu_baseline.lib','s6_cpu_evidence.lib','s7_cuda_v2.lib')
$artifacts = foreach ($name in $artifactNames) {
    $artifact = Join-Path $build $name
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) { throw "missing artifact $name" }
    [ordered]@{name=$name;bytes=(Get-Item $artifact).Length;
        sha256=(Get-FileHash $artifact -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$artifactManifestPath = Join-Path $capture 'artifact-manifest.json'
Write-S15Json $artifactManifestPath ([ordered]@{schema_version='S15-ARTIFACT-MANIFEST-v1';artifacts=$artifacts})
$artifactManifestHash = (Get-FileHash $artifactManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$benchmarkHash = ($artifacts | Where-Object {$_.name -eq 's15_public_sift1m_benchmark.exe'}).sha256
function Get-S15IdentitySnapshot {
    $revision = (& git -C $root rev-parse HEAD).Trim()
    $status = @(& git -C $root status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0) { throw 'cannot revalidate S15 Git identity' }
    return [ordered]@{
        revision=$revision;working_tree_state=if($status.Count -eq 0){'clean'}else{'dirty'};
        config_sha256=(Get-FileHash $configPath -Algorithm SHA256).Hash.ToLowerInvariant();
        base_sha256=(Get-FileHash $baseFile -Algorithm SHA256).Hash.ToLowerInvariant();
        query_sha256=(Get-FileHash $queryFile -Algorithm SHA256).Hash.ToLowerInvariant();
        source_identity_sha256=(Get-FileHash $sourceIdentityPath -Algorithm SHA256).Hash.ToLowerInvariant();
        artifacts=@($artifactNames | ForEach-Object {
            $item=Join-Path $build $_
            [ordered]@{name=$_;bytes=(Get-Item $item).Length;
                sha256=(Get-FileHash $item -Algorithm SHA256).Hash.ToLowerInvariant()}
        })
    }
}
function Assert-S15IdentityUnchanged($Expected,[string]$Phase) {
    $actual=Get-S15IdentitySnapshot
    if (($Expected|ConvertTo-Json -Depth 8 -Compress) -cne
            ($actual|ConvertTo-Json -Depth 8 -Compress)) {
        throw "S15 revision/config/dataset/build identity drift detected $Phase"
    }
}
$initialIdentity=Get-S15IdentitySnapshot
if ($initialIdentity.revision -ne $ExpectedRevision -or
        $initialIdentity.working_tree_state -ne 'clean') {
    throw 'initial S15 identity snapshot is not the expected clean revision'
}
$numericalPath = Join-Path $capture 'numerical-build-identity.json'
Write-S15Json $numericalPath ([ordered]@{schema_version='S15-NUMERICAL-BUILD-IDENTITY-v1';
    revision=$ExpectedRevision;build_type=$buildType;cuda_architecture=$cudaArchitecture;
    build_flags=$buildFlags;source_identity_sha256=(Get-FileHash $sourceIdentityPath -Algorithm SHA256).Hash.ToLowerInvariant();
    compile_commands=$effectiveCommands})
$numericalHash = (Get-FileHash $numericalPath -Algorithm SHA256).Hash.ToLowerInvariant()
$timestamp = [DateTimeOffset]::Now.ToString('o')
$commonArguments = @('--experiment-id',$ExperimentId,'--experiment-config',$configPath,
    '--config-sha256',$configHash,'--expected-revision',$ExpectedRevision,
    '--dataset-root',$dataset,'--base-sha256',$baseHash,'--query-sha256',$queryHash,
    '--captured-at',$timestamp,'--timezone','SE-Asia-Standard-Time',
    '--cuda-architecture',$cudaArchitecture,'--build-flags',$buildFlags,
    '--benchmark-binary-sha256',$benchmarkHash,
    '--artifact-manifest-sha256',$artifactManifestHash,
    '--numerical-build-identity-sha256',$numericalHash)

if ($Mode -eq 'Correctness') {
    $cpuManifest = Join-Path $build 'evidence/s15_public_sift1m_benchmark.manifest.txt'
    $fixtures = Join-Path $build 'evidence/s6_cpu_fixtures.status.txt'
    $cudaBinary = Join-Path $build 'evidence/cuda-v2/binary.manifest.txt'
    $cudaFunctional = Join-Path $build 'evidence/cuda-v2/functional.txt'
    foreach ($required in @($cpuManifest,$fixtures,$cudaBinary,$cudaFunctional)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "missing matching correctness prerequisite: $required"
        }
    }
    $arguments = @('--correctness',$results)+$commonArguments+@('--cpu-manifest',$cpuManifest,
        '--fixture-evidence',$fixtures,'--cuda-v2-binary',$cudaBinary,
        '--cuda-v2-functional',$cudaFunctional)
    Write-S15Json (Join-Path $capture 'effective-invocation.json') ([ordered]@{
        schema_version='S15-EFFECTIVE-INVOCATION-v1';mode='Correctness';executable=$benchmark;arguments=$arguments})
    & $benchmark @arguments
    if ($LASTEXITCODE -ne 0) { throw 'S15 correctness harness failed' }
    Assert-S15IdentityUnchanged $initialIdentity 'after correctness'
    Copy-Item -LiteralPath $configPath -Destination (Join-Path $results 'experiment-config.json')
    Write-Output "S15 correctness evidence created at $results"
    return
}

if ($Mode -eq 'Formal') {
    if ([string]::IsNullOrWhiteSpace($CorrectnessReference)) {
        throw 'Formal mode requires CorrectnessReference'
    }
    $correctnessGate = (Resolve-Path -LiteralPath $CorrectnessReference).Path
    $correctnessLedger = Join-Path (Split-Path -Parent $correctnessGate) 'correctness-cases.jsonl'
    foreach ($required in @($correctnessGate,$correctnessLedger)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw 'matching S15 correctness gate/ledger is missing'
        }
    }
} else {
    $correctnessGate = ''
    $correctnessLedger = ''
}

$nvidiaSmi = (Get-Command nvidia-smi.exe -ErrorAction Stop).Source
$gpuSnapshot = & $nvidiaSmi --query-gpu=name,uuid,driver_version,memory.total --format=csv,noheader,nounits
if ($LASTEXITCODE -ne 0 -or -not $gpuSnapshot) { throw 'cannot capture NVIDIA GPU identity' }
Write-S15Json (Join-Path $capture 'environment.json') ([ordered]@{
    schema_version='S15-ENVIRONMENT-v1';experiment_id=$ExperimentId;revision=$ExpectedRevision;
    working_tree_state='clean';config_sha256=$configHash;gpu_snapshot=@($gpuSnapshot);
    os=[Environment]::OSVersion.VersionString;processor=$env:PROCESSOR_IDENTIFIER;captured_at=$timestamp})
$modeArgument = if ($Mode -eq 'Preflight') {'--preflight'} else {'--run'}
$arguments = @($modeArgument,$results)+$commonArguments
if ($Mode -eq 'Formal') { $arguments += @('--correctness-reference',$correctnessGate) }
Write-S15Json (Join-Path $capture 'effective-invocation.json') ([ordered]@{
    schema_version='S15-EFFECTIVE-INVOCATION-v1';mode=$Mode;executable=$benchmark;arguments=$arguments;
    human_readable=((ConvertTo-S15DisplayArgument $benchmark)+' '+
        (($arguments | ForEach-Object {ConvertTo-S15DisplayArgument ([string]$_)}) -join ' '))})

$stopSignal = Join-Path $capture 'telemetry.stop'
$telemetryStdout = Join-Path $capture 'telemetry.stdout.log'
$telemetryStderr = Join-Path $capture 'telemetry-launch-errors.log'
$collectorArguments = @('-NoProfile','-File',$collector,'-Mode','Capture','-NvidiaSmi',$nvidiaSmi,
    '-OutputDirectory',$telemetry,'-StopSignal',$stopSignal,'-IntervalMilliseconds','1000',
    '-QueryTimeoutMilliseconds','900')
$collectorProcess = Start-Process -FilePath (Get-Process -Id $PID).Path `
    -ArgumentList (ConvertTo-S15NativeArgumentLine $collectorArguments) `
    -PassThru -WindowStyle Hidden -RedirectStandardOutput $telemetryStdout -RedirectStandardError $telemetryStderr
$readyPath = Join-Path $telemetry 'telemetry-ready.json'
$readyDeadline = [DateTime]::UtcNow.AddSeconds(15)
while (-not (Test-Path -LiteralPath $readyPath) -and [DateTime]::UtcNow -lt $readyDeadline) {
    Start-Sleep -Milliseconds 100
    if ($collectorProcess.HasExited) { break }
}
if (-not (Test-Path -LiteralPath $readyPath)) {
    throw 'telemetry collector did not become ready'
}
$benchmarkStdout = Join-Path $capture 'benchmark.stdout.log'
$benchmarkStderr = Join-Path $capture 'benchmark.stderr.log'
$started = [DateTimeOffset]::Now
$benchmarkProcess = Start-Process -FilePath $benchmark `
    -ArgumentList (ConvertTo-S15NativeArgumentLine $arguments) -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $benchmarkStdout -RedirectStandardError $benchmarkStderr
$benchmarkProcess.WaitForExit()
$ended = [DateTimeOffset]::Now
$benchmarkExit = $benchmarkProcess.ExitCode
New-Item -ItemType File -Path $stopSignal | Out-Null
$collectorExitedBeforeStop = $collectorProcess.HasExited
if (-not $collectorProcess.WaitForExit(15000)) {
    $collectorProcess.Kill($true)
    $collectorProcess.WaitForExit()
    $forced = $true
} else { $forced = $false }
Write-S15Json (Join-Path $capture 'benchmark-interval.json') ([ordered]@{
    schema_version='S15-BENCHMARK-INTERVAL-v1';experiment_id=$ExperimentId;revision=$ExpectedRevision;
    benchmark_pid=$benchmarkProcess.Id;telemetry_pid=$collectorProcess.Id;
    benchmark_started_at=$started.ToString('o');benchmark_ended_at=$ended.ToString('o');
    benchmark_exit_code=$benchmarkExit;telemetry_exit_code=$collectorProcess.ExitCode;
    telemetry_interval_ms=1000;maximum_telemetry_gap_ms=3000;telemetry_query_timeout_ms=900;
    telemetry_exited_before_benchmark_end=$collectorExitedBeforeStop;
    telemetry_exited_before_stop_signal=$collectorExitedBeforeStop;telemetry_forced_termination=$forced})
if ($benchmarkExit -ne 0) { throw "S15 benchmark process failed with exit code $benchmarkExit" }
Assert-S15IdentityUnchanged $initialIdentity "after $Mode"

Copy-Item -LiteralPath $configPath -Destination (Join-Path $results 'experiment-config.json')
if ($Mode -eq 'Formal') {
    Copy-Item -LiteralPath $correctnessGate -Destination (Join-Path $results 'correctness-gate.json')
    Copy-Item -LiteralPath $correctnessLedger -Destination (Join-Path $results 'correctness-cases.jsonl')
    & node $analyzer $results
    if ($LASTEXITCODE -ne 0) { throw 'S15 analyzer rejected the formal evidence packet' }
    Write-Output "S15 formal evidence analyzed at $results"
} else {
    Write-Output "S15 bounded non-performance preflight created at $results"
}
