param(
  [string]$InstallDirectory = ""
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

if ([string]::IsNullOrWhiteSpace($InstallDirectory)) {
  $InstallDirectory = $PSScriptRoot
  if (!(Test-Path -LiteralPath (Join-Path $InstallDirectory "bin") `
      -PathType Container)) {
    $InstallDirectory =
        Join-Path $env:LOCALAPPDATA "Programs\SystemAudioRoute"
  }
}
$installPath = [IO.Path]::GetFullPath($InstallDirectory.Trim().Trim('"'))
if (!(Test-Path -LiteralPath $installPath -PathType Container)) {
  Write-Host "alpha_uninstall status=not_installed directory=`"$installPath`""
  exit 0
}
$markerPath = Join-Path $installPath ".system-audio-route-alpha"
if (!(Test-Path -LiteralPath $markerPath -PathType Leaf)) {
  throw "Refusing to remove a directory without the System Audio Route Alpha marker."
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
    throw "Close installed process '$processName' before uninstalling."
  }
}

$driverPath = Join-Path $installPath "bin\SystemAudioRouteVirtualASIO.dll"
$inprocKey = "HKCU:\Software\Classes\CLSID\" +
    "{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}\InprocServer32"
$registeredDriverPath = $null
if (Test-Path -LiteralPath $inprocKey) {
  $candidate = (Get-Item -LiteralPath $inprocKey).GetValue("")
  if ($candidate -is [string] -and $candidate.Length -ne 0) {
    $registeredDriverPath = [IO.Path]::GetFullPath($candidate)
  }
}
$ownsRegistration = $null -ne $registeredDriverPath -and
    $registeredDriverPath.Equals(
        [IO.Path]::GetFullPath($driverPath),
        [StringComparison]::OrdinalIgnoreCase)

$suffix = [guid]::NewGuid().ToString("N").Substring(0, 8)
$removalPath = "$installPath.removing-$suffix"
$movedInstall = $false
$removedRegistration = $false
try {
  Move-Item -LiteralPath $installPath -Destination $removalPath
  $movedInstall = $true

  if ($ownsRegistration) {
    $registerPath =
        Join-Path $removalPath "bin\sar_virtual_asio_register.exe"
    & $registerPath --unregister --user --x64
    if ($LASTEXITCODE -ne 0) {
      throw "Virtual ASIO unregistration failed with exit code $LASTEXITCODE."
    }
    $removedRegistration = $true
  }

  Remove-Item -LiteralPath $removalPath -Recurse -Force
  $movedInstall = $false
} catch {
  $primaryError = $_
  $rollbackErrors = [System.Collections.Generic.List[string]]::new()
  if ($movedInstall -and (Test-Path -LiteralPath $removalPath) -and
      !(Test-Path -LiteralPath $installPath)) {
    try {
      Move-Item -LiteralPath $removalPath -Destination $installPath
    } catch {
      $rollbackErrors.Add("restore_install error=$($_.Exception.Message)")
    }
  }
  if ($removedRegistration -and
      (Test-Path -LiteralPath $installPath -PathType Container)) {
    $restoreRegister =
        Join-Path $installPath "bin\sar_virtual_asio_register.exe"
    $restoreDriver =
        Join-Path $installPath "bin\SystemAudioRouteVirtualASIO.dll"
    if ((Test-Path -LiteralPath $restoreRegister -PathType Leaf) -and
        (Test-Path -LiteralPath $restoreDriver -PathType Leaf)) {
      & $restoreRegister --register $restoreDriver --user --x64 2>$null
      if ($LASTEXITCODE -ne 0) {
        $rollbackErrors.Add("restore_asio exit_code=$LASTEXITCODE")
      }
    } else {
      $rollbackErrors.Add("restore_asio missing_payload")
    }
  }
  if ($rollbackErrors.Count -ne 0) {
    $steps = [string]::Join(",", $rollbackErrors)
    Write-Host "alpha_uninstall rollback=failed steps=$steps"
    throw ("$($primaryError.Exception.Message) Rollback failures: $steps")
  }
  throw $primaryError
}

$registrationStatus = if ($ownsRegistration) { "removed" } else { "not_owned" }
Write-Host ("alpha_uninstall status=removed directory=`"$installPath`" " +
    "registration=$registrationStatus")
