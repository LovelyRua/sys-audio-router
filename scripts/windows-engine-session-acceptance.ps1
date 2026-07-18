[CmdletBinding()]
param(
  [string]$BuildPath = (Join-Path $PSScriptRoot "..\build"),

  [ValidateSet("", "Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
  [string]$Configuration = "",

  [ValidateRange(1, 120)]
  [int]$ReadyTimeoutSeconds = 15,

  [ValidateRange(0, 60000)]
  [int]$DiagnosticsDelayMilliseconds = 500,

  [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"

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
  param([string]$CliPath, [string]$PipeName, [string[]]$OperationArguments)

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

function Assert-Accepted {
  param([pscustomobject]$Result, [string]$Step)

  if ($Result.ExitCode -ne 0 -or $Result.Lines.Count -eq 0 -or
      $Result.Lines[0] -notmatch '^control_response status=accepted command_id=cli-1(?:\s|$)') {
    throw "$Step did not return an accepted response: $($Result.Text)"
  }
}

function Start-SessionService {
  param(
    [string]$ServicePath,
    [string]$PipeName,
    [string]$SessionPath,
    [string]$LogPrefix,
    [switch]$Once
  )

  $arguments = @("--pipe", $PipeName, "--session", $SessionPath)
  if ($Once) { $arguments += "--once" }
  $stdoutPath = "$LogPrefix.stdout.log"
  $stderrPath = "$LogPrefix.stderr.log"
  $process = Start-Process -FilePath $ServicePath -ArgumentList $arguments `
      -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdoutPath `
      -RedirectStandardError $stderrPath
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
    $result = Invoke-ControlCli -CliPath $CliPath -PipeName $PipeName `
        -OperationArguments @("runtime-state")
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
$sessionBytes = 0
$corruptPreserved = $false

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
      -OperationArguments @("set-gain", "input-l", "output-l", "0.25")
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
  if ($processedBlocks -lt 1) { throw "restored runtime processed no audio blocks" }
  $stopped = Invoke-ControlCli -CliPath $cliPath -PipeName $secondPipe `
      -OperationArguments @("runtime-stop")
  Assert-Accepted -Result $stopped -Step "restored-runtime-stop"
  $cleanupComplete = Stop-TestProcess -Service $service
  $service = $null
  if (!$cleanupComplete) { throw "restored service process did not stop" }

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
    " corrupt_preserved=$(if ($corruptPreserved) { 1 } else { 0 })" +
    " cleanup_complete=$(if ($cleanupComplete) { 1 } else { 0 })" +
    " output_directory=$OutputDirectory" +
    " reason=$failureReason")

if ($passed) { exit 0 }
exit 1
