[CmdletBinding()]
param(
  [string]$BuildPath = (Join-Path $PSScriptRoot "..\build"),
  [ValidateSet("", "Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
  [string]$Configuration = "",
  [string]$CaptureDeviceIdA = "",
  [string]$CaptureDeviceIdB = "",
  [string]$RenderDeviceId = "",
  [ValidateRange(8000, 384000)]
  [int]$SampleRate = 48000,
  [ValidateRange(1, 120)]
  [int]$ReadyTimeoutSeconds = 20,
  [ValidateRange(100, 60000)]
  [int]$DiagnosticsDelayMilliseconds = 1500,
  [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

function Resolve-SarExecutable {
  param([string]$Root, [string]$Name, [string]$BuildConfiguration)
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
  param([string]$CliPath, [string]$PipeName, [string[]]$Arguments)
  $previousPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $lines = @(& $CliPath --pipe $PipeName @Arguments 2>&1 | ForEach-Object { [string]$_ })
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousPreference
  }
  [pscustomobject]@{
    ExitCode = $exitCode
    Lines = $lines
    Text = [string]::Join([Environment]::NewLine, $lines)
  }
}

function Save-StepResult {
  param([string]$OutputPath, [string]$Step, [pscustomobject]$Result)
  $Result.Text | Set-Content -LiteralPath (Join-Path $OutputPath "$Step.log") -Encoding UTF8
}

function Assert-Accepted {
  param([pscustomobject]$Result, [string]$Step)
  if ($Result.ExitCode -ne 0 -or $Result.Lines.Count -eq 0 -or
      $Result.Lines[0] -notmatch '^control_response status=accepted command_id=cli-\d+-\d+(?:\s|$)') {
    throw "$Step did not return an accepted response: $($Result.Text)"
  }
}

function ConvertFrom-CliQuotedText {
  param([string]$Value)
  return [regex]::Replace($Value, '\\(["\\])', '$1')
}

function Get-DeviceInventory {
  param([string[]]$Lines)
  $devices = @{}
  foreach ($line in $Lines) {
    if ($line -match '^device index=(?<index>\d+) id="(?<id>.*?)" label="(?<label>.*?)" backend=(?<backend>\S+) direction=(?<direction>\S+) default=(?<default>\S+) virtual=(?<virtual>\S+) formats=(?<formats>\d+)$') {
      $index = [int]$Matches.index
      $devices[$index] = [pscustomobject]@{
        index = $index
        id = ConvertFrom-CliQuotedText $Matches.id
        label = ConvertFrom-CliQuotedText $Matches.label
        backend = $Matches.backend
        direction = $Matches.direction
        default = ($Matches.default -eq "true")
        virtual = ($Matches.virtual -eq "true")
        formats = [System.Collections.Generic.List[object]]::new()
      }
    } elseif ($line -match '^device_format device_index=(?<device>\d+) index=(?<index>\d+) sample_rate=(?<rate>\d+) channels=(?<channels>\d+) frames=(?<frames>\d+) bits=(?<bits>\d+) valid_bits=(?<validBits>\d+) format=(?<format>\S+)$') {
      $deviceIndex = [int]$Matches.device
      if ($devices.ContainsKey($deviceIndex)) {
        $devices[$deviceIndex].formats.Add([pscustomobject]@{
          index = [int]$Matches.index
          sample_rate = [int]$Matches.rate
          channels = [int]$Matches.channels
          frames = [int]$Matches.frames
          bits = [int]$Matches.bits
          valid_bits = [int]$Matches.validBits
          format = $Matches.format
        })
      }
    }
  }
  return @($devices.Values | Sort-Object index)
}

function Select-EndpointDevice {
  param(
    [object[]]$Inventory,
    [ValidateSet("capture", "render")][string]$Direction,
    [string]$RequestedId,
    [int]$RequiredSampleRate,
    [string[]]$ExcludedIds = @()
  )
  $directions = if ($Direction -eq "capture") { @("input", "duplex") } else { @("output", "duplex") }
  $candidates = @($Inventory | Where-Object {
    $_.backend -eq "wasapi" -and $directions -contains $_.direction -and
    $ExcludedIds -notcontains $_.id -and
    @($_.formats | Where-Object { $_.sample_rate -eq $RequiredSampleRate }).Count -gt 0
  })
  if (![string]::IsNullOrWhiteSpace($RequestedId)) {
    $candidates = @($candidates | Where-Object { $_.id -eq $RequestedId })
    if ($candidates.Count -ne 1) {
      throw "$Direction endpoint '$RequestedId' is not a unique active WASAPI endpoint at $RequiredSampleRate Hz."
    }
  }
  if ($candidates.Count -eq 0) {
    throw "No eligible $Direction WASAPI endpoint is available at $RequiredSampleRate Hz."
  }
  $device = $candidates[0]
  $format = @($device.formats | Where-Object { $_.sample_rate -eq $RequiredSampleRate })[0]
  [pscustomobject]@{ Device = $device; Format = $format }
}

function Get-Counter {
  param([string]$Text, [string]$Name)
  if ($Text -notmatch "(?:^|\s)$([regex]::Escape($Name))=(?<value>\d+)(?:\s|$)") {
    throw "diagnostics did not contain $Name"
  }
  return [long]$Matches.value
}

function Start-AcceptanceService {
  param([string]$ServicePath, [string]$PipeName, [string]$SessionPath, [string]$LogPrefix)
  $stdoutPath = "$LogPrefix.stdout.log"
  $stderrPath = "$LogPrefix.stderr.log"
  $process = Start-Process -FilePath $ServicePath `
      -ArgumentList @("--pipe", $PipeName, "--session", $SessionPath) `
      -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdoutPath `
      -RedirectStandardError $stderrPath
  [pscustomobject]@{ Process = $process; StdoutPath = $stdoutPath; StderrPath = $stderrPath }
}

function Wait-ServiceReady {
  param([pscustomobject]$Service, [string]$CliPath, [string]$PipeName, [int]$TimeoutSeconds)
  $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
  do {
    if ($Service.Process.HasExited) {
      $stderr = if (Test-Path -LiteralPath $Service.StderrPath) {
        [string]::Join("; ", @(Get-Content -LiteralPath $Service.StderrPath))
      } else { "missing service stderr log" }
      throw "service exited before becoming ready: $stderr"
    }
    $result = Invoke-ControlCli $CliPath $PipeName @("runtime-state")
    if ($result.ExitCode -eq 0) { Assert-Accepted $result "service-ready"; return $result }
    Start-Sleep -Milliseconds 100
  } while ([DateTime]::UtcNow -lt $deadline)
  throw "service did not become ready within $TimeoutSeconds seconds"
}

function Stop-OwnedService {
  param([pscustomobject]$Service)
  if ($null -ne $Service -and !$Service.Process.HasExited) {
    Stop-Process -Id $Service.Process.Id -Force -ErrorAction SilentlyContinue
    Wait-Process -Id $Service.Process.Id -Timeout 5 -ErrorAction SilentlyContinue
    $Service.Process.Refresh()
  }
  return ($null -eq $Service -or $Service.Process.HasExited)
}

$runId = [Guid]::NewGuid().ToString("N")
$service = $null
$cliPath = ""
$activePipe = ""
$passed = $false
$cleanupComplete = $true
$failedStep = "setup"
$failureReason = "none"
$configuredHash = ""
$processedDelta = 0L
$selected = $null

try {
  $resolvedBuildPath = (Resolve-Path -LiteralPath $BuildPath).Path
  $servicePath = Resolve-SarExecutable $resolvedBuildPath "sar_engine_service.exe" $Configuration
  $cliPath = Resolve-SarExecutable $resolvedBuildPath "sar_control_cli.exe" $Configuration
  if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path ([IO.Path]::GetTempPath()) "sar-multi-endpoint-$runId"
  }
  $null = New-Item -ItemType Directory -Path $OutputDirectory -Force
  $OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
  $sessionPath = Join-Path $OutputDirectory "multi-endpoint-session.sars"
  [ordered]@{
    schema_version = 1; run_id = $runId; started_utc = [DateTime]::UtcNow.ToString("o")
    build_path = $resolvedBuildPath; configuration = $Configuration; sample_rate = $SampleRate
    requested_capture_a = $CaptureDeviceIdA; requested_capture_b = $CaptureDeviceIdB
    requested_render = $RenderDeviceId
  } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory "manifest.json") -Encoding UTF8

  $failedStep = "initial-service"
  $activePipe = "sar-multi-first-$PID-$runId"
  $service = Start-AcceptanceService $servicePath $activePipe $sessionPath (Join-Path $OutputDirectory "first-service")
  $initialState = Wait-ServiceReady $service $cliPath $activePipe $ReadyTimeoutSeconds
  Save-StepResult $OutputDirectory "runtime-state-initial" $initialState

  $failedStep = "device-inventory"
  $deviceResult = Invoke-ControlCli $cliPath $activePipe @("devices")
  Assert-Accepted $deviceResult $failedStep
  Save-StepResult $OutputDirectory "devices" $deviceResult
  $inventory = @(Get-DeviceInventory $deviceResult.Lines)
  $inventory | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $OutputDirectory "devices.json") -Encoding UTF8
  $captureA = Select-EndpointDevice $inventory "capture" $CaptureDeviceIdA $SampleRate
  $captureB = Select-EndpointDevice $inventory "capture" $CaptureDeviceIdB $SampleRate @($captureA.Device.id)
  $render = Select-EndpointDevice $inventory "render" $RenderDeviceId $SampleRate
  $selected = [ordered]@{
    captures = @(
      [ordered]@{ endpoint_id = "capture-a"; device_id = $captureA.Device.id; label = $captureA.Device.label; channels = $captureA.Format.channels },
      [ordered]@{ endpoint_id = "capture-b"; device_id = $captureB.Device.id; label = $captureB.Device.label; channels = $captureB.Format.channels }
    )
    renders = @(
      [ordered]@{ endpoint_id = "render-main"; device_id = $render.Device.id; label = $render.Device.label; channels = $render.Format.channels }
    )
  }
  $selected | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory "selected-endpoints.json") -Encoding UTF8

  $failedStep = "configure-matrix"
  $configureArguments = @(
    "runtime-configure-matrix",
    "capture", "capture-a", $captureA.Device.id, "0", [string]$captureA.Format.channels, "follower",
    "capture", "capture-b", $captureB.Device.id, "0", [string]$captureB.Format.channels, "follower",
    "render", "render-main", $render.Device.id, "0", [string]$render.Format.channels, "master"
  )
  $configured = Invoke-ControlCli $cliPath $activePipe $configureArguments
  Assert-Accepted $configured $failedStep
  Save-StepResult $OutputDirectory "runtime-configure-matrix" $configured
  if ($configured.Lines[0] -notmatch '(?:^|\s)runtime_mode=wasapi-matrix(?:\s|$)') {
    throw "matrix configure did not report runtime_mode=wasapi-matrix"
  }
  $configuredState = Invoke-ControlCli $cliPath $activePipe @("state")
  Assert-Accepted $configuredState "configured-session-state"
  Save-StepResult $OutputDirectory "session-state-configured" $configuredState

  $failedStep = "runtime-start"
  $started = Invoke-ControlCli $cliPath $activePipe @("runtime-start")
  Assert-Accepted $started $failedStep
  Save-StepResult $OutputDirectory "runtime-start" $started
  if ($started.Lines[0] -notmatch '(?:^|\s)runtime_running=true(?:\s|$)') {
    throw "runtime-start did not report runtime_running=true"
  }
  $diagnosticsBefore = Invoke-ControlCli $cliPath $activePipe @("diagnostics")
  Assert-Accepted $diagnosticsBefore "diagnostics-before"
  Save-StepResult $OutputDirectory "diagnostics-before" $diagnosticsBefore
  Start-Sleep -Milliseconds $DiagnosticsDelayMilliseconds
  $diagnosticsAfter = Invoke-ControlCli $cliPath $activePipe @("diagnostics")
  Assert-Accepted $diagnosticsAfter "diagnostics-after"
  Save-StepResult $OutputDirectory "diagnostics-after" $diagnosticsAfter
  $beforeBlocks = Get-Counter $diagnosticsBefore.Lines[0] "processed_blocks"
  $afterBlocks = Get-Counter $diagnosticsAfter.Lines[0] "processed_blocks"
  $null = Get-Counter $diagnosticsAfter.Lines[0] "xruns"
  $null = Get-Counter $diagnosticsAfter.Lines[0] "capture_overflow_frames"
  $null = Get-Counter $diagnosticsAfter.Lines[0] "render_underflow_frames"
  $processedDelta = $afterBlocks - $beforeBlocks
  if ($processedDelta -lt 1) { throw "matrix runtime processed no audio blocks during the observation window" }

  $failedStep = "session-snapshot"
  if (!(Test-Path -LiteralPath $sessionPath -PathType Leaf)) { throw "session file was not created" }
  Copy-Item -LiteralPath $sessionPath -Destination (Join-Path $OutputDirectory "session-running.sars") -Force
  $configuredHash = (Get-FileHash -LiteralPath $sessionPath -Algorithm SHA256).Hash
  $cleanupComplete = Stop-OwnedService $service
  $service = $null
  if (!$cleanupComplete) { throw "first service process did not stop" }

  $failedStep = "session-restore"
  $activePipe = "sar-multi-restored-$PID-$runId"
  $service = Start-AcceptanceService $servicePath $activePipe $sessionPath (Join-Path $OutputDirectory "restored-service")
  $restored = Wait-ServiceReady $service $cliPath $activePipe $ReadyTimeoutSeconds
  Save-StepResult $OutputDirectory "runtime-state-restored" $restored
  if ($restored.Lines[0] -notmatch '(?:^|\s)runtime_installed=true(?:\s|$)' -or
      $restored.Lines[0] -notmatch '(?:^|\s)runtime_running=true(?:\s|$)' -or
      $restored.Lines[0] -notmatch '(?:^|\s)runtime_mode=wasapi-matrix(?:\s|$)') {
    throw "restored session did not auto-start its WASAPI matrix runtime"
  }
  if ((Get-FileHash -LiteralPath $sessionPath -Algorithm SHA256).Hash -ne $configuredHash) {
    throw "read-only restore unexpectedly rewrote the session file"
  }
  Start-Sleep -Milliseconds $DiagnosticsDelayMilliseconds
  $restoredDiagnostics = Invoke-ControlCli $cliPath $activePipe @("diagnostics")
  Assert-Accepted $restoredDiagnostics "diagnostics-restored"
  Save-StepResult $OutputDirectory "diagnostics-restored" $restoredDiagnostics
  if ((Get-Counter $restoredDiagnostics.Lines[0] "processed_blocks") -lt 1) {
    throw "restored matrix runtime processed no audio blocks"
  }
  Copy-Item -LiteralPath $sessionPath -Destination (Join-Path $OutputDirectory "session-restored.sars") -Force
  $stopped = Invoke-ControlCli $cliPath $activePipe @("runtime-stop")
  Assert-Accepted $stopped "runtime-stop"
  Save-StepResult $OutputDirectory "runtime-stop" $stopped
  $cleanupComplete = Stop-OwnedService $service
  $service = $null
  if (!$cleanupComplete) { throw "restored service process did not stop" }
  $passed = $true
} catch {
  $failureReason = $_.Exception.Message
} finally {
  if ($null -ne $service) {
    try {
      if (![string]::IsNullOrWhiteSpace($activePipe) -and
          ![string]::IsNullOrWhiteSpace($cliPath)) {
        $null = Invoke-ControlCli $cliPath $activePipe @("runtime-stop")
      }
    } catch {}
    $cleanupComplete = (Stop-OwnedService $service) -and $cleanupComplete
  }
}

if (!$cleanupComplete) {
  $passed = $false
  if ($failureReason -eq "none") { $failureReason = "owned_service_cleanup_failed" }
}
$resultRecord = [ordered]@{
  schema_version = 1; passed = $passed
  failed_step = if ($passed) { "none" } else { $failedStep }
  reason = $failureReason; captures = 2; renders = 1
  processed_block_delta = $processedDelta; session_sha256 = $configuredHash
  cleanup_complete = $cleanupComplete; selected_endpoints = $selected
  completed_utc = [DateTime]::UtcNow.ToString("o")
}
if (![string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $resultRecord | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory "result.json") -Encoding UTF8
}
Write-Output ("multi_endpoint_acceptance passed=$(if ($passed) { 1 } else { 0 })" +
    " failed_step=$(if ($passed) { 'none' } else { $failedStep }) captures=2 renders=1" +
    " processed_delta=$processedDelta cleanup_complete=$(if ($cleanupComplete) { 1 } else { 0 })" +
    " session_sha256=$configuredHash output_directory=$OutputDirectory reason=$failureReason")
if ($passed) { exit 0 }
exit 1
