function Get-WasapiSoakModeNames {
  param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("render", "duplex", "loopback", "both", "all")]
    [string]$Mode
  )

  switch ($Mode) {
    "render" { return @("render") }
    "duplex" { return @("duplex") }
    "loopback" { return @("loopback") }
    "both" { return @("render", "duplex") }
    "all" { return @("render", "duplex", "loopback") }
  }
}

function Invoke-WasapiSoak {
  param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("render", "duplex", "loopback", "both", "all")]
    [string]$Mode,
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [uint32]::MaxValue)]
    [uint32]$Iterations,
    [Parameter(Mandatory = $true)]
    [scriptblock]$RunMeasurement
  )

  $modeNames = @(Get-WasapiSoakModeNames -Mode $Mode)
  $failures = @{}
  foreach ($modeName in $modeNames) {
    $failures[$modeName] = [uint64]0
  }

  for ([uint64]$iteration = 1; $iteration -le $Iterations; ++$iteration) {
    Write-Output "[soak] iteration $iteration/$Iterations"
    foreach ($modeName in $modeNames) {
      $exitCode = & $RunMeasurement $modeName $iteration
      if ($null -eq $exitCode) {
        $exitCode = 0
      }
      if ([int]$exitCode -ne 0) {
        $failures[$modeName] = [uint64]($failures[$modeName] + 1)
      }
    }
  }

  [uint64]$totalFailures = 0
  foreach ($modeName in $modeNames) {
    $modeFailures = $failures[$modeName]
    $totalFailures += $modeFailures
    Write-Output "[soak] mode=$modeName iterations=$Iterations failures=$modeFailures"
  }
  Write-Output "[soak] completed modes=$($modeNames.Count) iterations=$Iterations attempts=$($modeNames.Count * $Iterations) failures=$totalFailures"

  [pscustomobject]@{
    ModeNames = $modeNames
    Iterations = $Iterations
    Attempts = [uint64]$modeNames.Count * [uint64]$Iterations
    FailuresByMode = $failures
    FailureCount = $totalFailures
  }
}

Export-ModuleMember -Function Get-WasapiSoakModeNames, Invoke-WasapiSoak
