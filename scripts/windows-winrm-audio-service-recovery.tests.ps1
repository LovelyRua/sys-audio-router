$ErrorActionPreference = "Stop"

function Assert-True {
  param([bool]$Condition, [string]$Message)
  if (!$Condition) { throw $Message }
}

function Assert-Parses {
  param([string]$Path)
  $tokens = $null
  $errors = $null
  [void][System.Management.Automation.Language.Parser]::ParseFile(
      $Path, [ref]$tokens, [ref]$errors)
  Assert-True ($errors.Count -eq 0) `
      "Unexpected parser errors in '$Path': $($errors.Message -join '; ')"
}

$scriptPath = Join-Path $PSScriptRoot "windows-winrm-audio-service-recovery.ps1"
$wrapperPath = Join-Path $PSScriptRoot "windows-winrm-audio-service-recovery.cmd"
Assert-Parses -Path $scriptPath
$scriptText = Get-Content -LiteralPath $scriptPath -Raw
$wrapperText = Get-Content -LiteralPath $wrapperPath -Raw

Assert-True ($scriptText -match 'Restart-Service\s+-Name\s+Audiosrv') `
    "The acceptance does not restart Windows Audio."
Assert-True ($scriptText -match '--render-only') `
    "The acceptance does not use the render-only recovery path."
Assert-True ($scriptText -match '-AllowEmptyCaptureDeviceId') `
    "The render-only log is not parsed with the render-only contract."
Assert-True ($scriptText -match '\[guid\]::NewGuid\(\)') `
    "Runs do not receive unique evidence identities."
Assert-True ($scriptText -match '\[IO.FileShare\]::None') `
    "Concurrent users can enter the same test slot."
Assert-True ($scriptText -match 'WaitForExit\(\$waitMs\)') `
    "The remote process wait is not bounded."
Assert-True ($scriptText -notmatch 'password\s*=\s*\$Password') `
    "The plaintext password appears to be persisted."
Assert-True ($wrapperText -match 'SAR_TEST_PASSWORD') `
    "The wrapper does not support an ephemeral password environment variable."
Assert-True ($wrapperText -match 'SAR_RECOVERY_EVIDENCE_DIR') `
    "The wrapper does not support isolated local evidence output."

Write-Output "Audio-service recovery script tests passed"
