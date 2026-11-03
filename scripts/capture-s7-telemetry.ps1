param(
    [Parameter(Mandatory = $true)]
    [string]$NvidiaSmi,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [Parameter(Mandatory = $true)]
    [string]$StopSignal,
    [Parameter(Mandatory = $true)]
    [ValidateRange(100, 60000)]
    [int]$IntervalMilliseconds
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$metricsPath = Join-Path $OutputDirectory 'gpu-telemetry.jsonl'
$processesPath = Join-Path $OutputDirectory 'gpu-processes.jsonl'
$errorsPath = Join-Path $OutputDirectory 'telemetry-errors.log'
Set-Content -LiteralPath $metricsPath -Value $null -Encoding utf8
Set-Content -LiteralPath $processesPath -Value $null -Encoding utf8
Set-Content -LiteralPath $errorsPath -Value $null -Encoding utf8

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

while (-not (Test-Path -LiteralPath $StopSignal)) {
    $capturedAt = (Get-Date).ToString('o')
    try {
        $line = (& $NvidiaSmi "--query-gpu=$metricQuery" '--format=csv,noheader,nounits' |
            Select-Object -First 1)
        if ([string]::IsNullOrWhiteSpace($line)) {
            throw 'nvidia-smi GPU telemetry query returned no row'
        }
        $values = $line -split ',\s*', $metricFields.Count
        if ($values.Count -ne $metricFields.Count) {
            throw "GPU telemetry field count $($values.Count) did not match $($metricFields.Count)"
        }
        $row = [ordered]@{ captured_at = $capturedAt }
        for ($i = 0; $i -lt $metricFields.Count; ++$i) {
            $row[$metricFields[$i]] = $values[$i]
        }
        $row | ConvertTo-Json -Compress | Add-Content -LiteralPath $metricsPath -Encoding utf8

        $processLines = @(& $NvidiaSmi `
            '--query-compute-apps=timestamp,gpu_uuid,pid,process_name,used_gpu_memory' `
            '--format=csv,noheader,nounits')
        if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
            throw 'nvidia-smi process telemetry query failed'
        }
        if ($processLines.Count -eq 0) {
            [ordered]@{ captured_at = $capturedAt; gpu_uuid = ''; pid = '';
                process_name = ''; used_gpu_memory = ''; no_processes = $true } |
                ConvertTo-Json -Compress |
                Add-Content -LiteralPath $processesPath -Encoding utf8
        } else {
            foreach ($processLine in $processLines) {
                if ([string]::IsNullOrWhiteSpace($processLine)) { continue }
                $parts = $processLine -split ',\s*', 5
                if ($parts.Count -ne 5) {
                    throw "GPU process telemetry field count $($parts.Count) did not match 5"
                }
                [ordered]@{ captured_at = $capturedAt; nvidia_timestamp = $parts[0];
                    gpu_uuid = $parts[1]; pid = $parts[2]; process_name = $parts[3];
                    used_gpu_memory = $parts[4]; no_processes = $false } |
                    ConvertTo-Json -Compress |
                    Add-Content -LiteralPath $processesPath -Encoding utf8
            }
        }
    } catch {
        "${capturedAt} $($_.Exception.Message)" |
            Add-Content -LiteralPath $errorsPath -Encoding utf8
    }
    Start-Sleep -Milliseconds $IntervalMilliseconds
}
