$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'windows-lab-process.psm1') -Force
function Assert-True([bool]$Condition, [string]$Message) {
  if (!$Condition) { throw $Message }
}
Assert-True ((ConvertTo-SarLabArgument '') -eq '""') 'Empty argument was lost.'
Assert-True ((ConvertTo-SarLabArgument 'two words') -eq '"two words"') 'Spaces were not quoted.'
Assert-True ((ConvertTo-SarLabArgument 'C:\ending\') -eq '"C:\ending\\"') 'Trailing slash was not escaped.'
Assert-True ((ConvertTo-SarLabArgument 'a"b') -eq '"a\"b"') 'Embedded quote was not escaped.'
$rejected = $false
try { $null = ConvertTo-SarLabArgument "bad`nargument" } catch { $rejected = $true }
Assert-True $rejected 'Newline argument was accepted.'

$root = Join-Path ([IO.Path]::GetTempPath()) ('sar-lab-process-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root
$shell = (Get-Process -Id $PID).Path
try {
  $fixture = Join-Path $root 'fixture.ps1'
  Set-Content -LiteralPath $fixture -Encoding ASCII -Value @'
param([string]$Mode, [string]$Value)
if ($Mode -eq 'timeout') { Start-Sleep -Seconds 30; exit 0 }
if ($Mode -eq 'overflow') { [Console]::Out.Write(('x' * 20000)); exit 0 }
[Console]::Out.WriteLine($Value)
[Console]::Error.WriteLine('diagnostic')
exit 7
'@
  $result = Invoke-SarLabProcess -FilePath $shell -ArgumentList @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $fixture, 'echo', 'two words'
  ) -OutputDirectory $root -Name normal
  Assert-True ($result.failure_reason -eq '' -and $result.exit_code -eq 7) 'Exit status was lost.'
  Assert-True ($result.stdout_lines.Count -eq 1 -and $result.stdout_lines[0] -eq 'two words') 'Argument round-trip failed.'
  Assert-True ($result.stderr_lines.Count -eq 1 -and $result.stderr_lines[0] -eq 'diagnostic') 'stderr was lost.'

  $timed = Invoke-SarLabProcess -FilePath $shell -ArgumentList @(
    '-NoProfile', '-File', $fixture, 'timeout'
  ) -OutputDirectory $root -Name timed -TimeoutMilliseconds 300
  Assert-True ($timed.failure_reason -eq 'process_timeout' -and $timed.elapsed_ms -lt 10000) 'Timeout was not bounded.'

  $overflow = Invoke-SarLabProcess -FilePath $shell -ArgumentList @(
    '-NoProfile', '-File', $fixture, 'overflow'
  ) -OutputDirectory $root -Name overflow -MaximumOutputBytes 1024
  Assert-True ($overflow.failure_reason -eq 'process_output_limit' -and $overflow.stdout_lines.Count -eq 0) 'Oversized output was loaded.'
  $rejected = $false
  try {
    $null = Invoke-SarLabProcess -FilePath $shell -OutputDirectory $root -Name normal
  } catch { $rejected = $true }
  Assert-True $rejected 'Existing logs were overwritten.'
} finally {
  Remove-Item -LiteralPath $root -Recurse -Force
}
Write-Output 'Lab process boundary tests passed'
