[CmdletBinding()]
param([Parameter(Mandatory)][string]$Executable,
  [string]$OutputDirectory=(Join-Path ([IO.Path]::GetTempPath()) ('sar-native-startup-'+[guid]::NewGuid().ToString('N'))))
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'windows-lab-process.psm1') -Force
$null=New-Item -ItemType Directory -Path $OutputDirectory -Force
$previousLanguage=$env:SAR_NATIVE_UI_LANGUAGE
try {
  foreach($language in @('en','zh_CN')) {
    $env:SAR_NATIVE_UI_LANGUAGE=$language
    $directory=Join-Path $OutputDirectory $language
    $result=Invoke-SarLabProcess -FilePath (Resolve-Path -LiteralPath $Executable).Path `
      -ArgumentList @('--startup-smoke') -OutputDirectory $directory -Name 'native-gui-startup' `
      -TimeoutMilliseconds 15000
    $result | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $directory 'result.json') -Encoding UTF8
    if($result.failure_reason -ne '' -or $result.exit_code -ne 0) {
      throw "Native GUI startup failed: language=$language $($result.failure_reason) exit=$($result.exit_code); report=$directory"
    }
    Write-Output "native_gui_startup passed=1 language=$language elapsed_ms=$($result.elapsed_ms)"
  }
} finally { $env:SAR_NATIVE_UI_LANGUAGE=$previousLanguage }
