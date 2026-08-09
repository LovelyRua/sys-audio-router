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
    'SAR_MEASURE_EVIDENCE_DIR',
    'SAR_MEASURE_MIN_FEED_FORWARD_READY_BPS',
    'SAR_MEASURE_MAX_CONSECUTIVE_CAPTURE_RATE_CLAMPED_FRAMES')) {
  Assert-True $cmdText.Contains($environmentName) `
      "CMD wrapper is missing $environmentName."
}
foreach ($parameterName in @(
    'MinimumFeedForwardReadyBasisPoints',
    'MaximumConsecutiveCaptureRateClampedFrames')) {
  Assert-True $scriptText.Contains("[string]`$$parameterName") `
      "Measurement script is missing optional parameter $parameterName."
  Assert-True $cmdText.Contains("-$parameterName") `
      "CMD wrapper does not forward $parameterName."
}
Assert-True $scriptText.Contains(
    '-MinimumFeedForwardReadyBasisPoints $MinimumFeedForwardReadyBasisPoints') `
    'Measurement script does not forward the feed-forward gate to Invoke-WasapiSoak.'
Assert-True $scriptText.Contains(
    '-MaximumConsecutiveCaptureRateClampedFrames $MaximumConsecutiveCaptureRateClampedFrames') `
    'Measurement script does not forward the clamp gate to Invoke-WasapiSoak.'

$powershellPath = (Get-Process -Id $PID).Path
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

foreach ($invalidGate in @(
    @('MinimumFeedForwardReadyBasisPoints', '10001',
      'MinimumFeedForwardReadyBasisPoints must be between 0 and 10000.'),
    @('MaximumConsecutiveCaptureRateClampedFrames', '-1',
      'MaximumConsecutiveCaptureRateClampedFrames must be a non-negative integer.'))) {
  $previousErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $gateOutput = @(& $powershellPath -NoProfile -ExecutionPolicy Bypass `
        -File $scriptPath -Password ignored `
        -RepoRoot (Join-Path $PSScriptRoot "..") `
        "-$($invalidGate[0])" $invalidGate[1] 2>&1 |
        ForEach-Object { [string]$_ })
    $gateExitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }
  Assert-True ($gateExitCode -ne 0) `
      "$($invalidGate[0]) should reject '$($invalidGate[1])'."
  Assert-True ([string]::Join("`n", $gateOutput)).Contains($invalidGate[2]) `
      "$($invalidGate[0]) did not report its validation reason."
}

Write-Output "WinRM local measurement tests passed"
