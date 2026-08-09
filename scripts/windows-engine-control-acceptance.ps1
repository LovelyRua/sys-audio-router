[CmdletBinding()]
param(
  [string]$BuildPath = (Join-Path $PSScriptRoot "..\build"),

  [ValidateSet("", "Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
  [string]$Configuration = "",

  [string]$RenderDeviceId = "",

  [ValidateRange(1, 120)]
  [int]$ReadyTimeoutSeconds = 15,

  [ValidateRange(0, 60000)]
  [int]$DiagnosticsDelayMilliseconds = 500,

  [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"

function ConvertTo-SummaryValue {
  param([string]$Value)
  return '"' + ($Value -replace '([\\"])', '\$1') + '"'
}

function Resolve-SarExecutable {
  param(
    [string]$Root,
    [string]$Name,
    [string]$BuildConfiguration
  )

  $candidates = [System.Collections.Generic.List[string]]::new()
  if (![string]::IsNullOrWhiteSpace($BuildConfiguration)) {
    $candidates.Add((Join-Path $Root "$BuildConfiguration\$Name"))
  }
  $candidates.Add((Join-Path $Root $Name))
  foreach ($candidateConfiguration in @("Debug", "Release", "RelWithDebInfo", "MinSizeRel")) {
    $candidates.Add((Join-Path $Root "$candidateConfiguration\$Name"))
  }

  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
  }
  throw "$Name was not found below build path: $Root"
}

function Invoke-ControlCli {
  param(
    [string]$CliPath,
    [string]$PipeName,
    [string[]]$OperationArguments
  )

  $lines = @()
  $previousPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $lines = @(& $CliPath --pipe $PipeName @OperationArguments 2>&1 |
        ForEach-Object { [string]$_ })
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousPreference
  }
  return [pscustomobject]@{
    ExitCode = $exitCode
    Lines = $lines
    Text = [string]::Join([Environment]::NewLine, $lines)
  }
}

function Assert-AcceptedHeader {
  param(
    [pscustomobject]$Result,
    [string]$Step
  )

  if ($Result.ExitCode -ne 0) {
    throw "$Step exited with code $($Result.ExitCode): $($Result.Text)"
  }
  if ($Result.Lines.Count -eq 0 -or
      $Result.Lines[0] -notmatch '^control_response status=accepted command_id=cli-\d+-\d+(?:\s|$)') {
    throw "$Step did not return an accepted machine-readable response: $($Result.Text)"
  }
}

function Write-StepResult {
  param(
    [string]$Step,
    [pscustomobject]$Result
  )
  foreach ($line in $Result.Lines) {
    Write-Output "engine_control_acceptance_output step=$Step line=$(ConvertTo-SummaryValue $line)"
  }
}

$runId = [Guid]::NewGuid().ToString("N")
$pipeName = "sar-engine-acceptance-$PID-$runId"
$serviceProcess = $null
$runtimeMayBeRunning = $false
$cleanupComplete = $false
$passed = $false
$failedStep = "setup"
$failureReason = "none"
$deviceCount = 0
$processedBlocks = 0

try {
  $resolvedBuildPath = (Resolve-Path -LiteralPath $BuildPath).Path
  $servicePath = Resolve-SarExecutable -Root $resolvedBuildPath `
      -Name "sar_engine_service.exe" -BuildConfiguration $Configuration
  $cliPath = Resolve-SarExecutable -Root $resolvedBuildPath `
      -Name "sar_control_cli.exe" -BuildConfiguration $Configuration

  if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path ([IO.Path]::GetTempPath()) "sar-engine-acceptance-$runId"
  }
  $null = New-Item -ItemType Directory -Path $OutputDirectory -Force
  $OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
  $serviceStdoutPath = Join-Path $OutputDirectory "service.stdout.log"
  $serviceStderrPath = Join-Path $OutputDirectory "service.stderr.log"

  $failedStep = "service-start"
  $serviceProcess = Start-Process -FilePath $servicePath `
      -ArgumentList @("--pipe", $pipeName) -PassThru -WindowStyle Hidden `
      -RedirectStandardOutput $serviceStdoutPath `
      -RedirectStandardError $serviceStderrPath

  $failedStep = "devices"
  $deadline = [DateTime]::UtcNow.AddSeconds($ReadyTimeoutSeconds)
  $devicesResult = $null
  do {
    if ($serviceProcess.HasExited) {
      $stderr = if (Test-Path -LiteralPath $serviceStderrPath) {
        [string]::Join("; ", @(Get-Content -LiteralPath $serviceStderrPath))
      } else { "missing service stderr log" }
      throw "service exited before becoming ready with code $($serviceProcess.ExitCode): $stderr"
    }
    $devicesResult = Invoke-ControlCli -CliPath $cliPath -PipeName $pipeName `
        -OperationArguments @("devices")
    if ($devicesResult.ExitCode -eq 0) { break }
    Start-Sleep -Milliseconds 100
  } while ([DateTime]::UtcNow -lt $deadline)
  Assert-AcceptedHeader -Result $devicesResult -Step $failedStep
  Write-StepResult -Step $failedStep -Result $devicesResult
  if ($devicesResult.Lines[0] -notmatch '(?:^|\s)devices=(?<count>\d+)(?:\s|$)') {
    throw "devices response did not contain a numeric devices field"
  }
  $deviceCount = [int]$Matches["count"]
  if ($deviceCount -lt 1) {
    throw "devices response contained no active audio devices"
  }
  $deviceLines = @($devicesResult.Lines | Where-Object { $_ -match '^device ' })
  if ($deviceLines.Count -ne $deviceCount) {
    throw "devices response count did not match its machine-readable device lines"
  }
  $renderDeviceLines = @($deviceLines | Where-Object {
      $_ -match ' backend=wasapi direction=output '
    })
  if ($renderDeviceLines.Count -eq 0) {
    throw "devices response contained no active WASAPI render endpoint"
  }

  $failedStep = "runtime-configure-render"
  $configureArguments = @("runtime-configure-render")
  if (![string]::IsNullOrWhiteSpace($RenderDeviceId)) {
    $configureArguments += $RenderDeviceId
  }
  $configureResult = Invoke-ControlCli -CliPath $cliPath -PipeName $pipeName `
      -OperationArguments $configureArguments
  Assert-AcceptedHeader -Result $configureResult -Step $failedStep
  Write-StepResult -Step $failedStep -Result $configureResult
  if ($configureResult.Lines[0] -notmatch '(?:^|\s)runtime_installed=true(?:\s|$)' -or
      $configureResult.Lines[0] -notmatch '(?:^|\s)runtime_running=false(?:\s|$)' -or
      $configureResult.Lines[0] -notmatch '(?:^|\s)runtime_mode=wasapi-render(?:\s|$)') {
    throw "runtime-configure-render response did not describe an installed, stopped render runtime"
  }
  $expectedRenderId = if ([string]::IsNullOrWhiteSpace($RenderDeviceId)) {
    "default"
  } else {
    $RenderDeviceId
  }
  if ($configureResult.Lines[0] -notmatch
      (" render_id=" + [regex]::Escape($expectedRenderId) + '$')) {
    throw "runtime-configure-render response did not preserve the requested render endpoint"
  }

  $failedStep = "runtime-start"
  $startResult = Invoke-ControlCli -CliPath $cliPath -PipeName $pipeName `
      -OperationArguments @("runtime-start")
  Assert-AcceptedHeader -Result $startResult -Step $failedStep
  Write-StepResult -Step $failedStep -Result $startResult
  if ($startResult.Lines[0] -notmatch '(?:^|\s)runtime_installed=true(?:\s|$)' -or
      $startResult.Lines[0] -notmatch '(?:^|\s)runtime_running=true(?:\s|$)') {
    throw "runtime-start response did not describe a running runtime"
  }
  $runtimeMayBeRunning = $true

  $failedStep = "diagnostics"
  if ($DiagnosticsDelayMilliseconds -gt 0) {
    Start-Sleep -Milliseconds $DiagnosticsDelayMilliseconds
  }
  $diagnosticsResult = Invoke-ControlCli -CliPath $cliPath -PipeName $pipeName `
      -OperationArguments @("diagnostics")
  Assert-AcceptedHeader -Result $diagnosticsResult -Step $failedStep
  Write-StepResult -Step $failedStep -Result $diagnosticsResult
  if ($diagnosticsResult.Lines[0] -notmatch '(?:^|\s)processed_blocks=(?<blocks>\d+)(?:\s|$)') {
    throw "diagnostics response was missing numeric machine-readable fields"
  }
  $processedBlocks = [long]$Matches["blocks"]
  if ($diagnosticsResult.Lines[0] -notmatch '(?:^|\s)xruns=\d+(?:\s|$)' -or
      $diagnosticsResult.Lines[0] -notmatch '(?:^|\s)callback_peak_us=[0-9.eE+-]+(?:\s|$)') {
    throw "diagnostics response was missing numeric machine-readable fields"
  }
  if ($processedBlocks -lt 1) {
    throw "diagnostics reported zero processed blocks"
  }

  $failedStep = "runtime-stop"
  $stopResult = Invoke-ControlCli -CliPath $cliPath -PipeName $pipeName `
      -OperationArguments @("runtime-stop")
  Assert-AcceptedHeader -Result $stopResult -Step $failedStep
  Write-StepResult -Step $failedStep -Result $stopResult
  if ($stopResult.Lines[0] -notmatch '(?:^|\s)runtime_installed=true(?:\s|$)' -or
      $stopResult.Lines[0] -notmatch '(?:^|\s)runtime_running=false(?:\s|$)') {
    throw "runtime-stop response did not describe a stopped runtime"
  }
  $runtimeMayBeRunning = $false
  $passed = $true
} catch {
  $failureReason = $_.Exception.Message
} finally {
  if ($null -ne $serviceProcess) {
    if ($runtimeMayBeRunning -and !$serviceProcess.HasExited -and
        $null -ne $cliPath) {
      try {
        $cleanupStop = Invoke-ControlCli -CliPath $cliPath -PipeName $pipeName `
            -OperationArguments @("runtime-stop")
        Write-StepResult -Step "finally-runtime-stop" -Result $cleanupStop
      } catch {
        # Process termination below remains the final cleanup boundary.
      }
    }
    if (!$serviceProcess.HasExited) {
      Stop-Process -Id $serviceProcess.Id -Force -ErrorAction SilentlyContinue
      Wait-Process -Id $serviceProcess.Id -Timeout 5 -ErrorAction SilentlyContinue
      $serviceProcess.Refresh()
    }
    $cleanupComplete = $serviceProcess.HasExited
  } else {
    $cleanupComplete = $true
  }
}

if (!$cleanupComplete) {
  $passed = $false
  if ($failureReason -eq "none") { $failureReason = "service_process_cleanup_failed" }
}

Write-Output (
    "engine_control_acceptance" +
    " passed=$(if ($passed) { 1 } else { 0 })" +
    " failed_step=$(ConvertTo-SummaryValue $(if ($passed) { 'none' } else { $failedStep }))" +
    " pipe=$(ConvertTo-SummaryValue $pipeName)" +
    " devices=$deviceCount" +
    " processed_blocks=$processedBlocks" +
    " cleanup_complete=$(if ($cleanupComplete) { 1 } else { 0 })" +
    " output_directory=$(ConvertTo-SummaryValue $OutputDirectory)" +
    " reason=$(ConvertTo-SummaryValue $failureReason)")

if ($passed) { exit 0 }
exit 1
