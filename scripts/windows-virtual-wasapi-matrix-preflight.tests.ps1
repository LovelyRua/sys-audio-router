$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'windows-lab-process.psm1') -Force
function Assert-True([bool]$Condition, [string]$Message) {
  if (!$Condition) { throw $Message }
}
$root = Join-Path ([IO.Path]::GetTempPath()) ('sar-matrix-flow-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root
try {
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'windows-virtual-wasapi-matrix-preflight.ps1') -Destination $root
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'windows-virtual-wasapi-signal-evidence.psm1') -Destination $root
  foreach ($name in @('sar_control_cli.exe', 'sar_engine_service.exe', 'probe.exe')) {
    Set-Content -LiteralPath (Join-Path $root $name) -Value 'fixture' -Encoding ASCII
  }
  Set-Content -LiteralPath (Join-Path $root 'windows-lab-process.psm1') -Encoding ASCII -Value @'
function Get-SarLabSessionId { return 1 }
function Invoke-SarLabProcess {
  param($FilePath, $ArgumentList, $OutputDirectory, $Name, $TimeoutMilliseconds)
  $exitCode = 0; $failure = ''; $lines = @('control_response status=accepted request=1')
  $operation = if ($ArgumentList[0] -eq '--pipe') { $ArgumentList[2] } else { $ArgumentList[0] }
  Add-Content -LiteralPath $global:trace -Value $operation
  switch ($operation) {
    'devices' {
      $ids = @('matrix-capture', 'matrix-render', 'source-render', 'sink-capture')
      for ($index = 0; $index -lt 4; ++$index) {
        $flow = if ($index -eq 0 -or $index -eq 3) { 'input' } else { 'output' }
        $lines += "device index=$index id=`"$($ids[$index])`" name=`"test`" backend=wasapi direction=$flow active=1"
        $lines += "device_format device_index=$index index=0 sample_rate=48000 channels=2 bits=32"
      }
    }
    'connect-route' { $global:routes = $true }
    'disconnect-route' { $global:routes = $false }
    'diagnostics' { $global:blocks += 100; $lines[0] += " processed_blocks=$global:blocks" }
    'runtime-start' {
      if ($global:scenario -eq 'start-failure') { $exitCode = 1; $lines = @('control_response status=rejected') }
    }
    '--stop' {
      $global:stopRequested = $true
      if ($global:scenario -eq 'stop-failure') { $exitCode = 1 }
    }
    '--route' {
      $power = if ($global:routes -or $global:scenario -eq 'leakage') { '2e15' } else { '0' }
      $exitCode = if ($power -eq '0') { 3 } else { 0 }
      $sent = if ($global:scenario -eq 'no-frames' -and $global:routes) { 0 } else { 144000 }
      $nonzero = if ($power -eq '0') { 0 } else { 288000 }
      $lines = @("sent_frames=$sent captured_frames=192000 silent_frames=48000 capture_packets=400 discontinuity_packets=0 max_packet_frames=480 nonzero_samples=$nonzero",
        "target_power=$power cross_channel_power=0", "first_channel_power=$power second_channel_power=$power")
      if ($global:scenario -eq 'probe-timeout') { $failure = 'process_timeout' }
    }
  }
  $stdout = Join-Path $OutputDirectory "$Name.stdout.log"
  $stderr = Join-Path $OutputDirectory "$Name.stderr.log"
  $lines | Set-Content -LiteralPath $stdout -Encoding ASCII
  return [pscustomobject]@{
    exit_code=$exitCode; failure_reason=$failure; elapsed_ms=1
    stdout_lines=$lines; stderr_lines=@(); stdout_path=$stdout; stderr_path=$stderr
  }
}
Export-ModuleMember -Function Get-SarLabSessionId, Invoke-SarLabProcess
'@
  $harness = Join-Path $root 'harness.ps1'
  Set-Content -LiteralPath $harness -Encoding ASCII -Value @'
