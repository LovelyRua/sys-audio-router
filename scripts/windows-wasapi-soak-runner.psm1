function Get-WasapiSoakModeNames {
  param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("render", "duplex", "loopback", "both", "all")]
    [string]$Mode
  )

  switch ($Mode) {
    "render" { return @("render") }
    "duplex" { return @("duplex") }
    "loopback" { return @("loopback") }
    "both" { return @("render", "duplex") }
    "all" { return @("render", "duplex", "loopback") }
  }
}

function Get-WasapiSoakMetricNames {
  return @(
    "capture_discontinuity_cycles",
    "render_fifo_underflow_cycles",
    "wait_timeout_cycles",
    "render_wait_timeout_cycles",
    "duplex_event_wait_timeout_cycles",
    "capture_fifo_overflow_cycles",
    "capture_fifo_overflow_frames",
    "render_fifo_overflow_cycles",
    "render_fifo_overflow_frames",
    "process_error_cycles",
    "stream_start_error_cycles",
    "stream_stop_error_cycles",
    "rendered_frames",
    "target_rendered_frames",
    "render_fifo_underflow_frames",
    "render_startup_silence_frames",
    "render_recovery_silence_frames",
    "render_capture_starvation_silence_frames",
    "maximum_render_recovery_silence_frames"
  )
}

function New-WasapiSoakMetricState {
  param([uint64]$Attempts)

  $totals = @{}
  foreach ($metricName in Get-WasapiSoakMetricNames) {
    $totals[$metricName] = [uint64]0
  }

  return [pscustomobject]@{
    Attempts = $Attempts
    ParsedAttempts = [uint64]0
    AcceptedAttempts = [uint64]0
    ParseFailures = [uint64]0
    GateFailures = [uint64]0
    DurationMilliseconds = [uint64]0
    Totals = $totals
  }
}

function ConvertTo-WasapiSoakUnsignedInteger {
  param(
    [hashtable]$Fields,
    [string]$Name
  )

  if (!$Fields.ContainsKey($Name)) {
    return [pscustomobject]@{ Ok = $false; Error = "missing_metric:$Name" }
  }

  [uint64]$value = 0
  if (![uint64]::TryParse(
      [string]$Fields[$Name],
      [System.Globalization.NumberStyles]::None,
      [System.Globalization.CultureInfo]::InvariantCulture,
      [ref]$value)) {
    return [pscustomobject]@{ Ok = $false; Error = "invalid_metric:$Name" }
  }
  return [pscustomobject]@{ Ok = $true; Value = $value }
}

