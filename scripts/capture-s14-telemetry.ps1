param(
    [ValidateSet('Capture', 'SelfTest')]
    [string]$Mode = 'Capture',
    [string]$NvidiaSmi,
    [string]$OutputDirectory,
    [string]$StopSignal,
    [ValidateRange(100, 60000)]
    [int]$IntervalMilliseconds = 1000,
    [ValidateRange(100, 2999)]
    [int]$QueryTimeoutMilliseconds = 900
)

$ErrorActionPreference = 'Stop'

function Write-JsonLine([string]$Path, [object]$Value) {
    $Value | ConvertTo-Json -Compress -Depth 6 |
        Add-Content -LiteralPath $Path -Encoding utf8
}

function Invoke-BoundedProcess([string]$FilePath, [string[]]$Arguments,
        [int]$TimeoutMilliseconds) {
    # The approved deadline begins at the subprocess start request. Cleanup is
    # evidence, but it is not part of a falsely successful normal completion.
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FilePath
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) {
        [void]$info.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    $started = $false
    $completed = $false
    $stdoutTask = $null
    $stderrTask = $null
    try {
        $started = $process.Start()
        if (-not $started) { throw "Failed to start $FilePath." }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $remaining = [Math]::Max(0, $TimeoutMilliseconds -
            [int][Math]::Ceiling($watch.Elapsed.TotalMilliseconds))
        $completed = $process.WaitForExit($remaining)
        if ($completed) {
            $remaining = [Math]::Max(0, $TimeoutMilliseconds -
                [int][Math]::Ceiling($watch.Elapsed.TotalMilliseconds))
            $completed = $stdoutTask.Wait($remaining)
            if ($completed) {
                $remaining = [Math]::Max(0, $TimeoutMilliseconds -
                    [int][Math]::Ceiling($watch.Elapsed.TotalMilliseconds))
                $completed = $stderrTask.Wait($remaining)
            }
        }
    } catch {
        if (-not $started) { throw }
        $completed = $false
    }
    $deadlineDuration = $watch.Elapsed.TotalMilliseconds
    $completed = $completed -and $deadlineDuration -le $TimeoutMilliseconds
    $cleanupDuration = 0.0
    $cleanupStatus = 'not-required'
    if (-not $completed) {
        $cleanupWatch = [Diagnostics.Stopwatch]::StartNew()
        $cleanupStatus = 'completed'
        try { if (-not $process.HasExited) { $process.Kill($true) } }
            catch { $cleanupStatus = 'failed' }
        try { $process.WaitForExit() } catch { $cleanupStatus = 'failed' }
        try { if ($null -ne $stdoutTask) { $stdoutTask.Wait() } }
            catch { $cleanupStatus = 'failed' }
        try { if ($null -ne $stderrTask) { $stderrTask.Wait() } }
            catch { $cleanupStatus = 'failed' }
        $cleanupDuration = $cleanupWatch.Elapsed.TotalMilliseconds
    }
    $stdout = if ($null -ne $stdoutTask -and $stdoutTask.IsCompletedSuccessfully) {
        $stdoutTask.Result
    } else { '' }
    $stderr = if ($null -ne $stderrTask -and $stderrTask.IsCompletedSuccessfully) {
        $stderrTask.Result
    } else { '' }
    [pscustomobject]@{
        status = if (-not $completed) { 'timeout' }
            elseif ($process.ExitCode -ne 0) { 'failed' } else { 'completed' }
        exit_code = if ($completed) { $process.ExitCode } else { $null }
        duration_ms = $deadlineDuration
        normal_completion_duration_ms = if ($completed) { $deadlineDuration }
            else { $null }
        cleanup_duration_ms = $cleanupDuration
        cleanup_status = $cleanupStatus
        stdout = $stdout
        stderr = $stderr
    }
}

if ($Mode -eq 'SelfTest') {
    $pwsh = (Get-Process -Id $PID).Path
    $completed = Invoke-BoundedProcess $pwsh @('-NoProfile', '-Command',
        "Write-Output 'bounded-process-ok'") 900
    if ($completed.status -ne 'completed' -or
            $completed.stdout.Trim() -ne 'bounded-process-ok') {
        throw 'S14 bounded-process completion self-test failed.'
    }
    $timedOut = Invoke-BoundedProcess $pwsh @('-NoProfile', '-Command',
        'Start-Sleep -Milliseconds 1000') 100
    if ($timedOut.status -ne 'timeout') {
        throw 'S14 bounded-process timeout self-test failed.'
    }
    if ($null -ne $timedOut.normal_completion_duration_ms -or
            $timedOut.cleanup_duration_ms -lt 0 -or
            $timedOut.cleanup_status -ne 'completed' -or
            $timedOut.duration_ms -gt 250) {
        throw 'S14 bounded-process deadline/cleanup accounting self-test failed.'
    }
    Write-Output 'S14 telemetry collector self-test: passed'
    return
}

