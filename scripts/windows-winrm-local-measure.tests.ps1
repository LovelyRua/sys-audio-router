$ErrorActionPreference = "Stop"

function Assert-True {
  param([bool]$Condition, [string]$Message)
  if (!$Condition) { throw $Message }
}

$scriptPath = Join-Path $PSScriptRoot "windows-winrm-local-measure.ps1"
$cmdPath = Join-Path $PSScriptRoot "windows-winrm-local-measure.cmd"
$tokens = $null
$parseErrors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
    $scriptPath, [ref]$tokens, [ref]$parseErrors)
Assert-True ($parseErrors.Count -eq 0) "Measurement script has parser errors."

$scriptText = Get-Content -LiteralPath $scriptPath -Raw
$cmdText = Get-Content -LiteralPath $cmdPath -Raw
foreach ($requiredText in @(
    'CaptureId and RenderId must be supplied together.',
    'manifest.json',
    '.command.txt',
    '.combined.log',
    'soak-summary.log',
    'Copy-Item -Path "$remoteEvidenceDirectory\*"')) {
  Assert-True $scriptText.Contains($requiredText) `
      "Measurement script is missing evidence contract '$requiredText'."
}
foreach ($environmentName in @(
    'SAR_MEASURE_CAPTURE_ID',
    'SAR_MEASURE_RENDER_ID',
    'SAR_MEASURE_EVIDENCE_DIR')) {
  Assert-True $cmdText.Contains($environmentName) `
      "CMD wrapper is missing $environmentName."
}

$powershellPath = Join-Path $PSHOME "powershell.exe"
$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
  $output = @(& $powershellPath -NoProfile -ExecutionPolicy Bypass -File $scriptPath `
      -Password ignored -RepoRoot (Join-Path $PSScriptRoot "..") `
      -CaptureId capture-only 2>&1 | ForEach-Object { [string]$_ })
  $exitCode = $LASTEXITCODE
} finally {
  $ErrorActionPreference = $previousErrorActionPreference
}
Assert-True ($exitCode -ne 0) "A single endpoint ID should be rejected."
Assert-True ([string]::Join("`n", $output)).Contains(
    "CaptureId and RenderId must be supplied together.") `
    "Endpoint-pair validation did not report its reason."

Write-Output "WinRM local measurement tests passed"
