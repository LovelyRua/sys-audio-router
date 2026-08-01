param(
  [string]$InstallDirectory =
      (Join-Path $env:LOCALAPPDATA "Programs\SystemAudioRoute")
)

$ErrorActionPreference = "Stop"

function Test-PathBelowDirectory {
  param(
    [string]$CandidatePath,
    [string]$DirectoryPath
  )

  $candidate = [IO.Path]::GetFullPath($CandidatePath)
  $directoryPrefix =
      [IO.Path]::GetFullPath($DirectoryPath).TrimEnd(
          [IO.Path]::DirectorySeparatorChar,
          [IO.Path]::AltDirectorySeparatorChar) +
      [IO.Path]::DirectorySeparatorChar
  return $candidate.StartsWith(
      $directoryPrefix,
      [StringComparison]::OrdinalIgnoreCase)
}

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
          Test-PathBelowDirectory `
              -CandidatePath $_.Path `
              -DirectoryPath $installPath
        } catch {
          $false
        }
      })
  if ($running.Count -ne 0) {
    throw "Close installed process '$processName' before updating."
  }
}

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
    try {
      Remove-Item -LiteralPath $backupPath -Recurse -Force
    } catch {
      Write-Warning "Installed successfully but could not remove the previous payload backup: $($_.Exception.Message)"
    }
  }
  $result = ("alpha_install status=installed directory=`"{0}`" " +
      "driver=`"{1}`"") -f $installPath, $driverPath
  Write-Host $result
} catch {
  $primaryError = $_
  $rollbackErrors = [System.Collections.Generic.List[string]]::new()
  $newPayloadRemoved = !$installedNewPayload
  if ($installedNewPayload -and (Test-Path -LiteralPath $installPath)) {
    $newRegister = Join-Path $installPath "bin\sar_virtual_asio_register.exe"
    if (Test-Path -LiteralPath $newRegister -PathType Leaf) {
      & $newRegister --unregister --user --x64 2>$null | Out-Null
      if ($LASTEXITCODE -ne 0) {
        $rollbackErrors.Add(
            "unregister_new_payload exit_code=$LASTEXITCODE")
      }
    }
    try {
      Remove-Item -LiteralPath $installPath -Recurse -Force
      $newPayloadRemoved = !(Test-Path -LiteralPath $installPath)
    } catch {
      $rollbackErrors.Add("remove_new_payload error=$($_.Exception.Message)")
    }
  }
  if (Test-Path -LiteralPath $backupPath) {
    if (!$newPayloadRemoved) {
      $rollbackErrors.Add("restore_previous_install skipped_new_payload_remaining")
    } else {
      try {
        Move-Item -LiteralPath $backupPath -Destination $installPath
      } catch {
        $rollbackErrors.Add("restore_previous_install error=$($_.Exception.Message)")
      }
    }
  }
  if ($null -ne $previousDriverPath) {
    if (!(Test-Path -LiteralPath $previousDriverPath -PathType Leaf)) {
      $rollbackErrors.Add("restore_previous_asio missing_previous_driver")
    } else {
      $restoreRegister = Join-Path $sourceBin "sar_virtual_asio_register.exe"
      $restoredRegister = Join-Path $installPath "bin\sar_virtual_asio_register.exe"
      if (Test-Path -LiteralPath $restoredRegister -PathType Leaf) {
        $restoreRegister = $restoredRegister
      }
      & $restoreRegister --register $previousDriverPath --user --x64 2>$null
      if ($LASTEXITCODE -ne 0) {
        $rollbackErrors.Add("restore_previous_asio exit_code=$LASTEXITCODE")
      }
    }
  }
  if ($rollbackErrors.Count -ne 0) {
    $steps = [string]::Join(",", $rollbackErrors)
    Write-Host "alpha_install rollback=failed steps=$steps"
    throw ("$($primaryError.Exception.Message) Rollback failures: $steps")
  }
  throw $primaryError
} finally {
  if (Test-Path -LiteralPath $stagingPath) {
    Remove-Item -LiteralPath $stagingPath -Recurse -Force
  }
}