foreach ($value in @($NvidiaSmi, $OutputDirectory, $StopSignal)) {
    if ([string]::IsNullOrWhiteSpace($value)) {
        throw 'Capture mode requires NvidiaSmi, OutputDirectory, and StopSignal.'
    }
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$metricsPath = Join-Path $OutputDirectory 'gpu-telemetry.jsonl'
$processesPath = Join-Path $OutputDirectory 'gpu-processes.jsonl'
$attemptsPath = Join-Path $OutputDirectory 'telemetry-attempts.jsonl'
$errorsPath = Join-Path $OutputDirectory 'telemetry-errors.log'
$readyPath = Join-Path $OutputDirectory 'telemetry-ready.json'
$sessionPath = Join-Path $OutputDirectory 'telemetry-session.json'
foreach ($path in @($metricsPath, $processesPath, $attemptsPath, $errorsPath)) {
    Set-Content -LiteralPath $path -Value $null -Encoding utf8
}

$metricFields = @(
    'timestamp', 'uuid', 'pstate', 'temperature_gpu_c', 'utilization_gpu_percent',
    'utilization_memory_percent', 'clock_sm_mhz', 'clock_memory_mhz',
    'power_draw_w', 'power_limit_w', 'pcie_generation', 'pcie_width',
    'hardware_thermal_slowdown'
)
$metricQuery = 'timestamp,uuid,pstate,temperature.gpu,utilization.gpu,' +
    'utilization.memory,clocks.current.sm,clocks.current.memory,power.draw,' +
    'power.limit,pcie.link.gen.current,pcie.link.width.current,' +
    'clocks_event_reasons.hw_thermal_slowdown'
$sessionStartedAt = [DateTimeOffset]::Now
$sessionWatch = [Diagnostics.Stopwatch]::StartNew()
$sequence = 0
$completedAttempts = 0
$failedAttempts = 0

try {
    while (-not (Test-Path -LiteralPath $StopSignal)) {
        $scheduledElapsed = $sequence * [double]$IntervalMilliseconds
        $remaining = $scheduledElapsed - $sessionWatch.Elapsed.TotalMilliseconds
        if ($remaining -gt 0) {
            Start-Sleep -Milliseconds ([int][Math]::Ceiling($remaining))
        }
        $startedAt = [DateTimeOffset]::Now
        $startedElapsed = $sessionWatch.Elapsed.TotalMilliseconds
        $metric = Invoke-BoundedProcess $NvidiaSmi `
            @("--query-gpu=$metricQuery", '--format=csv,noheader,nounits') `
            $QueryTimeoutMilliseconds
        $process = Invoke-BoundedProcess $NvidiaSmi `
            @('--query-compute-apps=timestamp,gpu_uuid,pid,process_name,used_gpu_memory',
              '--format=csv,noheader,nounits') $QueryTimeoutMilliseconds
        $attemptStatus = if ($metric.status -eq 'completed' -and
                $process.status -eq 'completed') { 'completed' } else { 'failed' }
        if ($attemptStatus -eq 'completed') {
            $line = @($metric.stdout -split '\r?\n' |
                Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) |
                Select-Object -First 1
            if ([string]::IsNullOrWhiteSpace($line)) {
                $attemptStatus = 'failed'
                "{0} sequence={1} metric query returned no row" -f `
                    $startedAt.ToString('o'), $sequence |
                    Add-Content -LiteralPath $errorsPath -Encoding utf8
            } else {
                $values = $line -split ',\s*', $metricFields.Count
                if ($values.Count -ne $metricFields.Count) {
                    $attemptStatus = 'failed'
                    "{0} sequence={1} metric field count {2} did not match {3}" -f `
                        $startedAt.ToString('o'), $sequence, $values.Count,
                        $metricFields.Count |
                        Add-Content -LiteralPath $errorsPath -Encoding utf8
                } else {
                    $row = [ordered]@{
                        schema_version = 'S14-GPU-TELEMETRY-v1'
                        sequence = $sequence
                        captured_at = $startedAt.ToString('o')
                        query_duration_ms = $metric.duration_ms
                    }
                    for ($i = 0; $i -lt $metricFields.Count; ++$i) {
                        $row[$metricFields[$i]] = $values[$i]
                    }
                    Write-JsonLine $metricsPath $row
                    $processLines = @($process.stdout -split '\r?\n' |
                        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
                    if ($processLines.Count -eq 0) {
                        Write-JsonLine $processesPath ([ordered]@{
                            schema_version = 'S14-GPU-PROCESSES-v1'
                            sequence = $sequence
                            captured_at = $startedAt.ToString('o')
                            query_duration_ms = $process.duration_ms
                            gpu_uuid = ''; pid = ''; process_name = ''
                            used_gpu_memory = ''; no_processes = $true
                        })
                    } else {
                        foreach ($processLine in $processLines) {
                            $parts = $processLine -split ',\s*', 5
                            if ($parts.Count -ne 5) {
                                $attemptStatus = 'failed'
                                "{0} sequence={1} process field count {2} did not match 5" -f `
                                    $startedAt.ToString('o'), $sequence, $parts.Count |
                                    Add-Content -LiteralPath $errorsPath -Encoding utf8
                                continue
                            }
                            Write-JsonLine $processesPath ([ordered]@{
                                schema_version = 'S14-GPU-PROCESSES-v1'
                                sequence = $sequence
                                captured_at = $startedAt.ToString('o')
                                query_duration_ms = $process.duration_ms
                                nvidia_timestamp = $parts[0]
                                gpu_uuid = $parts[1]; pid = $parts[2]
                                process_name = $parts[3]
                                used_gpu_memory = $parts[4]; no_processes = $false
                            })
                        }
                    }
                }
            }
        }
        if ($metric.status -ne 'completed' -or $process.status -ne 'completed') {
            "{0} sequence={1} metric={2} process={3} metric_stderr={4} process_stderr={5}" -f `
                $startedAt.ToString('o'), $sequence, $metric.status, $process.status,
                $metric.stderr.Trim(), $process.stderr.Trim() |
                Add-Content -LiteralPath $errorsPath -Encoding utf8
        }
        $finishedAt = [DateTimeOffset]::Now
        Write-JsonLine $attemptsPath ([ordered]@{
            schema_version = 'S14-TELEMETRY-ATTEMPT-v1'
            sequence = $sequence
            scheduled_elapsed_ms = $scheduledElapsed
            attempt_started_at = $startedAt.ToString('o')
            attempt_started_elapsed_ms = $startedElapsed
            attempt_finished_at = $finishedAt.ToString('o')
            attempt_finished_elapsed_ms = $sessionWatch.Elapsed.TotalMilliseconds
            metric_status = $metric.status
            metric_exit_code = $metric.exit_code
            metric_duration_ms = $metric.duration_ms
            metric_normal_completion_duration_ms =
                $metric.normal_completion_duration_ms
            metric_cleanup_duration_ms = $metric.cleanup_duration_ms
            metric_cleanup_status = $metric.cleanup_status
            process_status = $process.status
            process_exit_code = $process.exit_code
            process_duration_ms = $process.duration_ms
            process_normal_completion_duration_ms =
                $process.normal_completion_duration_ms
            process_cleanup_duration_ms = $process.cleanup_duration_ms
            process_cleanup_status = $process.cleanup_status
            status = $attemptStatus
        })
        if ($attemptStatus -eq 'completed') {
            ++$completedAttempts
            if (-not (Test-Path -LiteralPath $readyPath)) {
                [ordered]@{
                    schema_version = 'S14-TELEMETRY-READY-v1'
                    sequence = $sequence
                    ready_at = [DateTimeOffset]::Now.ToString('o')
                } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $readyPath -Encoding utf8
            }
        } else { ++$failedAttempts }
        ++$sequence
    }
} finally {
    [ordered]@{
        schema_version = 'S14-TELEMETRY-SESSION-v1'
        process_id = $PID
        started_at = $sessionStartedAt.ToString('o')
        ended_at = [DateTimeOffset]::Now.ToString('o')
        interval_ms = $IntervalMilliseconds
        query_timeout_ms = $QueryTimeoutMilliseconds
        attempted_samples = $sequence
        completed_samples = $completedAttempts
        failed_samples = $failedAttempts
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $sessionPath -Encoding utf8
}
