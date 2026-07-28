$ErrorActionPreference = "Stop"

function Assert-Equal {
  param($Expected, $Actual, [string]$Message)

  if ($Expected -ne $Actual) {
    throw "$Message Expected '$Expected', got '$Actual'."
  }
}

$scriptPath = Join-Path $PSScriptRoot "windows-winrm-test.ps1"
$tokens = $null
$parseErrors = $null
[System.Management.Automation.Language.Parser]::ParseFile(
    $scriptPath, [ref]$tokens, [ref]$parseErrors) | Out-Null
Assert-Equal 0 $parseErrors.Count "Unexpected parser errors in '$scriptPath'."

$scriptText = Get-Content -LiteralPath $scriptPath -Raw
$lockOpen = $scriptText.IndexOf("[System.IO.File]::Open(")
$bootstrapDownload = $scriptText.IndexOf("curl.exe --silent")
$lockDispose = $scriptText.IndexOf('$slotLock.Dispose()')
Assert-Equal $true ($lockOpen -ge 0) "The remote workflow does not acquire a slot lock."
Assert-Equal $true ($lockOpen -lt $bootstrapDownload) "The slot lock must cover bootstrap download."
Assert-Equal $true ($lockDispose -gt $bootstrapDownload) "The slot lock is released before the run finishes."
Assert-Equal $true $scriptText.Contains("[System.IO.FileShare]::None") `
    "The slot lock must exclude every competing process."
Assert-Equal $true $scriptText.Contains('"default"') `
    "The legacy empty slot must have an explicit lock namespace."
Assert-Equal $true $scriptText.Contains("already active") `
    "A competing run must report an actionable busy error."

$testRoot = Join-Path $env:TEMP "sar-slot-lock-$([guid]::NewGuid().ToString('N'))"
$lockPath = Join-Path $testRoot "engineer-a.lock"
$firstLock = $null
$secondLock = $null
try {
  New-Item -ItemType Directory -Path $testRoot | Out-Null
  $firstLock = [System.IO.File]::Open(
      $lockPath,
      [System.IO.FileMode]::OpenOrCreate,
      [System.IO.FileAccess]::ReadWrite,
      [System.IO.FileShare]::None)

  $wasRejected = $false
  try {
    $secondLock = [System.IO.File]::Open(
        $lockPath,
        [System.IO.FileMode]::OpenOrCreate,
        [System.IO.FileAccess]::ReadWrite,
        [System.IO.FileShare]::None)
  } catch [System.IO.IOException] {
    $wasRejected = $true
  }
  Assert-Equal $true $wasRejected "A second holder acquired the same slot lock."

  $firstLock.Dispose()
  $firstLock = $null
  $secondLock = [System.IO.File]::Open(
      $lockPath,
      [System.IO.FileMode]::OpenOrCreate,
      [System.IO.FileAccess]::ReadWrite,
      [System.IO.FileShare]::None)
} finally {
  if ($null -ne $secondLock) {
    $secondLock.Dispose()
  }
  if ($null -ne $firstLock) {
    $firstLock.Dispose()
  }
  Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Output "WinRM slot lock tests passed"
