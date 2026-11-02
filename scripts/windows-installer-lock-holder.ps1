[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$DllPath,
  [Parameter(Mandatory = $true)]
  [string]$ReadyFile,
  [Parameter(Mandatory = $true)]
  [string]$ReleaseFile
)

$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class SarInstallerDllLock {
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern IntPtr LoadLibrary(string path);
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern bool FreeLibrary(IntPtr module);
}
'@

$module = [SarInstallerDllLock]::LoadLibrary($DllPath)
if ($module -eq [IntPtr]::Zero) {
  throw "Could not load test DLL: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}
try {
  [IO.File]::WriteAllText($ReadyFile, [string]$PID)
  $deadline = [DateTime]::UtcNow.AddMinutes(2)
  while ([DateTime]::UtcNow -lt $deadline -and
         !(Test-Path -LiteralPath $ReleaseFile)) {
    Start-Sleep -Milliseconds 100
  }
} finally {
  [void][SarInstallerDllLock]::FreeLibrary($module)
}