function ConvertFrom-WasapiMeasurementOutput {
  param(
    [Parameter(Mandatory = $true)]
    [AllowEmptyCollection()]
    [string[]]$Lines,
    [Parameter(Mandatory = $true)]
    [ValidateSet("render", "duplex", "loopback")]
    [string]$Mode,
    [uint64]$MaximumRenderRecoverySilenceFrames = 0,
    [ValidateRange(0, 10000)]
    [uint32]$MinimumRenderedFrameCoverageBasisPoints = 9900
  )

  $durationMatches = @($Lines | Where-Object { $_ -match '^\s*Duration ms:\s*\S+\s*$' })
  if ($durationMatches.Count -ne 1) {
    $reason = if ($durationMatches.Count -eq 0) { "missing_duration" } else { "duplicate_duration" }
    return [pscustomobject]@{ Ok = $false; Error = $reason }
  }

  $summaryLines = @($Lines | Where-Object { $_ -match '^wasapi_runtime_summary(?:\s|$)' })
  if ($summaryLines.Count -ne 1) {
    $reason = if ($summaryLines.Count -eq 0) { "missing_runtime_summary" } else { "duplicate_runtime_summary" }
    return [pscustomobject]@{ Ok = $false; Error = $reason }
  }

  [uint64]$durationMilliseconds = 0
  if (![uint64]::TryParse(
      ($durationMatches[0] -replace '^\s*Duration ms:\s*', '').Trim(),
      [System.Globalization.NumberStyles]::None,
      [System.Globalization.CultureInfo]::InvariantCulture,
      [ref]$durationMilliseconds) -or $durationMilliseconds -eq 0) {
    return [pscustomobject]@{ Ok = $false; Error = "invalid_duration" }
  }

  $fields = @{}
  foreach ($match in [regex]::Matches($summaryLines[0], '(?:^|\s)([^\s=]+)=([^\s]+)')) {
    $name = $match.Groups[1].Value
    if ($fields.ContainsKey($name)) {
      return [pscustomobject]@{ Ok = $false; Error = "duplicate_metric:$name" }
    }
    $fields[$name] = $match.Groups[2].Value
  }

  $requiredMetrics = [System.Collections.Generic.List[string]]::new()
  foreach ($name in @(
      "capture_discontinuity_cycles",
      "render_fifo_underflow_cycles",
      "wait_timeout_cycles",
      "capture_fifo_overflow_cycles",
      "capture_fifo_overflow_frames",
      "render_fifo_overflow_cycles",
      "render_fifo_overflow_frames",
      "process_error_cycles",
      "stream_start_error_cycles",
      "stream_stop_error_cycles")) {
    $requiredMetrics.Add($name)
  }
  if ($Mode -ne "loopback") {
    foreach ($name in @("render_wait_timeout_cycles", "rendered_frames", "render_sample_rate")) {
      $requiredMetrics.Add($name)
    }
  }
  if ($Mode -eq "duplex") {
    foreach ($name in @(
        "duplex_event_wait_timeout_cycles",
        "render_fifo_underflow_frames",
        "render_startup_silence_frames",
        "render_recovery_silence_frames",
        "render_capture_starvation_silence_frames",
        "maximum_render_recovery_silence_frames")) {
      $requiredMetrics.Add($name)
    }
  }

  $metrics = @{}
  foreach ($metricName in $requiredMetrics) {
    $parsed = ConvertTo-WasapiSoakUnsignedInteger -Fields $fields -Name $metricName
    if (!$parsed.Ok) {
      return [pscustomobject]@{ Ok = $false; Error = $parsed.Error }
    }
    $metrics[$metricName] = [uint64]$parsed.Value
  }
  foreach ($metricName in Get-WasapiSoakMetricNames) {
    if (!$metrics.ContainsKey($metricName)) {
      $metrics[$metricName] = [uint64]0
    }
  }

  $failures = [System.Collections.Generic.List[string]]::new()
  foreach ($metricName in @(
      "process_error_cycles",
      "stream_start_error_cycles",
      "stream_stop_error_cycles",
      "wait_timeout_cycles",
      "capture_fifo_overflow_cycles",
      "capture_fifo_overflow_frames",
      "render_fifo_overflow_cycles",
      "render_fifo_overflow_frames")) {
    if ($metrics[$metricName] -ne 0) {
      $failures.Add("${metricName}_nonzero")
    }
  }

  if ($Mode -ne "loopback") {
    if ($metrics.render_wait_timeout_cycles -ne 0) {
      $failures.Add("render_wait_timeout_cycles_nonzero")
    }
    if ($metrics.render_sample_rate -eq 0) {
      $failures.Add("render_sample_rate_zero")
    } else {
      $target = ([System.Numerics.BigInteger]$metrics.render_sample_rate *
          [System.Numerics.BigInteger]$durationMilliseconds) / 1000
      if ($target -gt [uint64]::MaxValue) {
        return [pscustomobject]@{ Ok = $false; Error = "metric_overflow:target_rendered_frames" }
      }
      $metrics.target_rendered_frames = [uint64]$target
      $minimum = ($target * $MinimumRenderedFrameCoverageBasisPoints + 9999) / 10000
      if ([System.Numerics.BigInteger]$metrics.rendered_frames -lt $minimum) {
        $failures.Add("rendered_frame_coverage_below_minimum")
      }
    }
  }

  if ($Mode -eq "duplex") {
    $attributed = [System.Numerics.BigInteger]$metrics.render_startup_silence_frames +
        [System.Numerics.BigInteger]$metrics.render_recovery_silence_frames +
        [System.Numerics.BigInteger]$metrics.render_capture_starvation_silence_frames
    if ($attributed -ne [System.Numerics.BigInteger]$metrics.render_fifo_underflow_frames) {
      $failures.Add("render_underflow_not_exactly_attributed")
    }
    if ($MaximumRenderRecoverySilenceFrames -ne 0 -and
        $metrics.maximum_render_recovery_silence_frames -gt
            $MaximumRenderRecoverySilenceFrames) {
      $failures.Add("maximum_render_recovery_silence_frames_exceeded")
    }
  }

  return [pscustomobject]@{
    Ok = $true
    Passed = $failures.Count -eq 0
    Error = [string]::Join(",", $failures)
    DurationMilliseconds = $durationMilliseconds
    Metrics = $metrics
  }
}

