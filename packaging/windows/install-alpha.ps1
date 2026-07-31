param(
  [string]$InstallDirectory =
      (Join-Path $env:LOCALAPPDATA "Programs\SystemAudioRoute")
)

$ErrorActionPreference = "Stop"
$packageRoot = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot "..\.."))
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot "bin") -PathType Container) {
  $packageRoot = [IO.Path]::GetFullPath($PSScriptRoot)
}
$sourceBin = Join-Path $packageRoot "bin"
$requiredPayload = @(
  "sar_engine_service.exe",
  "sar_control_cli.exe",
  "sar_virtual_asio_register.exe",
  "SystemAudioRouteVirtualASIO.dll",
  "SystemAudioRoute.exe",
  "Qt6Core.dll",
  "Qt6Quick.dll",
  "vc_redist.x64.exe"
)
foreach ($name in $requiredPayload) {
  $path = Join-Path $sourceBin $name
  if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Alpha package payload is missing '$path'."
  }
}
$requiredRuntimePayload = @(
  "plugins\platforms\qwindows.dll",
  "qml\QtQuick\qtquick2plugin.dll"
)
foreach ($relativePath in $requiredRuntimePayload) {
  $path = Join-Path $packageRoot $relativePath
  if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Alpha package runtime payload is missing '$path'."
  }
}
$redistPath = Join-Path $sourceBin "vc_redist.x64.exe"

$runtimeKey =
    "HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64"
$runtime = Get-ItemProperty -LiteralPath $runtimeKey `
    -ErrorAction SilentlyContinue
if ($null -eq $runtime -or $runtime.Installed -ne 1) {
  $redist = Start-Process -FilePath $redistPath `
      -ArgumentList "/install", "/passive", "/norestart" `
      -Wait -PassThru
  if ($redist.ExitCode -notin @(0, 1638, 3010)) {
    throw "Microsoft VC++ Runtime installation failed with exit code $($redist.ExitCode)."
  }
}

$installPath = [IO.Path]::GetFullPath($InstallDirectory.Trim().Trim('"'))
if ($installPath -eq $packageRoot) {
  throw "Install directory must not be the extracted package directory."
}
$markerName = ".system-audio-route-alpha"
$markerPath = Join-Path $installPath $markerName
if ((Test-Path -LiteralPath $installPath) -and
    !(Test-Path -LiteralPath $markerPath -PathType Leaf)) {
  throw "Install directory exists but is not a System Audio Route Alpha installation."
}
foreach ($processName in @("sar_engine_service", "SystemAudioRoute")) {
  $running = @(Get-Process -Name $processName -ErrorAction SilentlyContinue |
      Where-Object {
        try {
          $_.Path.StartsWith(
              $installPath,
              [StringComparison]::OrdinalIgnoreCase)
        } catch {
          $false
        }
      })
  if ($running.Count -ne 0) {
    throw "Close installed process '$processName' before updating."
  }
}

$parent = Split-Path -Parent $installPath
$leaf = Split-Path -Leaf $installPath
$suffix = [guid]::NewGuid().ToString("N").Substring(0, 8)
$stagingPath = Join-Path $parent "$leaf.staging-$suffix"
$backupPath = Join-Path $parent "$leaf.backup-$suffix"
$hadPreviousInstall = Test-Path -LiteralPath $installPath
$installedNewPayload = $false
$previousDriverPath = $null
$inprocKey = "HKCU:\Software\Classes\CLSID\" +
    "{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}\InprocServer32"
if (Test-Path -LiteralPath $inprocKey) {
  $candidate = (Get-Item -LiteralPath $inprocKey).GetValue("")
  if ($candidate -is [string] -and $candidate.Length -ne 0) {
    $previousDriverPath = $candidate
  }
}

try {
  New-Item -ItemType Directory -Path $stagingPath -Force | Out-Null
  Copy-Item -LiteralPath $sourceBin -Destination $stagingPath -Recurse
  foreach ($directoryName in @("plugins", "qml")) {
    Copy-Item -LiteralPath (Join-Path $packageRoot $directoryName) `
        -Destination $stagingPath -Recurse
  }
  Set-Content -LiteralPath (Join-Path $stagingPath $markerName) `
      -Value "System Audio Route Alpha 0.1.0" -Encoding ASCII
  foreach ($name in @(
      "uninstall-alpha.cmd",
      "uninstall-alpha.ps1",
      "LICENSE",
      "NOTICE.md",
      "README.md")) {
    $source = Join-Path $packageRoot $name
    if (Test-Path -LiteralPath $source -PathType Leaf) {
      Copy-Item -LiteralPath $source -Destination $stagingPath
    }
  }

  if ($hadPreviousInstall) {
    Move-Item -LiteralPath $installPath -Destination $backupPath
  }
  Move-Item -LiteralPath $stagingPath -Destination $installPath
  $installedNewPayload = $true

  $installedBin = Join-Path $installPath "bin"
  $registerPath = Join-Path $installedBin "sar_virtual_asio_register.exe"
  $driverPath = Join-Path $installedBin "SystemAudioRouteVirtualASIO.dll"
  & $registerPath --register $driverPath --user --x64
  if ($LASTEXITCODE -ne 0) {
    throw "Virtual ASIO registration failed with exit code $LASTEXITCODE."
  }
  & $registerPath --verify $driverPath --user --x64
  if ($LASTEXITCODE -ne 0) {
    throw "Virtual ASIO verification failed with exit code $LASTEXITCODE."
  }

  if (Test-Path -LiteralPath $backupPath) {
    Remove-Item -LiteralPath $backupPath -Recurse -Force
  }
  $result = ("alpha_install status=installed directory=`"{0}`" " +
      "driver=`"{1}`"") -f $installPath, $driverPath
  Write-Host $result
} catch {
  if ($installedNewPayload -and (Test-Path -LiteralPath $installPath)) {
    $newRegister = Join-Path $installPath "bin\sar_virtual_asio_register.exe"
    if (Test-Path -LiteralPath $newRegister -PathType Leaf) {
      & $newRegister --unregister --user --x64 2>$null | Out-Null
    }
    Remove-Item -LiteralPath $installPath -Recurse -Force
  }
  if (Test-Path -LiteralPath $backupPath) {
    Move-Item -LiteralPath $backupPath -Destination $installPath
    $oldRegister = Join-Path $installPath "bin\sar_virtual_asio_register.exe"
    $oldDriver = Join-Path $installPath "bin\SystemAudioRouteVirtualASIO.dll"
    if ((Test-Path -LiteralPath $oldRegister -PathType Leaf) -and
        (Test-Path -LiteralPath $oldDriver -PathType Leaf)) {
      & $oldRegister --register $oldDriver --user --x64 2>$null
    }
  } elseif ($null -ne $previousDriverPath -and
      (Test-Path -LiteralPath $previousDriverPath -PathType Leaf)) {
    $sourceRegister = Join-Path $sourceBin "sar_virtual_asio_register.exe"
    & $sourceRegister --register $previousDriverPath --user --x64 2>$null
  }
  throw
} finally {
  if (Test-Path -LiteralPath $stagingPath) {
    Remove-Item -LiteralPath $stagingPath -Recurse -Force
  }
}
