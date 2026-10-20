[CmdletBinding()]
param([Parameter(Mandatory)][string]$Executable,
  [string]$OutputDirectory=(Join-Path ([IO.Path]::GetTempPath()) ('sar-native-startup-'+[guid]::NewGuid().ToString('N'))))
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'windows-lab-process.psm1') -Force
$null=New-Item -ItemType Directory -Path $OutputDirectory -Force
$result=Invoke-SarLabProcess -FilePath (Resolve-Path -LiteralPath $Executable).Path `
  -ArgumentList @('--startup-smoke') -OutputDirectory $OutputDirectory -Name 'native-gui-startup' `
  -TimeoutMilliseconds 15000
$result | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
if($result.failure_reason -ne '' -or $result.exit_code -ne 0) {
  throw "Native GUI startup failed: $($result.failure_reason) exit=$($result.exit_code); report=$OutputDirectory"
}
Write-Output "native_gui_startup passed=1 elapsed_ms=$($result.elapsed_ms)"
