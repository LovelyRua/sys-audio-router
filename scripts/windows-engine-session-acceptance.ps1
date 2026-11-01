[CmdletBinding()]
param(
  [string]$BuildPath = (Join-Path $PSScriptRoot "..\build"),

  [ValidateSet("", "Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
  [string]$Configuration = "",

  [ValidateRange(1, 120)]
  [int]$ReadyTimeoutSeconds = 15,

  [ValidateRange(0, 60000)]
  [int]$DiagnosticsDelayMilliseconds = 500,

  [string]$OutputDirectory = "",

  [switch]$SkipAudioProgressCheck
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "windows-lab-process.psm1") -Force
$script:controlCliCount = 0

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
  param([string]$CliPath, [string]$PipeName, [string[]]$OperationArguments,
        [int]$TimeoutMilliseconds = 10000)

  $script:controlCliCount += 1
  $result = Invoke-SarLabProcess -FilePath $CliPath `
      -ArgumentList (@("--pipe", $PipeName) + $OperationArguments) `
      -OutputDirectory $OutputDirectory `
      -Name "control-$PID-$runId-$script:controlCliCount" `
      -TimeoutMilliseconds $TimeoutMilliseconds -MaximumOutputBytes 16777216
  if ($result.failure_reason -ne "") {
    throw "CLI $($OperationArguments[0]) failed: $($result.failure_reason); logs=$($result.stderr_path)"
  }
  $lines = @($result.stdout_lines) + @($result.stderr_lines)
  return [pscustomobject]@{
    ExitCode = $result.exit_code
    Lines = $lines
    Text = [string]::Join([Environment]::NewLine, $lines)
  }
}

function Assert-Accepted {
  param([pscustomobject]$Result, [string]$Step)

  if ($Result.ExitCode -ne 0 -or $Result.Lines.Count -eq 0 -or
      $Result.Lines[0] -notmatch '^control_response status=accepted command_id=cli-\d+-\d+(?:\s|$)') {
    throw "$Step did not return an accepted response: $($Result.Text)"
  }
}

function Read-SessionParts {
  param([string]$Path)

  $bytes = [IO.File]::ReadAllBytes($Path)
  $stream = [IO.MemoryStream]::new($bytes, $false)
  $reader = [IO.BinaryReader]::new($stream)
  try {
    if ([Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne "SARS") {
      throw "invalid session magic"
    }
    $version = $reader.ReadUInt16()
    $reserved = $reader.ReadUInt16()
    $payloadLength = $reader.ReadUInt32()
    if ($version -ne 5 -or $payloadLength -ne $bytes.Length - 12) {
      throw "unexpected session version or payload length"
    }
    $schema = $reader.ReadUInt32()
    $presetLength = $reader.ReadUInt32()
    if ($presetLength -gt $stream.Length - $stream.Position) {
      throw "invalid session preset length"
    }
    $preset = $reader.ReadBytes([int]$presetLength)
    $tail = $reader.ReadBytes([int]($stream.Length - $stream.Position))
    return [pscustomobject]@{
      Version = $version; Reserved = $reserved; Schema = $schema
      Preset = $preset; Tail = $tail
    }
  } finally {
    $reader.Dispose()
    $stream.Dispose()
  }
}

function Write-SessionParts {
  param([string]$Path, [pscustomobject]$Parts, [byte[]]$Preset, [byte[]]$Tail)

  $stream = [IO.MemoryStream]::new()
  $writer = [IO.BinaryWriter]::new($stream)
  try {
    $writer.Write([Text.Encoding]::ASCII.GetBytes("SARS"))
    $writer.Write([uint16]$Parts.Version)
    $writer.Write([uint16]$Parts.Reserved)
    $writer.Write([uint32](8 + $Preset.Length + $Tail.Length))
    $writer.Write([uint32]$Parts.Schema)
    $writer.Write([uint32]$Preset.Length)
    $writer.Write($Preset)
    $writer.Write($Tail)
    [IO.File]::WriteAllBytes($Path, $stream.ToArray())
  } finally {
    $writer.Dispose()
    $stream.Dispose()
  }
}

function Get-SessionMatrixEndpoint {
  param([pscustomobject]$Parts)

  $stream = [IO.MemoryStream]::new($Parts.Tail, $false)
  $reader = [IO.BinaryReader]::new($stream)
  try {
    if ($reader.ReadByte() -ne 3) { throw "expected a WASAPI matrix runtime" }
    foreach ($field in @("capture device", "render device")) {
      $length = $reader.ReadUInt32()
      if ($length -gt $stream.Length - $stream.Position) { throw "invalid $field length" }
      $null = $reader.ReadBytes([int]$length)
    }
    if ($reader.ReadUInt32() -ne 1) { throw "expected one matrix endpoint" }
    $endpointLength = $reader.ReadUInt32()
    if ($endpointLength -gt $stream.Length - $stream.Position) { throw "invalid endpoint length" }
    $endpointId = [Text.Encoding]::UTF8.GetString($reader.ReadBytes([int]$endpointLength))
    $deviceOffset = [int]$stream.Position
    $oldDeviceLength = $reader.ReadUInt32()
    if ($oldDeviceLength -gt $stream.Length - $stream.Position) { throw "invalid device length" }
    $null = $reader.ReadBytes([int]$oldDeviceLength)
    return [pscustomobject]@{
      Id = $endpointId; DeviceOffset = $deviceOffset; SuffixOffset = [int]$stream.Position
    }
  } finally {
    $reader.Dispose()
    $stream.Dispose()
  }
}

function Set-SessionMatrixDeviceUnavailable {
  param([string]$Path)

  $parts = Read-SessionParts $Path
  $endpoint = Get-SessionMatrixEndpoint $parts
  $newTail = [IO.MemoryStream]::new()
  $writer = [IO.BinaryWriter]::new($newTail)
  try {
    $writer.Write($parts.Tail, 0, $endpoint.DeviceOffset)
    $unavailable = [Text.Encoding]::UTF8.GetBytes("sar-test-unavailable-device")
    $writer.Write([uint32]$unavailable.Length)
    $writer.Write($unavailable)
    $writer.Write($parts.Tail, $endpoint.SuffixOffset,
        $parts.Tail.Length - $endpoint.SuffixOffset)
    Write-SessionParts $Path $parts $parts.Preset ($newTail.ToArray())
  } finally {
    $writer.Dispose()
    $newTail.Dispose()
  }
}

function Assert-SessionMatrixEndpoint {
  param([pscustomobject]$Parts, [string]$Expected)

  $endpoint = Get-SessionMatrixEndpoint $Parts
  if ($endpoint.Id -cne $Expected) {
    throw "persisted matrix endpoint '$($endpoint.Id)' did not match '$Expected'"
  }
}

function Assert-PersistedPreset {
  param([string]$CliPath, [string]$PipeName, [string]$SessionPath,
        [string]$PresetPath, [string]$Step)

  $exported = Invoke-ControlCli -CliPath $CliPath -PipeName $PipeName `
      -OperationArguments @("preset-save", $PresetPath)
  Assert-Accepted -Result $exported -Step $Step
  $session = Read-SessionParts $SessionPath
  $expected = [IO.File]::ReadAllBytes($PresetPath)
  if ([Convert]::ToBase64String($session.Preset) -cne
      [Convert]::ToBase64String($expected)) {
    throw "${Step}: persisted preset differs from the running service"
  }
  return $session
}

function Get-DefaultRenderChannels {
  param([pscustomobject]$Devices)

  $index = $null
  foreach ($line in $Devices.Lines) {
    if ($line -match '^device index=(?<index>\d+) .* backend=wasapi direction=output default=true ') {
      $index = [int]$Matches.index
      break
    }
  }
  if ($null -eq $index) { throw "no default WASAPI render endpoint" }
  foreach ($line in $Devices.Lines) {
    if ($line -match "^device_format device_index=$index index=0 sample_rate=(?<rate>\d+) channels=(?<channels>\d+) ") {
      if ([int]$Matches.rate -ne 48000) { throw "default render mix rate must be 48000 Hz" }
      return [int]$Matches.channels
    }
  }
  throw "default render mix format was not reported"
}

function Stop-SessionServiceGracefully {
  param([pscustomobject]$Service, [string]$ServicePath, [string]$PipeName)

  $null = & $ServicePath --pipe $PipeName --stop
  if ($LASTEXITCODE -ne 0 -or !$Service.Process.WaitForExit(10000)) {
    throw "owned service did not stop gracefully"
  }
  if ($Service.Process.ExitCode -ne 0) { throw "owned service exited with an error" }
}

function Start-SessionService {
  param(
    [string]$ServicePath,
    [string]$PipeName,
    [string]$SessionPath,
    [string]$LogPrefix,
    [switch]$Once
  )

  $arguments = @("--pipe", $PipeName, "--session", "`"$SessionPath`"")
  if ($Once) { $arguments += "--once" }
  $stdoutPath = "$LogPrefix.stdout.log"
  $stderrPath = "$LogPrefix.stderr.log"
  $process = Start-Process -FilePath $ServicePath -ArgumentList $arguments `
      -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdoutPath `
      -RedirectStandardError $stderrPath
  $null = $process.Handle
  return [pscustomobject]@{
    Process = $process
    StdoutPath = $stdoutPath
    StderrPath = $stderrPath
  }
}

function Wait-ServiceReady {
  param(
    [pscustomobject]$Service,
    [string]$CliPath,
    [string]$PipeName,
    [int]$TimeoutSeconds
  )

  $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
  do {
    if ($Service.Process.HasExited) {
      $stderr = if (Test-Path -LiteralPath $Service.StderrPath) {
        [string]::Join("; ", @(Get-Content -LiteralPath $Service.StderrPath))
      } else { "missing service stderr log" }
      throw "service exited before becoming ready: $stderr"
    }
    $remainingMilliseconds = [Math]::Max(100, [Math]::Min(10000,
        [int](($deadline - [DateTime]::UtcNow).TotalMilliseconds)))
    $result = Invoke-ControlCli -CliPath $CliPath -PipeName $PipeName `
        -OperationArguments @("runtime-state") `
        -TimeoutMilliseconds $remainingMilliseconds
    if ($result.ExitCode -eq 0) {
      Assert-Accepted -Result $result -Step "service-ready"
      return $result
    }
    Start-Sleep -Milliseconds 100
  } while ([DateTime]::UtcNow -lt $deadline)
  throw "service did not become ready within $TimeoutSeconds seconds"
}

function Stop-TestProcess {
  param([pscustomobject]$Service)

  if ($null -ne $Service -and !$Service.Process.HasExited) {
    Stop-Process -Id $Service.Process.Id -Force -ErrorAction SilentlyContinue
    Wait-Process -Id $Service.Process.Id -Timeout 5 -ErrorAction SilentlyContinue
    $Service.Process.Refresh()
  }
  return $null -eq $Service -or $Service.Process.HasExited
}

$runId = [Guid]::NewGuid().ToString("N")
$service = $null
$passed = $false
$cleanupComplete = $true
$failedStep = "setup"
$failureReason = "none"
$processedBlocks = 0
$audioProgressVerified = $false
$sessionBytes = 0
$corruptPreserved = $false
$matrixPersistenceVerified = $false

try {
  $resolvedBuildPath = (Resolve-Path -LiteralPath $BuildPath).Path
  $servicePath = Resolve-SarExecutable -Root $resolvedBuildPath `
      -Name "sar_engine_service.exe" -BuildConfiguration $Configuration
  $cliPath = Resolve-SarExecutable -Root $resolvedBuildPath `
      -Name "sar_control_cli.exe" -BuildConfiguration $Configuration
  if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path ([IO.Path]::GetTempPath()) "sar-session-acceptance-$runId"
  }
  $null = New-Item -ItemType Directory -Path $OutputDirectory -Force
  $OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
  $sessionPath = Join-Path $OutputDirectory "engine-session.sars"

  $failedStep = "initial-service"
  $firstPipe = "sar-session-first-$PID-$runId"
  $service = Start-SessionService -ServicePath $servicePath -PipeName $firstPipe `
      -SessionPath $sessionPath -LogPrefix (Join-Path $OutputDirectory "first")
  $initialState = Wait-ServiceReady -Service $service -CliPath $cliPath `
      -PipeName $firstPipe -TimeoutSeconds $ReadyTimeoutSeconds
  if ($initialState.Lines[0] -notmatch '(?:^|\s)runtime_installed=false(?:\s|$)') {
    throw "new session did not start without an installed runtime"
  }
  if (!(Test-Path -LiteralPath $sessionPath -PathType Leaf)) {
    throw "new session file was not created"
  }
  $magic = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($sessionPath), 0, 4)
  if ($magic -ne "SARS") { throw "new session file did not contain SARS magic" }

  $failedStep = "configure"
  $configured = Invoke-ControlCli -CliPath $cliPath -PipeName $firstPipe `
      -OperationArguments @("runtime-configure-render")
  Assert-Accepted -Result $configured -Step $failedStep
  $gain = Invoke-ControlCli -CliPath $cliPath -PipeName $firstPipe `
      -OperationArguments @("set-gain", "asio-output-l", "wasapi-render-l", "0.25")
  Assert-Accepted -Result $gain -Step "set-gain"
  $started = Invoke-ControlCli -CliPath $cliPath -PipeName $firstPipe `
      -OperationArguments @("runtime-start")
  Assert-Accepted -Result $started -Step "runtime-start"
  if ($started.Lines[0] -notmatch '(?:^|\s)runtime_running=true(?:\s|$)') {
    throw "runtime-start did not report a running runtime"
  }
  $sessionBytes = (Get-Item -LiteralPath $sessionPath).Length
  $persistedHash = (Get-FileHash -LiteralPath $sessionPath -Algorithm SHA256).Hash
  $cleanupComplete = Stop-TestProcess -Service $service
  $service = $null
  if (!$cleanupComplete) { throw "first service process did not stop" }

  $failedStep = "restored-service"
  $secondPipe = "sar-session-second-$PID-$runId"
  $service = Start-SessionService -ServicePath $servicePath -PipeName $secondPipe `
      -SessionPath $sessionPath -LogPrefix (Join-Path $OutputDirectory "second")
  $restoredState = Wait-ServiceReady -Service $service -CliPath $cliPath `
      -PipeName $secondPipe -TimeoutSeconds $ReadyTimeoutSeconds
  if ($restoredState.Lines[0] -notmatch '(?:^|\s)runtime_installed=true(?:\s|$)' -or
      $restoredState.Lines[0] -notmatch '(?:^|\s)runtime_running=true(?:\s|$)' -or
      $restoredState.Lines[0] -notmatch '(?:^|\s)runtime_mode=wasapi-render(?:\s|$)') {
    throw "restored session did not auto-start its WASAPI render runtime"
  }
  if ((Get-FileHash -LiteralPath $sessionPath -Algorithm SHA256).Hash -ne $persistedHash) {
    throw "read-only restore unexpectedly rewrote the session file"
  }
  if ($DiagnosticsDelayMilliseconds -gt 0) {
    Start-Sleep -Milliseconds $DiagnosticsDelayMilliseconds
  }
  $diagnostics = Invoke-ControlCli -CliPath $cliPath -PipeName $secondPipe `
      -OperationArguments @("diagnostics")
  Assert-Accepted -Result $diagnostics -Step "restored-diagnostics"
  if ($diagnostics.Lines[0] -notmatch '(?:^|\s)processed_blocks=(?<blocks>\d+)(?:\s|$)') {
    throw "restored diagnostics did not contain processed_blocks"
  }
  $processedBlocks = [long]$Matches["blocks"]
  if (!$SkipAudioProgressCheck) {
    if ($processedBlocks -lt 1) { throw "restored runtime processed no audio blocks" }
    $audioProgressVerified = $true
  }
  $stopped = Invoke-ControlCli -CliPath $cliPath -PipeName $secondPipe `
      -OperationArguments @("runtime-stop")
  Assert-Accepted -Result $stopped -Step "restored-runtime-stop"
  $cleanupComplete = Stop-TestProcess -Service $service
  $service = $null
  if (!$cleanupComplete) { throw "restored service process did not stop" }

  $failedStep = "matrix-configure-persistence"
  $matrixPath = Join-Path $OutputDirectory "matrix-session.sars"
  $matrixPipe = "sar-session-matrix-$PID-$runId"
  $service = Start-SessionService -ServicePath $servicePath -PipeName $matrixPipe `
      -SessionPath $matrixPath -LogPrefix (Join-Path $OutputDirectory "matrix")
  $null = Wait-ServiceReady -Service $service -CliPath $cliPath `
      -PipeName $matrixPipe -TimeoutSeconds $ReadyTimeoutSeconds
  $devices = Invoke-ControlCli -CliPath $cliPath -PipeName $matrixPipe `
      -OperationArguments @("devices")
  Assert-Accepted -Result $devices -Step "matrix-devices"
  $channels = Get-DefaultRenderChannels $devices
  $asio = Invoke-ControlCli -CliPath $cliPath -PipeName $matrixPipe `
      -OperationArguments @("virtual-asio-list")
  Assert-Accepted -Result $asio -Step "matrix-virtual-asio"
  $asioDefinitions = [string]::Join("`n", @($asio.Lines | Select-Object -Skip 1))
  if ([string]::IsNullOrWhiteSpace($asioDefinitions)) {
    throw "matrix session has no Virtual ASIO definitions"
  }

  $configured = Invoke-ControlCli -CliPath $cliPath -PipeName $matrixPipe `
      -OperationArguments @("runtime-configure-matrix", "render", "sar-retired",
          "default", "0", [string]$channels, "master")
  Assert-Accepted -Result $configured -Step "matrix-configure-retired"
  $initial = Assert-PersistedPreset $cliPath $matrixPipe $matrixPath `
      (Join-Path $OutputDirectory "matrix-retired-initial.sarp") $failedStep
  Assert-SessionMatrixEndpoint $initial "sar-retired"
  $route = Invoke-ControlCli -CliPath $cliPath -PipeName $matrixPipe `
      -OperationArguments @("connect-route", "asio-output-l", "sar-retired.ch1", "0.375")
  Assert-Accepted -Result $route -Step "matrix-retired-route"
  $oldParts = Assert-PersistedPreset $cliPath $matrixPipe $matrixPath `
      (Join-Path $OutputDirectory "matrix-retired.sarp") "matrix-retired-route"
  Stop-SessionServiceGracefully $service $servicePath $matrixPipe
  $service = $null

  $failedStep = "matrix-stopped-restore"
  $matrixPipe = "sar-session-matrix-stopped-$PID-$runId"
  $service = Start-SessionService -ServicePath $servicePath -PipeName $matrixPipe `
      -SessionPath $matrixPath -LogPrefix (Join-Path $OutputDirectory "matrix-stopped")
  $state = Wait-ServiceReady -Service $service -CliPath $cliPath `
      -PipeName $matrixPipe -TimeoutSeconds $ReadyTimeoutSeconds
  if ($state.Lines[0] -notmatch '(?:^|\s)runtime_installed=true(?:\s|$)' -or
      $state.Lines[0] -notmatch '(?:^|\s)runtime_running=false(?:\s|$)') {
    throw "stopped matrix session did not restore without auto-start"
  }
  $started = Invoke-ControlCli -CliPath $cliPath -PipeName $matrixPipe `
      -OperationArguments @("runtime-start")
  Assert-Accepted -Result $started -Step "matrix-runtime-start"
  $configured = Invoke-ControlCli -CliPath $cliPath -PipeName $matrixPipe `
      -OperationArguments @("runtime-configure-matrix", "render", "sar-current",
          "default", "0", [string]$channels, "master")
  Assert-Accepted -Result $configured -Step "matrix-configure-current"
  if ($configured.Lines[0] -notmatch '(?:^|\s)runtime_running=true(?:\s|$)') {
    throw "successful live configuration did not retain the running state"
  }
  $current = Assert-PersistedPreset $cliPath $matrixPipe $matrixPath `
      (Join-Path $OutputDirectory "matrix-current.sarp") "matrix-configure-current"
  Assert-SessionMatrixEndpoint $current "sar-current"
  $currentText = [Text.Encoding]::ASCII.GetString($current.Preset)
  if ($currentText.Contains("sar-retired.ch1") -or !$currentText.Contains("sar-current.ch1")) {
    throw "removed endpoint or its route remained in the persisted preset"
  }

  $failedStep = "matrix-configure-rollback"
  $beforeRejected = (Get-FileHash -LiteralPath $matrixPath -Algorithm SHA256).Hash
  $rejected = Invoke-ControlCli -CliPath $cliPath -PipeName $matrixPipe `
      -OperationArguments @("runtime-configure-matrix", "render", "sar-rejected",
          "sar-test-unavailable-device", "0", [string]$channels, "master")
  if ($rejected.ExitCode -eq 0 -or $rejected.Text -notmatch 'wasapi_device_lookup_failed') {
    throw "unavailable endpoint did not reach the runtime rollback path: $($rejected.Text)"
  }
  if ((Get-FileHash -LiteralPath $matrixPath -Algorithm SHA256).Hash -ne $beforeRejected) {
    throw "rejected configuration changed the persisted session"
  }
  $state = Invoke-ControlCli -CliPath $cliPath -PipeName $matrixPipe `
      -OperationArguments @("runtime-state")
  Assert-Accepted -Result $state -Step "matrix-rollback-state"
  if ($state.Lines[0] -notmatch '(?:^|\s)runtime_running=true(?:\s|$)') {
    throw "rejected configuration did not restore the running runtime"
  }
  $null = Assert-PersistedPreset $cliPath $matrixPipe $matrixPath `
      (Join-Path $OutputDirectory "matrix-rollback.sarp") "matrix-rollback-preset"
  Stop-SessionServiceGracefully $service $servicePath $matrixPipe
  $service = $null

  $failedStep = "matrix-running-restore"
  $matrixPipe = "sar-session-matrix-running-$PID-$runId"
  $service = Start-SessionService -ServicePath $servicePath -PipeName $matrixPipe `
      -SessionPath $matrixPath -LogPrefix (Join-Path $OutputDirectory "matrix-running")
  $state = Wait-ServiceReady -Service $service -CliPath $cliPath `
      -PipeName $matrixPipe -TimeoutSeconds $ReadyTimeoutSeconds
  if ($state.Lines[0] -notmatch '(?:^|\s)runtime_running=true(?:\s|$)' -or
      $state.Lines[0] -notmatch '(?:^|\s)runtime_mode=wasapi-matrix(?:\s|$)') {
    throw "running matrix session did not auto-start after restart"
  }
  $restored = Assert-PersistedPreset $cliPath $matrixPipe $matrixPath `
      (Join-Path $OutputDirectory "matrix-running.sarp") $failedStep
  Assert-SessionMatrixEndpoint $restored "sar-current"
  $asio = Invoke-ControlCli -CliPath $cliPath -PipeName $matrixPipe `
      -OperationArguments @("virtual-asio-list")
  Assert-Accepted -Result $asio -Step "matrix-restored-virtual-asio"
  if ([string]::Join("`n", @($asio.Lines | Select-Object -Skip 1)) -cne $asioDefinitions) {
    throw "Virtual ASIO definitions changed across configuration or restart"
  }
  Stop-SessionServiceGracefully $service $servicePath $matrixPipe
  $service = $null

  $failedStep = "matrix-stale-preset-restore"
  $current = Read-SessionParts $matrixPath
  Write-SessionParts $matrixPath $current $oldParts.Preset $current.Tail
  $matrixPipe = "sar-session-matrix-stale-$PID-$runId"
  $service = Start-SessionService -ServicePath $servicePath -PipeName $matrixPipe `
      -SessionPath $matrixPath -LogPrefix (Join-Path $OutputDirectory "matrix-stale")
  $state = Wait-ServiceReady -Service $service -CliPath $cliPath `
      -PipeName $matrixPipe -TimeoutSeconds $ReadyTimeoutSeconds
  if ($state.Lines[0] -notmatch '(?:^|\s)runtime_running=true(?:\s|$)') {
    throw "stale-preset session did not retain auto-start"
  }
  $reconciled = Assert-PersistedPreset $cliPath $matrixPipe $matrixPath `
      (Join-Path $OutputDirectory "matrix-reconciled.sarp") $failedStep
  Assert-SessionMatrixEndpoint $reconciled "sar-current"
  $reconciledText = [Text.Encoding]::ASCII.GetString($reconciled.Preset)
  if ([regex]::Matches($reconciledText, [regex]::Escape("sar-retired.ch1")).Count -lt 2 -or
      !$reconciledText.Contains("sar-current.ch1")) {
    throw "restore did not preserve the unknown old port and route or add the configured port"
  }
  $asio = Invoke-ControlCli -CliPath $cliPath -PipeName $matrixPipe `
      -OperationArguments @("virtual-asio-list")
  Assert-Accepted -Result $asio -Step "matrix-reconciled-virtual-asio"
  if ([string]::Join("`n", @($asio.Lines | Select-Object -Skip 1)) -cne $asioDefinitions) {
    throw "Virtual ASIO definitions changed during stale-preset restore"
  }
  Stop-SessionServiceGracefully $service $servicePath $matrixPipe
  $service = $null

  $failedStep = "matrix-failed-restore-preservation"
  Set-SessionMatrixDeviceUnavailable $matrixPath
  $unavailableHash = (Get-FileHash -LiteralPath $matrixPath -Algorithm SHA256).Hash
  $matrixPipe = "sar-session-matrix-unavailable-$PID-$runId"
  $service = Start-SessionService -ServicePath $servicePath -PipeName $matrixPipe `
      -SessionPath $matrixPath -LogPrefix (Join-Path $OutputDirectory "matrix-unavailable")
  $state = Wait-ServiceReady -Service $service -CliPath $cliPath `
      -PipeName $matrixPipe -TimeoutSeconds $ReadyTimeoutSeconds
  if ($state.Lines[0] -notmatch '(?:^|\s)runtime_installed=false(?:\s|$)') {
    throw "unavailable endpoint unexpectedly restored"
  }
  if ((Get-FileHash -LiteralPath $matrixPath -Algorithm SHA256).Hash -ne $unavailableHash) {
    throw "failed restore changed the desired session"
  }
  Stop-SessionServiceGracefully $service $servicePath $matrixPipe
  $service = $null
  if ((Get-FileHash -LiteralPath $matrixPath -Algorithm SHA256).Hash -ne $unavailableHash) {
    throw "shutdown after failed restore lost the desired session or auto-start intent"
  }
  $unavailableLog = [IO.File]::ReadAllText((Join-Path $OutputDirectory "matrix-unavailable.stderr.log"))
  if ($unavailableLog -notmatch 'runtime_restore_failed detail=wasapi_device_lookup_failed') {
    throw "unavailable session did not exercise the runtime restore failure path"
  }

  $failedStep = "matrix-reconfigure-after-failed-restore"
  $matrixPipe = "sar-session-matrix-reconfigure-$PID-$runId"
  $service = Start-SessionService -ServicePath $servicePath -PipeName $matrixPipe `
      -SessionPath $matrixPath -LogPrefix (Join-Path $OutputDirectory "matrix-reconfigure")
  $null = Wait-ServiceReady -Service $service -CliPath $cliPath `
      -PipeName $matrixPipe -TimeoutSeconds $ReadyTimeoutSeconds
  $configured = Invoke-ControlCli -CliPath $cliPath -PipeName $matrixPipe `
      -OperationArguments @("runtime-configure-matrix", "render", "sar-current",
          "default", "0", [string]$channels, "master")
  Assert-Accepted -Result $configured -Step $failedStep
  if ($configured.Lines[0] -notmatch '(?:^|\s)runtime_running=false(?:\s|$)') {
    throw "reconfiguration unexpectedly started the unavailable runtime"
  }
  $null = Assert-PersistedPreset $cliPath $matrixPipe $matrixPath `
      (Join-Path $OutputDirectory "matrix-reconfigured.sarp") $failedStep
  Stop-SessionServiceGracefully $service $servicePath $matrixPipe
  $service = $null
  $matrixPipe = "sar-session-matrix-reconfigured-$PID-$runId"
  $service = Start-SessionService -ServicePath $servicePath -PipeName $matrixPipe `
      -SessionPath $matrixPath -LogPrefix (Join-Path $OutputDirectory "matrix-reconfigured")
  $state = Wait-ServiceReady -Service $service -CliPath $cliPath `
      -PipeName $matrixPipe -TimeoutSeconds $ReadyTimeoutSeconds
  if ($state.Lines[0] -notmatch '(?:^|\s)runtime_running=true(?:\s|$)') {
    throw "reconfiguration after failed restore lost auto-start intent"
  }
  Stop-SessionServiceGracefully $service $servicePath $matrixPipe
  $service = $null
  $matrixPersistenceVerified = $true

  $failedStep = "corrupt-preservation"
  $corruptPath = Join-Path $OutputDirectory "corrupt-session.sars"
  [IO.File]::WriteAllBytes($corruptPath, [byte[]](0x53, 0x41, 0x52, 0x53, 0xFF, 0x00, 0x7F))
  $corruptHash = (Get-FileHash -LiteralPath $corruptPath -Algorithm SHA256).Hash
  $corruptPipe = "sar-session-corrupt-$PID-$runId"
  $service = Start-SessionService -ServicePath $servicePath -PipeName $corruptPipe `
      -SessionPath $corruptPath -LogPrefix (Join-Path $OutputDirectory "corrupt") -Once
  $null = Wait-ServiceReady -Service $service -CliPath $cliPath `
      -PipeName $corruptPipe -TimeoutSeconds $ReadyTimeoutSeconds
  if (!$service.Process.WaitForExit(5000)) { throw "corrupt-file service did not exit in --once mode" }
  $stderr = [string]::Join([Environment]::NewLine,
      @(Get-Content -LiteralPath $service.StderrPath))
  $corruptPreserved = ((Get-FileHash -LiteralPath $corruptPath -Algorithm SHA256).Hash `
      -eq $corruptHash)
  if (!$corruptPreserved) { throw "corrupt session file was overwritten" }
  if ($stderr -notmatch 'session_warning code=session_load_failed') {
    throw "corrupt session did not emit the expected recovery warning"
  }
  $service = $null
  $passed = $true
} catch {
  $failureReason = $_.Exception.Message
} finally {
  if ($null -ne $service) {
    $cleanupComplete = (Stop-TestProcess -Service $service) -and $cleanupComplete
  }
}

if (!$cleanupComplete) {
  $passed = $false
  if ($failureReason -eq "none") { $failureReason = "service_process_cleanup_failed" }
}

Write-Output (
    "engine_session_acceptance" +
    " passed=$(if ($passed) { 1 } else { 0 })" +
    " failed_step=$(if ($passed) { 'none' } else { $failedStep })" +
    " session_bytes=$sessionBytes" +
    " restored_processed_blocks=$processedBlocks" +
    " audio_progress_verified=$(if ($audioProgressVerified) { 1 } else { 0 })" +
    " skip_audio_progress_check=$(if ($SkipAudioProgressCheck) { 1 } else { 0 })" +
    " matrix_persistence_verified=$(if ($matrixPersistenceVerified) { 1 } else { 0 })" +
    " corrupt_preserved=$(if ($corruptPreserved) { 1 } else { 0 })" +
    " cleanup_complete=$(if ($cleanupComplete) { 1 } else { 0 })" +
    " output_directory=$OutputDirectory" +
    " reason=$failureReason")

if ($passed) { exit 0 }
exit 1
