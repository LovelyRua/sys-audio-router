$ErrorActionPreference = "Stop"

Import-Module (Join-Path $PSScriptRoot "windows-wasapi-soak-runner.psm1") -Force

function Assert-Equal {
  param($Expected, $Actual, [string]$Message)
  if ($Expected -ne $Actual) {
    throw "$Message Expected '$Expected', got '$Actual'."
  }
}

foreach ($scriptName in @("windows-wasapi-soak-runner.psm1", "windows-winrm-local-measure.ps1")) {
  $tokens = $null
  $parseErrors = $null
  [void][System.Management.Automation.Language.Parser]::ParseFile(
      (Join-Path $PSScriptRoot $scriptName), [ref]$tokens, [ref]$parseErrors)
  Assert-Equal 0 $parseErrors.Count "Unexpected parser errors in '$scriptName'."
}

$expectedModes = @{
  render = "render"
  duplex = "duplex"
  loopback = "loopback"
  both = "render,duplex"
  all = "render,duplex,loopback"
}
foreach ($mode in $expectedModes.Keys) {
  $actual = [string]::Join(",", @(Get-WasapiSoakModeNames -Mode $mode))
  Assert-Equal $expectedModes[$mode] $actual "Unexpected expansion for mode '$mode'."
}

$calls = [System.Collections.Generic.List[string]]::new()
$output = @(Invoke-WasapiSoak -Mode all -Iterations 3 -RunMeasurement {
  param($modeName, $iteration)
  $calls.Add("$iteration`:$modeName") | Out-Null
  if (($modeName -eq "render" -and $iteration -eq 2) -or $modeName -eq "loopback") {
    return 1
  }
  return 0
})
$result = $output[-1]
$text = [string]::Join("`n", $output[0..($output.Count - 2)])

Assert-Equal 9 $calls.Count "Unexpected invocation count."
Assert-Equal "1:render,1:duplex,1:loopback,2:render,2:duplex,2:loopback,3:render,3:duplex,3:loopback" ([string]::Join(",", $calls)) "Unexpected invocation order."
Assert-Equal 3 $result.Iterations "Unexpected result iteration count."
Assert-Equal 9 $result.Attempts "Unexpected result attempt count."
Assert-Equal 4 $result.FailureCount "Unexpected total failure count."
Assert-Equal 1 $result.FailuresByMode.render "Unexpected render failure count."
Assert-Equal 0 $result.FailuresByMode.duplex "Unexpected duplex failure count."
Assert-Equal 3 $result.FailuresByMode.loopback "Unexpected loopback failure count."
Assert-Equal $true $text.Contains("[soak] mode=render iterations=3 failures=1") "Missing render summary."
Assert-Equal $true $text.Contains("[soak] mode=duplex iterations=3 failures=0") "Missing duplex summary."
Assert-Equal $true $text.Contains("[soak] mode=loopback iterations=3 failures=3") "Missing loopback summary."
Assert-Equal $true $text.Contains("[soak] completed modes=3 iterations=3 attempts=9 failures=4") "Missing aggregate summary."

$nullResult = @(Invoke-WasapiSoak -Mode render -Iterations 2 -RunMeasurement {
  return $null
})[-1]
Assert-Equal 0 $nullResult.FailureCount "Null runner output should mean success."

$invalidIterationsRejected = $false
try {
  Invoke-WasapiSoak -Mode render -Iterations 0 -RunMeasurement { return 0 } | Out-Null
} catch {
  $invalidIterationsRejected = $true
}
Assert-Equal $true $invalidIterationsRejected "Zero iterations should be rejected."

Write-Output "WASAPI soak runner tests passed"
