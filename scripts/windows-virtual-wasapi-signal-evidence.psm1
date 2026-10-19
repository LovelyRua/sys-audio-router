Set-StrictMode -Version 2.0

function ConvertFrom-SarMatrixSignalProbeEvidence {
  [CmdletBinding()]
  param(
    [AllowNull()][AllowEmptyCollection()][AllowEmptyString()][string[]]$Lines,
    [int]$ExitCode,
    [bool]$RoutesEnabled
  )

  $integerNames = @('sent_frames', 'captured_frames', 'silent_frames',
    'capture_packets', 'discontinuity_packets', 'max_packet_frames', 'nonzero_samples')
  $powerNames = @('target_power', 'cross_channel_power', 'first_channel_power', 'second_channel_power')
  $names = $integerNames + $powerNames
  $result = [ordered]@{ passed = $false; reason = ''; exit_code = $ExitCode;
    routes_enabled = $RoutesEnabled }
  foreach ($name in $names) { $result[$name] = $null }

  # Check each line before tokenizing; never join an unbounded report.
  if ($null -eq $Lines -or $Lines.Count -eq 0) {
    $result.reason = 'missing_evidence'
    return [pscustomobject]$result
  }
  if ($Lines.Count -gt 4096) {
    $result.reason = 'evidence_line_limit'
    return [pscustomobject]$result
  }
  [long]$bytes = 0
  foreach ($line in $Lines) {
    if ($null -ne $line) {
      if ($line.Length -gt 1048576) {
        $result.reason = 'evidence_byte_limit'
        return [pscustomobject]$result
      }
      $bytes += [Text.Encoding]::UTF8.GetByteCount($line)
    }
    $bytes += 1 # Include a separator even for empty lines.
    if ($bytes -gt 1048576) {
      $result.reason = 'evidence_byte_limit'
      return [pscustomobject]$result
    }
  }

  $values = @{}
  $fieldPattern = '^(?<name>' + ($names -join '|') + ')(?=$|[^A-Za-z0-9_])(?<suffix>.*)$'
  foreach ($line in $Lines) {
    if ($null -eq $line) { continue }
    foreach ($token in [regex]::Split($line, '\s+')) {
      $field = [regex]::Match($token, $fieldPattern)
      if (!$field.Success) { continue }
      $name = $field.Groups['name'].Value
      if ($values.ContainsKey($name)) {
        $result.reason = "duplicate_field:$name"
        return [pscustomobject]$result
      }
      $suffix = $field.Groups['suffix'].Value
      if (!$suffix.StartsWith('=') -or $suffix.Length -eq 1) {
        $result.reason = "invalid_field:$name"
        return [pscustomobject]$result
      }
      $values[$name] = $suffix.Substring(1)
    }
  }
  $culture = [Globalization.CultureInfo]::InvariantCulture
  foreach ($name in $names) {
    if (!$values.ContainsKey($name)) {
      $result.reason = "missing_field:$name"
      return [pscustomobject]$result
    }
    $value = $values[$name]
    if ($integerNames -contains $name) {
      [uint64]$number = 0
      if ($value -cnotmatch '^[0-9]+$' -or
          ![uint64]::TryParse($value, [Globalization.NumberStyles]::None,
            $culture, [ref]$number)) {
        $result.reason = "invalid_field:$name"
        return [pscustomobject]$result
      }
      $result[$name] = $number
    } else {
      [double]$power = 0
      if ($value -cnotmatch '^(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$' -or
          ![double]::TryParse($value, [Globalization.NumberStyles]::Float,
            $culture, [ref]$power) -or [double]::IsNaN($power) -or
          [double]::IsInfinity($power) -or $power -lt 0) {
        $result.reason = "invalid_field:$name"
        return [pscustomobject]$result
      }
      # Do not let floating-point underflow turn nonzero content into a silent control.
      if ($power -eq 0 -and ($value -split '[eE]', 2)[0] -match '[1-9]') {
        $result.reason = "invalid_field:$name"
        return [pscustomobject]$result
      }
      $result[$name] = $power
    }
  }

  if ($result.sent_frames -eq 0) { $result.reason = 'no_sent_frames' }
  elseif ($result.sent_frames -lt 48000) { $result.reason = 'insufficient_render' }
  elseif ($result.captured_frames -lt 48000) { $result.reason = 'insufficient_capture' }
  elseif ($result.capture_packets -eq 0) { $result.reason = 'no_capture_packets' }
  elseif ($result.max_packet_frames -eq 0) { $result.reason = 'no_packet_frames' }
  elseif ($result.silent_frames -gt $result.captured_frames -or
      $result.max_packet_frames -gt $result.captured_frames -or
      $result.capture_packets -gt $result.captured_frames -or
      ([bigint]$result.capture_packets * [bigint]$result.max_packet_frames) -lt
        [bigint]$result.captured_frames -or
      $result.nonzero_samples -gt
        (([decimal]$result.captured_frames - [decimal]$result.silent_frames) * 2)) {
    $result.reason = 'invalid_capture_accounting'
  }
  elseif ($RoutesEnabled) {
    if ($ExitCode -ne 0) { $result.reason = 'enabled_exit_code' }
    elseif ($result.nonzero_samples -eq 0 -or $result.target_power -lt 1e12 -or $result.first_channel_power -lt 1e12 -or
        $result.second_channel_power -lt 1e12) {
      $result.reason = 'insufficient_signal_power'
    } elseif ($result.cross_channel_power -gt
        ($result.first_channel_power / 100.0 + $result.second_channel_power / 100.0)) {
      $result.reason = 'crosstalk'
    }
  } else {
    if ($ExitCode -ne 3) { $result.reason = 'disabled_exit_code' }
    elseif ($result.nonzero_samples -ne 0 -or $result.target_power -ne 0 -or $result.cross_channel_power -ne 0 -or
        $result.first_channel_power -ne 0 -or
        $result.second_channel_power -ne 0) { $result.reason = 'control_not_silent' }
  }
  $result.passed = ($result.reason -eq '')
  return [pscustomobject]$result
}

Export-ModuleMember -Function ConvertFrom-SarMatrixSignalProbeEvidence
