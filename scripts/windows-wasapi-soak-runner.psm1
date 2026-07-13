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
    "capture_fifo_overflow_cycles",
    "render_fifo_overflow_cycles"
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
    ParseFailures = [uint64]0
    DurationMilliseconds = [uint64]0
    Totals = $totals
  }
}

function ConvertFrom-WasapiMeasurementOutput {
  param(
    [Parameter(Mandatory = $true)]
    [AllowEmptyCollection()]
    [string[]]$Lines
  )

  $durationMatches = @($Lines | Where-Object { $_ -match '^\s*Duration ms:\s*\d+\s*$' })
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
      [ref]$durationMilliseconds) -or $durationMilliseconds -eq 0) {
    return [pscustomobject]@{ Ok = $false; Error = "invalid_duration" }
  }

  $fields = @{}
  foreach ($match in [regex]::Matches($summaryLines[0], '(?:^|\s)([^\s=]+)=([^\s]*)')) {
    $fields[$match.Groups[1].Value] = $match.Groups[2].Value
  }

  $metrics = @{}
  foreach ($metricName in Get-WasapiSoakMetricNames) {
    if (!$fields.ContainsKey($metricName)) {
      return [pscustomobject]@{ Ok = $false; Error = "missing_metric:$metricName" }
    }
    [uint64]$metricValue = 0
    if (![uint64]::TryParse($fields[$metricName], [ref]$metricValue)) {
      return [pscustomobject]@{ Ok = $false; Error = "invalid_metric:$metricName" }
    }
    $metrics[$metricName] = $metricValue
  }

  return [pscustomobject]@{
    Ok = $true
    Error = ""
    DurationMilliseconds = $durationMilliseconds
    Metrics = $metrics
  }
}

function Add-WasapiSoakMeasurement {
  param(
    [Parameter(Mandatory = $true)]$State,
    [Parameter(Mandatory = $true)]$Measurement
  )

  $State.ParsedAttempts = [uint64]($State.ParsedAttempts + 1)
  $State.DurationMilliseconds = [uint64](
      $State.DurationMilliseconds + $Measurement.DurationMilliseconds)
  foreach ($metricName in Get-WasapiSoakMetricNames) {
    $State.Totals[$metricName] = [uint64](
        $State.Totals[$metricName] + $Measurement.Metrics[$metricName])
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
    "parse_failures=$($State.ParseFailures)",
    "duration_seconds=$durationSeconds"
  )
  foreach ($metricName in Get-WasapiSoakMetricNames) {
    $total = [uint64]$State.Totals[$metricName]
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
      if ([int]$exitCode -ne 0) {
        $failures[$modeName] = [uint64]($failures[$modeName] + 1)
      }

      $measurement = ConvertFrom-WasapiMeasurementOutput -Lines $measurementLines.ToArray()
      if ($measurement.Ok) {
        Add-WasapiSoakMeasurement -State $metricsByMode[$modeName] -Measurement $measurement
        Add-WasapiSoakMeasurement -State $totalMetrics -Measurement $measurement
      } else {
        $metricsByMode[$modeName].ParseFailures = [uint64](
            $metricsByMode[$modeName].ParseFailures + 1)
        $totalMetrics.ParseFailures = [uint64]($totalMetrics.ParseFailures + 1)
        Write-Output "[soak] parse_failure mode=$modeName iteration=$iteration reason=$($measurement.Error)"
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
  }
}

Export-ModuleMember -Function Get-WasapiSoakModeNames, Invoke-WasapiSoak
