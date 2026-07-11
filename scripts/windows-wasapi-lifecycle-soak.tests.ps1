$ErrorActionPreference = "Stop"

Import-Module (Join-Path $PSScriptRoot "windows-wasapi-lifecycle-soak.psm1") -Force

function Assert-Equal {
  param($Expected, $Actual, [string]$Message)
  if ($Expected -ne $Actual) { throw "$Message Expected '$Expected', got '$Actual'." }
}

foreach ($scriptName in @("windows-wasapi-lifecycle-soak.psm1", "windows-wasapi-lifecycle-soak.ps1")) {
  $tokens = $null
  $parseErrors = $null
  [void][System.Management.Automation.Language.Parser]::ParseFile(
      (Join-Path $PSScriptRoot $scriptName), [ref]$tokens, [ref]$parseErrors)
  Assert-Equal 0 $parseErrors.Count "Unexpected parser errors in '$scriptName'."
}

$calls = [System.Collections.Generic.List[string]]::new()
$outcomes = [System.Collections.Generic.Queue[string]]::new()
foreach ($outcome in @("success", "startup_failure", "runtime_failure", "stop_timeout", "success", "runtime_failure")) {
  $outcomes.Enqueue($outcome)
}
$output = @(Invoke-WasapiLifecycleSoak -Mode all -Iterations 2 -RunAttempt {
  param($modeName, $iteration)
  $calls.Add("$iteration`:$modeName") | Out-Null
  $outcome = $outcomes.Dequeue()
  return [pscustomobject]@{ Outcome = $outcome; ExitCode = if ($outcome -eq "success") { 0 } else { 1 } }
})
$result = $output[-1]
$text = [string]::Join("`n", $output[0..($output.Count - 2)])

Assert-Equal "1:render,1:duplex,1:loopback,2:render,2:duplex,2:loopback" ([string]::Join(",", $calls)) "Unexpected invocation order."
Assert-Equal 6 $result.Attempts "Unexpected attempt count."
Assert-Equal 1 $result.StartupFailureCount "Unexpected startup failure count."
Assert-Equal 2 $result.RuntimeFailureCount "Unexpected runtime failure count."
Assert-Equal 1 $result.StopTimeoutCount "Unexpected stop timeout count."
Assert-Equal 4 $result.FailureCount "Unexpected total failure count."
Assert-Equal 1 $result.ByMode.duplex.StartupFailures "Unexpected duplex startup failures."
Assert-Equal 1 $result.ByMode.render.StopTimeouts "Unexpected render stop timeouts."
Assert-Equal $true $text.Contains("mode=loopback attempts=2 startup_failures=0 runtime_failures=2 stop_timeouts=0") "Missing loopback summary."
Assert-Equal $true $text.Contains("completed attempts=6 startup_failures=1 runtime_failures=2 stop_timeouts=1 failures=4") "Missing aggregate summary."

$invalidOutcomeRejected = $false
try {
  Invoke-WasapiLifecycleSoak -Mode render -Iterations 1 -RunAttempt {
    [pscustomobject]@{ Outcome = "unknown"; ExitCode = 0 }
  } | Out-Null
} catch {
  $invalidOutcomeRejected = $true
}
Assert-Equal $true $invalidOutcomeRejected "Invalid outcomes should be rejected."

Write-Output "WASAPI lifecycle soak tests passed"
