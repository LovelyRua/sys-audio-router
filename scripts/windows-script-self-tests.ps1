param(
  [string]$ScriptsDirectory = $PSScriptRoot
)

$ErrorActionPreference = "Stop"
$root = [IO.Path]::GetFullPath($ScriptsDirectory)
$tests = @(Get-ChildItem -LiteralPath $root -Filter "*.tests.ps1" -File |
    Sort-Object Name)
if ($tests.Count -eq 0) {
  throw "No PowerShell script self-tests were found in '$root'."
}

$powershellPath = (Get-Process -Id $PID).Path
$failures = [System.Collections.Generic.List[string]]::new()
$started = [Diagnostics.Stopwatch]::StartNew()
foreach ($test in $tests) {
  Write-Host "[script-test] $($test.Name)"
  $previousErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $output = @(& $powershellPath -NoProfile -NonInteractive `
        -ExecutionPolicy Bypass -File $test.FullName 2>&1 |
        ForEach-Object { [string]$_ })
    $exitCode = [int]$LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }
  if ($exitCode -ne 0) {
    foreach ($line in $output) {
      Write-Host "  $line"
    }
    $failures.Add("$($test.Name):exit_$exitCode")
  } elseif ($output.Count -ne 0) {
    Write-Host "  $($output[$output.Count - 1])"
  }
}
$started.Stop()

if ($failures.Count -ne 0) {
  throw "PowerShell script self-tests failed: $([string]::Join(', ', $failures))."
}
Write-Output (("windows_script_self_tests passed=1 test_count={0} " +
    "elapsed_ms={1}") -f $tests.Count, $started.ElapsedMilliseconds)
