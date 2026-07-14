param(
  [Parameter(Mandatory = $true)]
  [string]$ConfigPath
)

$ErrorActionPreference = "Stop"

$config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
$resultPath = [string]$config.result_path
$temporaryResultPath = "$resultPath.tmp"
$exitCode = 1
$failure = $null

try {
  $previousErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $arguments = [string[]]$config.arguments
    & ([string]$config.executable_path) @arguments `
        1> ([string]$config.stdout_path) 2> ([string]$config.stderr_path)
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }
} catch {
  $failure = $_.Exception.ToString()
  $failure | Set-Content -LiteralPath ([string]$config.stderr_path) -Encoding UTF8
} finally {
  $record = [ordered]@{
    exit_code = $exitCode
    finished_utc = [datetime]::UtcNow.ToString('o')
    launch_error = $failure
  }
  $record | ConvertTo-Json -Compress |
      Set-Content -LiteralPath $temporaryResultPath -Encoding ASCII
  Move-Item -LiteralPath $temporaryResultPath -Destination $resultPath -Force
}

exit $exitCode
