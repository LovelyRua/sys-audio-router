[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$CaptureDeviceId,
  [Parameter(Mandatory)][string]$RenderDeviceId,
  [string]$BuildPath = (Join-Path $PSScriptRoot '..\build'),
  [ValidateSet('', 'Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
  [string]$Configuration = '',
  [ValidateRange(1, 60)][int]$ObserveSeconds = 5,
  [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

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

function Invoke-Cli([string[]]$Arguments) {
  $lines = @(& $script:cli --pipe $script:pipe @Arguments 2>&1 | ForEach-Object { [string]$_ })
  if ($LASTEXITCODE -ne 0 -or $lines.Count -eq 0 -or
      $lines[0] -notmatch '^control_response status=accepted ') {
    throw "CLI $($Arguments[0]) failed: $($lines -join '; ')"
  }
  return $lines
}

function Get-Counter([string]$Text, [string]$Name) {
  if ($Text -notmatch "(?:^|\s)$([regex]::Escape($Name))=(\d+)(?:\s|$)") {
    throw "diagnostics did not include $Name"
  }
  return [long]$Matches[1]
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

if ([Diagnostics.Process]::GetCurrentProcess().SessionId -eq 0) {
  throw 'Run this preflight in a logged-on interactive audio session, not WinRM Session 0.'
}
if ($CaptureDeviceId -eq $RenderDeviceId) {
  throw 'Capture and render endpoint IDs must differ.'
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
$result = [ordered]@{
  passed = $false; capture_device_id = $CaptureDeviceId
  render_device_id = $RenderDeviceId; processed_block_delta = 0
  output_directory = $OutputDirectory; reason = ''
}
$process = $null
try {
  $serviceArguments = '--pipe "{0}" --session "{1}" --log-file "{2}"' -f $pipe, $session, $log
  $process = Start-Process -FilePath $service -ArgumentList $serviceArguments -PassThru -WindowStyle Hidden
  $ready = $false
  for ($i = 0; $i -lt 100; ++$i) {
    if ($process.HasExited) { throw 'Engine exited during startup.' }
    $state = @(& $cli --pipe $pipe runtime-state 2>&1 | ForEach-Object { [string]$_ })
    if ($LASTEXITCODE -eq 0 -and $state.Count -gt 0 -and $state[0] -match '^control_response status=accepted ') {
      $ready = $true
      break
    }
    Start-Sleep -Milliseconds 100
  }
  if (!$ready) { throw 'Engine did not become ready.' }

  $inventory = @(Invoke-Cli @('devices'))
  $inventory | Set-Content -LiteralPath (Join-Path $OutputDirectory 'devices.log') -Encoding UTF8
  $captureChannels = Assert-Endpoint $inventory $CaptureDeviceId 'input'
  $renderChannels = Assert-Endpoint $inventory $RenderDeviceId 'output'
  $configured = @(Invoke-Cli @(
    'runtime-configure-matrix', 'capture', 'virtual-in', $CaptureDeviceId,
    '0', [string]$captureChannels, 'follower',
    'render', 'hardware-out', $RenderDeviceId,
    '0', [string]$renderChannels, 'master'
  ))
  $configured | Set-Content -LiteralPath (Join-Path $OutputDirectory 'configure.log') -Encoding UTF8
  for ($channel = 1; $channel -le $renderChannels; ++$channel) {
    $sourceChannel = [Math]::Min($channel, $captureChannels)
    $null = Invoke-Cli @('connect-route', "virtual-in.ch$sourceChannel", "hardware-out.ch$channel", '1')
  }
  $graph = @(Invoke-Cli @('graph'))
  $graph | Set-Content -LiteralPath (Join-Path $OutputDirectory 'graph.log') -Encoding UTF8
  $null = Invoke-Cli @('runtime-start')
  $before = @(Invoke-Cli @('diagnostics'))
  'running' | Set-Content -LiteralPath (Join-Path $OutputDirectory 'ready.flag') -Encoding ASCII
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
    $null = & $service --pipe $pipe --stop 2>&1
    if (!$process.WaitForExit(10000)) {
      $result.passed = $false
      $result.reason = 'Owned engine did not stop gracefully.'
    }
  }
}
$result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
Write-Output "virtual_wasapi_matrix_preflight passed=$([int]$result.passed) processed_delta=$($result.processed_block_delta) output_directory=$OutputDirectory reason=$($result.reason)"
if (!$result.passed) { exit 1 }
