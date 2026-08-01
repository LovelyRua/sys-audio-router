[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$PackagePath,

  [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA `
      "Programs\SystemAudioRoute-Alpha-Acceptance"),

  [ValidateRange(1, 30)]
  [int]$GuiHealthSeconds = 4
)

$ErrorActionPreference = "Stop"

function Invoke-CommandScript {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [string[]]$Argument = @()
  )

  $previousPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $output = @(& $Path @Argument 2>&1 | ForEach-Object { [string]$_ })
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousPreference
  }
  return @{
    ExitCode = $exitCode
    Output = $output
  }
}

function Write-CommandOutput {
  param([hashtable]$Result)
  foreach ($line in $Result.Output) {
    Write-Output $line
  }
}

$resolvedPackagePath = [IO.Path]::GetFullPath($PackagePath.Trim().Trim('"'))
if (!(Test-Path -LiteralPath $resolvedPackagePath -PathType Leaf)) {
  throw "Alpha package was not found: $resolvedPackagePath"
}
if ([IO.Path]::GetExtension($resolvedPackagePath) -ne ".zip") {
  throw "Alpha package must be a ZIP file."
}

$installPath = [IO.Path]::GetFullPath($InstallDirectory.Trim().Trim('"'))
if (Test-Path -LiteralPath $installPath) {
  throw "Acceptance install directory must not already exist: $installPath"
}

$asioRegistrationKey =
    "HKCU:\Software\Classes\CLSID\" +
    "{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}\InprocServer32"
if (Test-Path -LiteralPath $asioRegistrationKey) {
  $registeredDriver = (Get-Item -LiteralPath $asioRegistrationKey).GetValue("")
  throw "Acceptance requires no existing Virtual ASIO registration; found '$registeredDriver'."
}

$suffix = [guid]::NewGuid().ToString("N").Substring(0, 8)
$workPath = Join-Path ([IO.Path]::GetTempPath()) "sar-alpha-acceptance-$suffix"
$negativeInstallPath = "$installPath.missing-runtime-$suffix"
$ownershipInstallPath = "$installPath.ownership-$suffix"
$guiStdoutPath = Join-Path $workPath "gui.stdout.txt"
$guiStderrPath = Join-Path $workPath "gui.stderr.txt"
$primaryInstalled = $false
$ownershipInstalled = $false
$guiProcess = $null
$packageRoot = $null
$packageHash = (Get-FileHash -Algorithm SHA256 `
    -LiteralPath $resolvedPackagePath).Hash
foreach ($unusedPath in @($negativeInstallPath, $ownershipInstallPath)) {
  if (Test-Path -LiteralPath $unusedPath) {
    throw "Acceptance scratch path must not already exist: $unusedPath"
  }
}

$savedEnvironment = @{}
foreach ($name in @(
    "PATH",
    "QTDIR",
    "QT_PLUGIN_PATH",
    "QML2_IMPORT_PATH",
    "QT_QPA_PLATFORM",
    "QT_QUICK_BACKEND")) {
  $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable(
      $name, [EnvironmentVariableTarget]::Process)
}

try {
  New-Item -ItemType Directory -Path $workPath | Out-Null
  Expand-Archive -LiteralPath $resolvedPackagePath -DestinationPath $workPath

  $roots = @(Get-ChildItem -LiteralPath $workPath -Directory)
  if ($roots.Count -ne 1) {
    throw "Alpha package must contain exactly one top-level directory."
  }
  $packageRoot = $roots[0].FullName
  $installerPath = Join-Path $packageRoot "install-alpha.cmd"
  if (!(Test-Path -LiteralPath $installerPath -PathType Leaf)) {
    throw "Alpha package does not contain install-alpha.cmd."
  }

  $platformPluginPath = Join-Path $packageRoot `
      "plugins\platforms\qwindows.dll"
  if (!(Test-Path -LiteralPath $platformPluginPath -PathType Leaf)) {
    throw "Alpha package does not contain qwindows.dll."
  }

  $hiddenPlatformPluginPath = "$platformPluginPath.acceptance-missing"
  Move-Item -LiteralPath $platformPluginPath `
      -Destination $hiddenPlatformPluginPath
  try {
    $negativeResult = Invoke-CommandScript -Path $installerPath `
        -Argument @($negativeInstallPath)
    if ($negativeResult.ExitCode -eq 0) {
      throw "Installer accepted a package with a missing platform plugin."
    }
    if (Test-Path -LiteralPath $negativeInstallPath) {
      throw "Rejected package changed the negative-test install directory."
    }
  } finally {
    if (Test-Path -LiteralPath $hiddenPlatformPluginPath -PathType Leaf) {
      Move-Item -LiteralPath $hiddenPlatformPluginPath `
          -Destination $platformPluginPath
    }
  }

  $installResult = Invoke-CommandScript -Path $installerPath `
      -Argument @($installPath)
  Write-CommandOutput -Result $installResult
  if ($installResult.ExitCode -ne 0) {
    throw "Alpha installer failed with exit code $($installResult.ExitCode)."
  }
  $primaryInstalled = $true

  $installedFiles = @(
    "bin\SystemAudioRoute.exe",
    "bin\Qt6Core.dll",
    "bin\Qt6Quick.dll",
    "bin\qt.conf",
    "bin\sar_virtual_asio_register.exe",
    "bin\SystemAudioRouteVirtualASIO.dll",
    "plugins\platforms\qwindows.dll",
    "qml\QtQuick\qtquick2plugin.dll",
    ".system-audio-route-alpha",
    "uninstall-alpha.cmd"
  )
  foreach ($relativePath in $installedFiles) {
    $path = Join-Path $installPath $relativePath
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "Installed Alpha payload is missing '$relativePath'."
    }
  }

  [Environment]::SetEnvironmentVariable(
      "QTDIR", $null, [EnvironmentVariableTarget]::Process)
  [Environment]::SetEnvironmentVariable(
      "QT_PLUGIN_PATH", $null, [EnvironmentVariableTarget]::Process)
  [Environment]::SetEnvironmentVariable(
      "QML2_IMPORT_PATH", $null, [EnvironmentVariableTarget]::Process)
  $cleanPath = @($env:PATH -split ';' | Where-Object {
      $_ -and $_ -notmatch '(?i)\\Qt\\|\\Qt6\\|\\Qt\\6\\'
    }) -join ';'
  [Environment]::SetEnvironmentVariable(
      "PATH", $cleanPath, [EnvironmentVariableTarget]::Process)
  [Environment]::SetEnvironmentVariable(
      "QT_QPA_PLATFORM", "offscreen", [EnvironmentVariableTarget]::Process)
  [Environment]::SetEnvironmentVariable(
      "QT_QUICK_BACKEND", "software", [EnvironmentVariableTarget]::Process)

  $guiPath = Join-Path $installPath "bin\SystemAudioRoute.exe"
  $guiProcess = Start-Process -FilePath $guiPath `
      -WorkingDirectory (Join-Path $installPath "bin") `
      -RedirectStandardOutput $guiStdoutPath `
      -RedirectStandardError $guiStderrPath `
      -PassThru
  Start-Sleep -Seconds $GuiHealthSeconds
  if ($guiProcess.HasExited) {
    $stderr = if (Test-Path -LiteralPath $guiStderrPath) {
      (Get-Content -LiteralPath $guiStderrPath -Raw).Trim()
    } else {
      ""
    }
    throw "Installed GUI exited early with code $($guiProcess.ExitCode): $stderr"
  }
  Stop-Process -Id $guiProcess.Id -Force
  Wait-Process -Id $guiProcess.Id -Timeout 5 -ErrorAction SilentlyContinue
  $guiProcess = $null

  $registerPath = Join-Path $installPath `
      "bin\sar_virtual_asio_register.exe"
  $driverPath = Join-Path $installPath `
      "bin\SystemAudioRouteVirtualASIO.dll"
  & $registerPath --verify $driverPath --user --x64
  if ($LASTEXITCODE -ne 0) {
    throw "Installed Virtual ASIO verification failed with exit code $LASTEXITCODE."
  }
  $registeredDriver = (Get-Item -LiteralPath $asioRegistrationKey).GetValue("")
  if (![IO.Path]::GetFullPath($registeredDriver).Equals(
          [IO.Path]::GetFullPath($driverPath),
          [StringComparison]::OrdinalIgnoreCase)) {
    throw "Virtual ASIO registration does not belong to the installed payload."
  }

  $updateResult = Invoke-CommandScript -Path $installerPath `
      -Argument @($installPath)
  Write-CommandOutput -Result $updateResult
  if ($updateResult.ExitCode -ne 0) {
    throw "Alpha in-place update failed with exit code $($updateResult.ExitCode)."
  }
  & $registerPath --verify $driverPath --user --x64
  if ($LASTEXITCODE -ne 0) {
    throw "Updated Virtual ASIO verification failed with exit code $LASTEXITCODE."
  }

  $ownershipInstallResult = Invoke-CommandScript -Path $installerPath `
      -Argument @($ownershipInstallPath)
  Write-CommandOutput -Result $ownershipInstallResult
  if ($ownershipInstallResult.ExitCode -ne 0) {
    throw "Alpha ownership install failed with exit code $($ownershipInstallResult.ExitCode)."
  }
  $ownershipInstalled = $true
  $ownershipDriverPath = Join-Path $ownershipInstallPath `
      "bin\SystemAudioRouteVirtualASIO.dll"
  $registeredDriver = (Get-Item -LiteralPath $asioRegistrationKey).GetValue("")
  if (![IO.Path]::GetFullPath($registeredDriver).Equals(
          [IO.Path]::GetFullPath($ownershipDriverPath),
          [StringComparison]::OrdinalIgnoreCase)) {
    throw "Second install did not take Virtual ASIO registration ownership."
  }

  $guiProcess = Start-Process `
      -FilePath (Join-Path $ownershipInstallPath "bin\SystemAudioRoute.exe") `
      -WorkingDirectory (Join-Path $ownershipInstallPath "bin") `
      -RedirectStandardOutput (Join-Path $workPath "ownership-gui.stdout.txt") `
      -RedirectStandardError (Join-Path $workPath "ownership-gui.stderr.txt") `
      -PassThru
  Start-Sleep -Seconds 1
  if ($guiProcess.HasExited) {
    throw "Second installed GUI exited before the path-boundary test."
  }

  $primaryUninstallResult = Invoke-CommandScript `
      -Path (Join-Path $installPath "uninstall-alpha.cmd")
  Write-CommandOutput -Result $primaryUninstallResult
  if ($primaryUninstallResult.ExitCode -ne 0) {
    throw "Primary uninstall failed while its same-prefix sibling was running."
  }
  if (![string]::Join("`n", $primaryUninstallResult.Output).Contains(
          "registration=not_owned")) {
    throw "Primary uninstall did not preserve the second install's registration."
  }
  $primaryInstalled = $false
  if ($guiProcess.HasExited) {
    throw "Primary uninstall disturbed the same-prefix sibling GUI."
  }
  if (Test-Path -LiteralPath $installPath) {
    throw "Primary uninstaller left the installation directory behind."
  }
  $registeredDriver = (Get-Item -LiteralPath $asioRegistrationKey).GetValue("")
  if (![IO.Path]::GetFullPath($registeredDriver).Equals(
          [IO.Path]::GetFullPath($ownershipDriverPath),
          [StringComparison]::OrdinalIgnoreCase)) {
    throw "Primary uninstall changed the second install's registration."
  }

  Stop-Process -Id $guiProcess.Id -Force
  Wait-Process -Id $guiProcess.Id -Timeout 5 -ErrorAction SilentlyContinue
  $guiProcess = $null

  $ownershipUninstallResult = Invoke-CommandScript `
      -Path (Join-Path $ownershipInstallPath "uninstall-alpha.cmd")
  Write-CommandOutput -Result $ownershipUninstallResult
  if ($ownershipUninstallResult.ExitCode -ne 0) {
    throw "Ownership uninstaller failed with exit code $($ownershipUninstallResult.ExitCode)."
  }
  $ownershipInstalled = $false
  Start-Sleep -Milliseconds 750
  if (Test-Path -LiteralPath $ownershipInstallPath) {
    throw "Ownership uninstaller left the installation directory behind."
  }
  if (Test-Path -LiteralPath $asioRegistrationKey) {
    throw "Ownership uninstaller left the Virtual ASIO registration behind."
  }

  Write-Output (
      "alpha_package_acceptance passed=1" +
      " package=`"$resolvedPackagePath`"" +
      " sha256=$packageHash" +
      " gui_health_seconds=$GuiHealthSeconds" +
      " missing_runtime_guard=passed" +
      " install=passed update=passed gui_launch=passed asio=passed" +
      " path_boundary=passed ownership=passed uninstall=passed")
} finally {
  if ($null -ne $guiProcess -and !$guiProcess.HasExited) {
    Stop-Process -Id $guiProcess.Id -Force -ErrorAction SilentlyContinue
  }
  foreach ($name in $savedEnvironment.Keys) {
    [Environment]::SetEnvironmentVariable(
        $name,
        $savedEnvironment[$name],
        [EnvironmentVariableTarget]::Process)
  }
  foreach ($cleanupInstall in @(
      @{ Active = $primaryInstalled; Path = $installPath },
      @{ Active = $ownershipInstalled; Path = $ownershipInstallPath })) {
    if ($cleanupInstall.Active -and
        (Test-Path -LiteralPath $cleanupInstall.Path)) {
      $markerPath = Join-Path $cleanupInstall.Path ".system-audio-route-alpha"
      $uninstallerPath = Join-Path $cleanupInstall.Path "uninstall-alpha.cmd"
      if ((Test-Path -LiteralPath $markerPath -PathType Leaf) -and
          (Test-Path -LiteralPath $uninstallerPath -PathType Leaf)) {
        Invoke-CommandScript -Path $uninstallerPath | Out-Null
      }
    }
  }
  if (Test-Path -LiteralPath $workPath) {
    Remove-Item -LiteralPath $workPath -Recurse -Force
  }
}
