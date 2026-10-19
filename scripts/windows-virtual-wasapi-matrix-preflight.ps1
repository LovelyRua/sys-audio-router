[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$CaptureDeviceId,
  [Parameter(Mandatory)][string]$RenderDeviceId,
  [string]$BuildPath = (Join-Path $PSScriptRoot '..\build'),
  [ValidateSet('', 'Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
  [string]$Configuration = '',
  [ValidateRange(1, 60)][int]$ObserveSeconds = 5,
  [switch]$SkipRoutes,
  [switch]$ExerciseRouteTransitions,
  [string]$SignalProbePath = '',
  [string]$ProbeRenderDeviceId = '',
  [string]$ProbeCaptureDeviceId = '',
  [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
Import-Module (Join-Path $PSScriptRoot 'windows-lab-process.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'windows-virtual-wasapi-signal-evidence.psm1') -Force
$script:commandIndex = 0

function Resolve-Executable([string]$Name) {
  $paths = @()
  if ($Configuration) { $paths += (Join-Path $BuildPath "$Configuration\$Name") }
  $paths += (Join-Path $BuildPath $Name)
  foreach ($candidate in @('Release', 'Debug', 'RelWithDebInfo', 'MinSizeRel')) {
    $paths += (Join-Path $BuildPath "$candidate\$Name")
  }
  foreach ($path in $paths) {
    if (Test-Path -LiteralPath $path -PathType Leaf) {
      return (Resolve-Path -LiteralPath $path).Path
    }
  }
  throw "$Name was not found under $BuildPath"
}

function Invoke-Cli([string[]]$Arguments, [int]$TimeoutMilliseconds = 10000) {
  ++$script:commandIndex
  $command = Invoke-SarLabProcess -FilePath $script:cli -ArgumentList (
    @('--pipe', $script:pipe) + $Arguments
  ) -OutputDirectory $OutputDirectory -Name ('control-' + $script:commandIndex) `
    -TimeoutMilliseconds $TimeoutMilliseconds
  $lines = @($command.stdout_lines)
  if ($command.failure_reason -ne '' -or $command.exit_code -ne 0 -or $lines.Count -eq 0 -or
      $lines[0] -notmatch '^control_response status=accepted ') {
    throw "CLI $($Arguments[0]) failed: $($command.failure_reason) exit=$($command.exit_code); logs=$($command.stderr_path)"
  }
  return $lines
}

function Get-Counter([string]$Text, [string]$Name) {
  if ($Text -notmatch "(?:^|\s)$([regex]::Escape($Name))=(\d+)(?:\s|$)") {
    throw "diagnostics did not include $Name"
  }
  return [long]$Matches[1]
}

function Set-MatrixRoutes([bool]$Enabled) {
  for ($channel = 1; $channel -le $script:renderChannels; ++$channel) {
    $sourceChannel = [Math]::Min($channel, $script:captureChannels)
    $arguments = @('disconnect-route', "virtual-in.ch$sourceChannel", "hardware-out.ch$channel")
    if ($Enabled) { $arguments = @('connect-route', "virtual-in.ch$sourceChannel", "hardware-out.ch$channel", '1') }
    $null = Invoke-Cli $arguments
  }
}

function Invoke-SignalPhase([string]$Name, [bool]$RoutesEnabled) {
  $before = @(Invoke-Cli @('diagnostics'))
  $probe = Invoke-SarLabProcess -FilePath $SignalProbePath -ArgumentList @(
    '--route', $ProbeRenderDeviceId, $ProbeCaptureDeviceId
  ) -OutputDirectory $OutputDirectory -Name "signal-$Name" -TimeoutMilliseconds 15000
  $evidence = ConvertFrom-SarMatrixSignalProbeEvidence -Lines $probe.stdout_lines `
    -ExitCode $probe.exit_code -RoutesEnabled $RoutesEnabled
  $after = @(Invoke-Cli @('diagnostics'))
  $delta = (Get-Counter $after[0] 'processed_blocks') - (Get-Counter $before[0] 'processed_blocks')
  $record = [pscustomobject][ordered]@{
    name = $Name; routes_enabled = $RoutesEnabled; passed = $false
    process_failure = $probe.failure_reason; elapsed_ms = $probe.elapsed_ms
    processed_block_delta = $delta; evidence = $evidence
    stdout_path = $probe.stdout_path; stderr_path = $probe.stderr_path
  }
  $record.passed = ($probe.failure_reason -eq '' -and $evidence.passed -and $delta -gt 0)
  $result.phases.Add($record)
  $record | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $OutputDirectory "$Name.json") -Encoding UTF8
  if (!$record.passed) {
    throw "Signal phase '$Name' failed: $($probe.failure_reason) $($evidence.reason) processed_delta=$delta"
  }
}

function Assert-Endpoint([string[]]$Inventory, [string]$Id, [string]$Direction) {
  $escaped = [regex]::Escape($Id)
  $pattern = "^device index=(\d+) id=`"$escaped`" .*backend=wasapi direction=$Direction "
  $device = @($Inventory | Where-Object { $_ -match $pattern })
  if ($device.Count -ne 1) {
    throw "Expected one active WASAPI $Direction endpoint with ID '$Id', found $($device.Count)"
  }
  $null = $device[0] -match $pattern
  $deviceIndex = $Matches[1]
  $format = @($Inventory | Where-Object {
    $_ -match "^device_format device_index=$deviceIndex index=\d+ sample_rate=48000 channels=([12]) "
  })
  if ($format.Count -eq 0) {
    throw "Endpoint '$Id' needs a 48 kHz mono/stereo format"
  }
  $null = $format[0] -match 'channels=([12]) '
  return [int]$Matches[1]
}

if ((Get-SarLabSessionId) -eq 0) {
  throw 'Run this preflight in a logged-on interactive audio session, not WinRM Session 0.'
}
if ($CaptureDeviceId -eq $RenderDeviceId) {
  throw 'Capture and render endpoint IDs must differ.'
}
if ($ExerciseRouteTransitions -and (!$SignalProbePath -or $SkipRoutes)) {
  throw 'Route transitions require a signal probe and cannot be combined with SkipRoutes.'
}
if ($SignalProbePath) {
  if (!$ProbeRenderDeviceId -or !$ProbeCaptureDeviceId) {
    throw 'Signal probe requires both probe endpoint IDs.'
  }
  $SignalProbePath = (Resolve-Path -LiteralPath $SignalProbePath -ErrorAction Stop).Path
  if (!(Test-Path -LiteralPath $SignalProbePath -PathType Leaf)) {
    throw 'Signal probe must be an executable file.'
  }
  $allIds = @($CaptureDeviceId, $RenderDeviceId, $ProbeRenderDeviceId, $ProbeCaptureDeviceId)
  if (@($allIds | Sort-Object -Unique).Count -ne 4) {
    throw 'Signal acceptance requires four distinct endpoints; a direct loopback could bypass the matrix.'
  }
} elseif ($ProbeRenderDeviceId -or $ProbeCaptureDeviceId) {
  throw 'Probe endpoint IDs require SignalProbePath.'
}

$runId = [guid]::NewGuid().ToString('N')
if (!$OutputDirectory) {
  $OutputDirectory = Join-Path ([IO.Path]::GetTempPath()) "sar-virtual-wasapi-preflight-$runId"
}
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$script:pipe = "sar-virtual-wasapi-preflight-$runId"
$script:cli = Resolve-Executable 'sar_control_cli.exe'
$service = Resolve-Executable 'sar_engine_service.exe'
$session = Join-Path $OutputDirectory 'session.sars'
$log = Join-Path $OutputDirectory 'engine.log'
if ((Test-Path -LiteralPath $session) -or
    (Test-Path -LiteralPath (Join-Path $OutputDirectory 'ready.flag'))) {
  throw 'OutputDirectory contains a previous run. Use a new directory.'
}
$result = [ordered]@{
  schema_version = 2; run_id = $runId
  passed = $false; capture_device_id = $CaptureDeviceId
  render_device_id = $RenderDeviceId; processed_block_delta = 0
  routes_enabled = !$SkipRoutes
  signal_checked = $false; signal_detected = $null
  route_transitions_checked = $false
  phases = [Collections.Generic.List[object]]::new()
  engine_stopped_gracefully = $false; engine_forced_termination = $false
  engine_exit_code = $null
  engine_sha256 = (Get-FileHash -LiteralPath $service -Algorithm SHA256).Hash
  cli_sha256 = (Get-FileHash -LiteralPath $cli -Algorithm SHA256).Hash
  probe_sha256 = if ($SignalProbePath) { (Get-FileHash -LiteralPath $SignalProbePath -Algorithm SHA256).Hash } else { '' }
  acceptance_script_sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
  process_module_sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'windows-lab-process.psm1') -Algorithm SHA256).Hash
  evidence_module_sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'windows-virtual-wasapi-signal-evidence.psm1') -Algorithm SHA256).Hash
  output_directory = $OutputDirectory; reason = ''
}
$process = $null
try {
  $serviceArguments = '--pipe "{0}" --session "{1}" --log-file "{2}"' -f $pipe, $session, $log
  $process = Start-Process -FilePath $service -ArgumentList $serviceArguments -PassThru -WindowStyle Hidden
  $null = $process.Handle
  $ready = $false
  $startupClock = [Diagnostics.Stopwatch]::StartNew()
  for ($i = 0; $i -lt 100 -and $startupClock.ElapsedMilliseconds -lt 15000; ++$i) {
    if ($process.HasExited) { throw 'Engine exited during startup.' }
    try {
      $state = @(Invoke-Cli @('runtime-state') 1000)
      $ready = $true
      break
    } catch {}
    Start-Sleep -Milliseconds 100
  }
  $startupClock.Stop()
  if (!$ready) { throw 'Engine did not become ready.' }

  $inventory = @(Invoke-Cli @('devices'))
  $inventory | Set-Content -LiteralPath (Join-Path $OutputDirectory 'devices.log') -Encoding UTF8
  $captureChannels = Assert-Endpoint $inventory $CaptureDeviceId 'input'
  $renderChannels = Assert-Endpoint $inventory $RenderDeviceId 'output'
  if ($SignalProbePath) {
    $null = Assert-Endpoint $inventory $ProbeRenderDeviceId 'output'
    $null = Assert-Endpoint $inventory $ProbeCaptureDeviceId 'input'
  }
  $configured = @(Invoke-Cli @(
    'runtime-configure-matrix', 'capture', 'virtual-in', $CaptureDeviceId,
    '0', [string]$captureChannels, 'follower',
    'render', 'hardware-out', $RenderDeviceId,
    '0', [string]$renderChannels, 'master'
  ))
  $configured | Set-Content -LiteralPath (Join-Path $OutputDirectory 'configure.log') -Encoding UTF8
  if ($SignalProbePath -and ($captureChannels -ne 2 -or $renderChannels -ne 2)) {
    throw 'Stereo signal acceptance requires two-channel matrix endpoints.'
  }
  Set-MatrixRoutes $true
  if ($SkipRoutes -or $ExerciseRouteTransitions) { Set-MatrixRoutes $false }
  $graph = @(Invoke-Cli @('graph'))
  $graph | Set-Content -LiteralPath (Join-Path $OutputDirectory 'graph.log') -Encoding UTF8
  $null = Invoke-Cli @('runtime-start')
  $before = @(Invoke-Cli @('diagnostics'))
  'running' | Set-Content -LiteralPath (Join-Path $OutputDirectory 'ready.flag') -Encoding ASCII
  if ($SignalProbePath) {
    $result.signal_checked = $true
    if ($ExerciseRouteTransitions) {
      Invoke-SignalPhase 'disabled-before' $false
      Set-MatrixRoutes $true
      Invoke-SignalPhase 'enabled' $true
      Set-MatrixRoutes $false
      Invoke-SignalPhase 'disabled-after' $false
      Set-MatrixRoutes $true
      Invoke-SignalPhase 'reenabled' $true
      $result.route_transitions_checked = $true
      $result.signal_detected = $true
    } else {
      Invoke-SignalPhase 'signal' (!$SkipRoutes)
      $result.signal_detected = !$SkipRoutes
    }
  }
  Start-Sleep -Seconds $ObserveSeconds
  $after = @(Invoke-Cli @('diagnostics'))
  $after | Set-Content -LiteralPath (Join-Path $OutputDirectory 'diagnostics.log') -Encoding UTF8
  $result.processed_block_delta = (Get-Counter $after[0] 'processed_blocks') -
                                  (Get-Counter $before[0] 'processed_blocks')
  if ($result.processed_block_delta -le 0) { throw 'Matrix processed no blocks.' }
  $result.passed = $true
} catch {
  $result.reason = $_.Exception.Message
} finally {
  if ($null -ne $process -and !$process.HasExited) {
    try { $null = Invoke-Cli @('runtime-stop') } catch {}
    try {
      $stop = Invoke-SarLabProcess -FilePath $service -ArgumentList @('--pipe', $pipe, '--stop') `
        -OutputDirectory $OutputDirectory -Name 'engine-stop'
      if ($stop.failure_reason -ne '' -or $stop.exit_code -ne 0) {
        throw 'Engine stop command failed.'
      }
    } catch {
      $result.passed = $false
      $result.reason = $_.Exception.Message
    }
    if (!$process.WaitForExit(10000)) {
      $result.passed = $false
      $result.reason = 'Owned engine did not stop gracefully.'
      $result.engine_forced_termination = $true
      try {
        $process.Kill()
        if (!$process.WaitForExit(5000)) {
          $result.reason = 'Owned engine remained active after forced termination.'
        }
      } catch {
        $result.reason = 'Owned engine cleanup failed: ' + $_.Exception.Message
      }
    } else {
      $process.Refresh()
      $result.engine_exit_code = $process.ExitCode
      if ($process.ExitCode -eq 0) {
        $result.engine_stopped_gracefully = $true
      } else {
        $result.passed = $false
        $result.reason = "Engine exited with code $($process.ExitCode)."
      }
    }
  }
  if ($result.passed -and !$result.engine_stopped_gracefully) {
    $result.passed = $false
    $result.reason = 'Engine exited before the graceful stop request.'
  }
  if ($null -ne $process) { $process.Dispose() }
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
Write-Output "virtual_wasapi_matrix_preflight passed=$([int]$result.passed) signal_checked=$([int]$result.signal_checked) processed_delta=$($result.processed_block_delta) output_directory=$OutputDirectory reason=$($result.reason)"
if (!$result.passed) { exit 1 }