param([string]$Scenario, [string]$OutputDirectory, [string]$Mode)
$global:scenario=$Scenario; $global:routes=$false; $global:blocks=0; $global:stopRequested=$false
$global:trace=Join-Path $OutputDirectory 'operations.log'
function global:Start-Sleep { param($Milliseconds, $Seconds) }
function global:Start-Process {
  param($FilePath, $ArgumentList, [switch]$PassThru, $WindowStyle)
  Add-Content -LiteralPath $global:trace -Value 'engine-start'
  $fake = [pscustomobject]@{HasExited=$false; Handle=1; ExitCode=0}
  $fake | Add-Member ScriptMethod WaitForExit {
    param($Milliseconds)
    $this.HasExited=$true
    if ($global:scenario -eq 'engine-exit-failure') { $this.ExitCode=17 }
    return $true
  }
  $fake | Add-Member ScriptMethod Kill { $this.HasExited=$true }
  $fake | Add-Member ScriptMethod Refresh {}
  $fake | Add-Member ScriptMethod Dispose {}
  return $fake
}
$parameters=@{
  CaptureDeviceId='matrix-capture'; RenderDeviceId='matrix-render'; BuildPath=$PSScriptRoot
  SignalProbePath=(Join-Path $PSScriptRoot 'probe.exe')
  ProbeRenderDeviceId='source-render'; ProbeCaptureDeviceId='sink-capture'
  OutputDirectory=$OutputDirectory; ObserveSeconds=1
}
if ($Mode -eq 'transitions') { $parameters.ExerciseRouteTransitions=$true }
if ($Mode -eq 'silent') { $parameters.SkipRoutes=$true }
& (Join-Path $PSScriptRoot 'windows-virtual-wasapi-matrix-preflight.ps1') @parameters
exit $LASTEXITCODE
'@
  $shell = (Get-Process -Id $PID).Path
  foreach ($case in @(
    @{name='transitions'; scenario='normal'; mode='transitions'; pass=$true; phases=4},
    @{name='enabled'; scenario='normal'; mode='enabled'; pass=$true; phases=1},
    @{name='silent'; scenario='normal'; mode='silent'; pass=$true; phases=1},
    @{name='leakage'; scenario='leakage'; mode='transitions'; pass=$false; phases=1},
    @{name='timeout'; scenario='probe-timeout'; mode='transitions'; pass=$false; phases=1},
    @{name='frames'; scenario='no-frames'; mode='transitions'; pass=$false; phases=2},
    @{name='start'; scenario='start-failure'; mode='transitions'; pass=$false; phases=0},
    @{name='stop'; scenario='stop-failure'; mode='transitions'; pass=$false; phases=4},
    @{name='engine-exit'; scenario='engine-exit-failure'; mode='transitions'; pass=$false; phases=4}
  )) {
    $output = Join-Path $root $case.name
    $null = New-Item -ItemType Directory -Path $output
    $child = Invoke-SarLabProcess -FilePath $shell -ArgumentList @(
      '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $harness,
      $case.scenario, $output, $case.mode
    ) -OutputDirectory $root -Name ('case-' + $case.name) -TimeoutMilliseconds 15000
    Assert-True ($child.failure_reason -eq '') "Harness failed: $($case.name) $($child.failure_reason)"
    $resultPath = Join-Path $output 'result.json'
    Assert-True (Test-Path -LiteralPath $resultPath) "Result missing: $($case.name); $($child.stderr_lines -join '; ')"
    $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
    Assert-True ($result.passed -eq $case.pass) "Wrong pass result: $($case.name) $($result.reason)"
    Assert-True (($child.exit_code -eq 0) -eq $case.pass) "Wrong exit status: $($case.name)"
    Assert-True ($result.phases.Count -eq $case.phases) "Wrong phase count: $($case.name)"
    if ($case.name -eq 'engine-exit') {
      Assert-True (!$result.engine_stopped_gracefully -and $result.engine_exit_code -eq 17) 'Abnormal engine exit was hidden.'
    } else {
      Assert-True $result.engine_stopped_gracefully "Engine cleanup missing: $($case.name)"
    }
    $operations = @(Get-Content -LiteralPath (Join-Path $output 'operations.log'))
    Assert-True (@($operations | Where-Object { $_ -eq 'engine-start' }).Count -eq 1) 'Flow restarted the engine.'
    Assert-True ($operations -contains '--stop') 'Graceful stop was not requested.'
    if ($case.name -eq 'transitions') {
      Assert-True $result.route_transitions_checked 'Transitions were not verified.'
      Assert-True (($result.phases.name -join ',') -eq 'disabled-before,enabled,disabled-after,reenabled') 'Phase order changed.'
      Assert-True (($result.phases.routes_enabled -join ',') -eq 'False,True,False,True') 'Route state sequence changed.'
    }
  }
} finally {
  Remove-Item -LiteralPath $root -Recurse -Force
}
Write-Output 'Virtual WASAPI matrix user-flow tests passed (9 scenarios)'
