function Get-WasapiLifecycleModeNames {
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

function Invoke-WasapiLifecycleSoak {
  param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("render", "duplex", "loopback", "both", "all")]
    [string]$Mode,
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [uint32]::MaxValue)]
    [uint32]$Iterations,
    [Parameter(Mandatory = $true)]
    [scriptblock]$RunAttempt
  )

  $modeNames = @(Get-WasapiLifecycleModeNames -Mode $Mode)
  $byMode = @{}
  foreach ($modeName in $modeNames) {
    $byMode[$modeName] = [ordered]@{
      Attempts = [uint64]0
      StartupFailures = [uint64]0
      RuntimeFailures = [uint64]0
      StopTimeouts = [uint64]0
    }
  }

  for ([uint64]$iteration = 1; $iteration -le $Iterations; ++$iteration) {
    Write-Output "[lifecycle-soak] iteration $iteration/$Iterations"
    foreach ($modeName in $modeNames) {
      $attempt = & $RunAttempt $modeName $iteration
      if ($null -eq $attempt -or
          $attempt.Outcome -notin @("success", "startup_failure", "runtime_failure", "stop_timeout")) {
        throw "Runner returned an invalid lifecycle outcome for mode '$modeName', iteration $iteration."
      }
      ++$byMode[$modeName].Attempts
      switch ($attempt.Outcome) {
        "startup_failure" { ++$byMode[$modeName].StartupFailures }
        "runtime_failure" { ++$byMode[$modeName].RuntimeFailures }
        "stop_timeout" { ++$byMode[$modeName].StopTimeouts }
      }
      Write-Output "[lifecycle-soak] mode=$modeName iteration=$iteration outcome=$($attempt.Outcome) exit_code=$($attempt.ExitCode)"
    }
  }

  [uint64]$startupFailures = 0
  [uint64]$runtimeFailures = 0
  [uint64]$stopTimeouts = 0
  foreach ($modeName in $modeNames) {
    $stats = $byMode[$modeName]
    $startupFailures += $stats.StartupFailures
    $runtimeFailures += $stats.RuntimeFailures
    $stopTimeouts += $stats.StopTimeouts
    Write-Output "[lifecycle-soak] mode=$modeName attempts=$($stats.Attempts) startup_failures=$($stats.StartupFailures) runtime_failures=$($stats.RuntimeFailures) stop_timeouts=$($stats.StopTimeouts)"
  }
  $failureCount = $startupFailures + $runtimeFailures + $stopTimeouts
  Write-Output "[lifecycle-soak] completed attempts=$($modeNames.Count * $Iterations) startup_failures=$startupFailures runtime_failures=$runtimeFailures stop_timeouts=$stopTimeouts failures=$failureCount"

  [pscustomobject]@{
    ModeNames = $modeNames
    Iterations = $Iterations
    Attempts = [uint64]$modeNames.Count * [uint64]$Iterations
    ByMode = $byMode
    StartupFailureCount = $startupFailures
    RuntimeFailureCount = $runtimeFailures
    StopTimeoutCount = $stopTimeouts
    FailureCount = $failureCount
  }
}

Export-ModuleMember -Function Get-WasapiLifecycleModeNames, Invoke-WasapiLifecycleSoak