function Add-WasapiSoakMeasurement {
  param(
    [Parameter(Mandatory = $true)]$State,
    [Parameter(Mandatory = $true)]$Measurement,
    [Parameter(Mandatory = $true)][bool]$AttemptAccepted
  )

  $State.ParsedAttempts = [uint64]($State.ParsedAttempts + 1)
  if ($AttemptAccepted) {
    $State.AcceptedAttempts = [uint64]($State.AcceptedAttempts + 1)
  }
  if (!$Measurement.Passed) {
    $State.GateFailures = [uint64]($State.GateFailures + 1)
  }
  $State.DurationMilliseconds = [uint64](
      $State.DurationMilliseconds + $Measurement.DurationMilliseconds)
  foreach ($metricName in Get-WasapiSoakMetricNames) {
    if ($metricName -eq "maximum_render_recovery_silence_frames") {
      $State.Totals[$metricName] = [uint64][math]::Max(
          $State.Totals[$metricName], $Measurement.Metrics[$metricName])
    } else {
      $State.Totals[$metricName] = [uint64](
          $State.Totals[$metricName] + $Measurement.Metrics[$metricName])
    }
  }
}

function Format-WasapiSoakMetricSummary {
  param(
    [Parameter(Mandatory = $true)][string]$Label,
    [Parameter(Mandatory = $true)]$State
  )

  $culture = [System.Globalization.CultureInfo]::InvariantCulture
  $durationSeconds = ([double]$State.DurationMilliseconds / 1000.0).ToString("0.000", $culture)
  $parts = @(
    "[soak] metrics $Label",
    "attempts=$($State.Attempts)",
    "parsed=$($State.ParsedAttempts)",
    "accepted=$($State.AcceptedAttempts)",
    "parse_failures=$($State.ParseFailures)",
    "gate_failures=$($State.GateFailures)",
    "duration_seconds=$durationSeconds"
  )
  foreach ($metricName in Get-WasapiSoakMetricNames) {
    $total = [uint64]$State.Totals[$metricName]
    if ($metricName -eq "maximum_render_recovery_silence_frames") {
      $parts += "${metricName}_maximum=$total"
      continue
    }
    $rate = "n/a"
    if ($State.DurationMilliseconds -ne 0) {
      $rate = (($total * 1000.0) / $State.DurationMilliseconds).ToString("0.000000", $culture)
    }
    $parts += "${metricName}_total=$total"
    $parts += "${metricName}_per_second=$rate"
  }
  return [string]::Join(" ", $parts)
}

