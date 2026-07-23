param(
  [string]$SamplesRoot = "$env:USERPROFILE\src\windows-driver-samples-acx",
  [ValidateSet("Probe", "Acx", "Sysvad", "All")]
  [string]$Target = "All",
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug",
  [ValidateSet("x64", "Win32", "ARM64")]
  [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Require-File {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Description
  )

  if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "$Description was not found at '$Path'."
  }
  return (Get-Item -LiteralPath $Path).FullName
}

function Get-LatestWdkVersion {
  param([Parameter(Mandatory = $true)][string]$KitsRoot)

  $includeRoot = Join-Path $KitsRoot "Include"
  if (!(Test-Path -LiteralPath $includeRoot -PathType Container)) {
    throw "The WDK include root was not found at '$includeRoot'."
  }

  $versions = @(Get-ChildItem -LiteralPath $includeRoot -Directory | Where-Object {
    Test-Path -LiteralPath (Join-Path $_.FullName "km\wdm.h") -PathType Leaf
  } | Sort-Object { [version]$_.Name } -Descending)
  if ($versions.Count -eq 0) {
    throw "No WDK version containing km\wdm.h was found under '$includeRoot'."
  }
  return $versions[0].Name
}

function Invoke-SampleBuild {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Solution,
    [Parameter(Mandatory = $true)][string]$MsBuild
  )

  Require-File -Path $Solution -Description "$Name solution" | Out-Null
  Write-Host ""
  Write-Host "=== Building $Name ==="
  Write-Host "Solution: $Solution"
  $buildOutput = @(& $MsBuild $Solution /m `
      "/p:Configuration=$Configuration" "/p:Platform=$Platform" /v:minimal 2>&1)
  $exitCode = $LASTEXITCODE
  $buildOutput | ForEach-Object { Write-Host $_ }
  $errorText = ($buildOutput | Out-String)
  $loggedErrors = [regex]::Matches(
      $errorText, '(?im):\s*error(?:\s+[A-Z]+\d+)?\s*:').Count
  if ($exitCode -ne 0 -or $loggedErrors -ne 0) {
    throw "$Name build failed with exit code $exitCode and $loggedErrors logged error(s)."
  }
}

$kitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10"
$wdkVersion = Get-LatestWdkVersion -KitsRoot $kitsRoot
$wdkBuildRoot = Join-Path $kitsRoot "build\$wdkVersion"
$driverTargets = Join-Path $wdkBuildRoot "WindowsDriver.Common.targets"
$acxHeader = Join-Path $kitsRoot "Include\$wdkVersion\km\acx\km\1.0\acx.h"
Require-File -Path $driverTargets -Description "WDK driver targets" | Out-Null
Require-File -Path $acxHeader -Description "ACX kernel header" | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} `
    "Microsoft Visual Studio\Installer\vswhere.exe"
Require-File -Path $vswhere -Description "Visual Studio locator" | Out-Null
$vsInstall = @(& $vswhere -latest -products * `
    -requires Component.Microsoft.Windows.DriverKit `
    -property installationPath) | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($vsInstall)) {
  throw "Visual Studio DriverKit integration is missing. Add Component.Microsoft.Windows.DriverKit to the Visual Studio 2022 Build Tools instance."
}

$msbuild = Join-Path $vsInstall "MSBuild\Current\Bin\amd64\MSBuild.exe"
Require-File -Path $msbuild -Description "64-bit MSBuild" | Out-Null
$samplesRootFull = [IO.Path]::GetFullPath($SamplesRoot)

Write-Host "Windows driver spike toolchain"
Write-Host "Visual Studio: $vsInstall"
Write-Host "MSBuild:       $msbuild"
Write-Host "WDK:           $wdkVersion"
Write-Host "Samples:       $samplesRootFull"
Write-Host "Target:        $Target ($Configuration|$Platform)"

if ($Target -eq "Probe") {
  exit 0
}

if (!(Test-Path -LiteralPath $samplesRootFull -PathType Container)) {
  throw "Windows-driver-samples checkout was not found at '$samplesRootFull'."
}

if ($Target -in @("Acx", "All")) {
  $acxSolution = Join-Path $samplesRootFull `
      "audio\Acx\Samples\AudioCodec\Driver\AudioCodec.sln"
  Invoke-SampleBuild -Name "ACX AudioCodec" -Solution $acxSolution `
      -MsBuild $msbuild
}

if ($Target -in @("Sysvad", "All")) {
  $sysvadSolution = Join-Path $samplesRootFull "audio\sysvad\sysvad.sln"
  if (!(Test-Path -LiteralPath $sysvadSolution -PathType Leaf)) {
    throw "SysVAD is absent from the sparse checkout. Run: git -C `"$samplesRootFull`" sparse-checkout add --skip-checks audio/sysvad wil"
  }
  $wilHeader = Join-Path $samplesRootFull "wil\include\wil\resource.h"
  if (!(Test-Path -LiteralPath $wilHeader -PathType Leaf)) {
    throw "The WIL submodule is missing. Run: git -C `"$samplesRootFull`" submodule update --init wil"
  }
  Invoke-SampleBuild -Name "SysVAD" -Solution $sysvadSolution `
      -MsBuild $msbuild
}

Write-Host ""
Write-Host "Requested Microsoft sample builds completed successfully."