function Invoke-WasapiSoak {
  param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("render", "duplex", "loopback", "both", "all")]
    [string]$Mode,
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [uint32]::MaxValue)]
    [uint32]$Iterations,
    [uint64]$MaximumRenderRecoverySilenceFrames = 0,
    [ValidateRange(0, 10000)]
    [uint32]$MinimumRenderedFrameCoverageBasisPoints = 9900,
    [Parameter(Mandatory = $true)]
    [scriptblock]$RunMeasurement
  )

  $modeNames = @(Get-WasapiSoakModeNames -Mode $Mode)
  $failures = @{}
  $metricsByMode = @{}
  foreach ($modeName in $modeNames) {
    $failures[$modeName] = [uint64]0
    $metricsByMode[$modeName] = New-WasapiSoakMetricState -Attempts $Iterations
  }
  $totalMetrics = New-WasapiSoakMetricState -Attempts (
      [uint64]$modeNames.Count * [uint64]$Iterations)

  for ([uint64]$iteration = 1; $iteration -le $Iterations; ++$iteration) {
    Write-Output "[soak] iteration $iteration/$Iterations"
    foreach ($modeName in $modeNames) {
      $measurementRecords = @(& $RunMeasurement $modeName $iteration 6>&1)
      $measurementLines = [System.Collections.Generic.List[string]]::new()
      $exitCodeRecords = [System.Collections.Generic.List[object]]::new()
      foreach ($record in $measurementRecords) {
        if ($record -is [System.Management.Automation.InformationRecord]) {
          $recordText = [string]$record
          Write-Host $recordText
          foreach ($line in ($recordText -split "\r?\n")) {
            $measurementLines.Add($line)
          }
        } else {
          $exitCodeRecords.Add($record)
        }
      }

      $exitCode = 0
      if ($exitCodeRecords.Count -ne 0) {
        $exitCode = $exitCodeRecords[$exitCodeRecords.Count - 1]
        for ($recordIndex = 0; $recordIndex -lt $exitCodeRecords.Count - 1; ++$recordIndex) {
          $recordText = [string]$exitCodeRecords[$recordIndex]
          Write-Host $recordText
          foreach ($line in ($recordText -split "\r?\n")) {
            $measurementLines.Add($line)
          }
        }
      }

      $attemptFailed = [int]$exitCode -ne 0
      $measurement = ConvertFrom-WasapiMeasurementOutput `
          -Lines $measurementLines.ToArray() -Mode $modeName `
          -MaximumRenderRecoverySilenceFrames $MaximumRenderRecoverySilenceFrames `
          -MinimumRenderedFrameCoverageBasisPoints $MinimumRenderedFrameCoverageBasisPoints
      if ($measurement.Ok) {
        if (!$measurement.Passed) {
          $attemptFailed = $true
          Write-Output "[soak] gate_failure mode=$modeName iteration=$iteration reason=$($measurement.Error)"
        }
        Add-WasapiSoakMeasurement -State $metricsByMode[$modeName] `
            -Measurement $measurement -AttemptAccepted (!$attemptFailed)
        Add-WasapiSoakMeasurement -State $totalMetrics `
            -Measurement $measurement -AttemptAccepted (!$attemptFailed)
      } else {
        $attemptFailed = $true
        $metricsByMode[$modeName].ParseFailures = [uint64](
            $metricsByMode[$modeName].ParseFailures + 1)
        $totalMetrics.ParseFailures = [uint64]($totalMetrics.ParseFailures + 1)
        Write-Output "[soak] parse_failure mode=$modeName iteration=$iteration reason=$($measurement.Error)"
      }
      if ($attemptFailed) {
        $failures[$modeName] = [uint64]($failures[$modeName] + 1)
      }
    }
  }

  [uint64]$totalFailures = 0
  foreach ($modeName in $modeNames) {
    $modeFailures = $failures[$modeName]
    $totalFailures += $modeFailures
    Write-Output "[soak] mode=$modeName iterations=$Iterations failures=$modeFailures"
    Write-Output (Format-WasapiSoakMetricSummary -Label "mode=$modeName" -State $metricsByMode[$modeName])
  }
  Write-Output (Format-WasapiSoakMetricSummary -Label "total" -State $totalMetrics)
  Write-Output "[soak] completed modes=$($modeNames.Count) iterations=$Iterations attempts=$($modeNames.Count * $Iterations) failures=$totalFailures"

  [pscustomobject]@{
    ModeNames = $modeNames
    Iterations = $Iterations
    Attempts = [uint64]$modeNames.Count * [uint64]$Iterations
    FailuresByMode = $failures
    FailureCount = $totalFailures
    MetricsByMode = $metricsByMode
    TotalMetrics = $totalMetrics
    ParseFailureCount = $totalMetrics.ParseFailures
    GateFailureCount = $totalMetrics.GateFailures
  }
}

Export-ModuleMember -Function Get-WasapiSoakModeNames, Invoke-WasapiSoak
