$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
Import-Module (Join-Path $PSScriptRoot 'windows-virtual-wasapi-signal-evidence.psm1') -Force
$script:checks = 0

function Assert-True([bool]$Condition, [string]$Message) {
  if (!$Condition) { throw $Message }
  $script:checks++
}

# Same three-line layout and scientific notation as wasapi_bridge_probe --route.
$valid = @(
  'sent_frames=144000 captured_frames=143520 silent_frames=480 capture_packets=299 discontinuity_packets=2 max_packet_frames=480',
  'target_power=3.456e+13 cross_channel_power=1.234e+07',
  'second_channel_power=1.728e+13'
)
$fields = @('sent_frames', 'captured_frames', 'silent_frames', 'capture_packets',
  'discontinuity_packets', 'max_packet_frames', 'target_power',
  'cross_channel_power', 'second_channel_power')

function Set-Field([string]$Name, [string]$Value) {
  return @($valid | ForEach-Object {
    [regex]::Replace($_, ('\b' + $Name + '=\S+'), ($Name + '=' + $Value))
  })
}

function Assert-Rejected([string[]]$Evidence, [string]$Reason,
    [int]$Code = 0, [bool]$Enabled = $true) {
  $result = ConvertFrom-SarMatrixSignalProbeEvidence -Lines $Evidence `
    -ExitCode $Code -RoutesEnabled $Enabled
  Assert-True (!$result.passed) "Unexpected success: $Reason"
  Assert-True ($result.reason -eq $Reason) "Expected $Reason, got $($result.reason)"
  Assert-True ($result.exit_code -eq $Code -and $result.routes_enabled -eq $Enabled) `
    'Invocation context was not preserved.'
}

$result = ConvertFrom-SarMatrixSignalProbeEvidence $valid 0 $true
Assert-True $result.passed 'Actual-format enabled evidence should pass.'
Assert-True ($result.reason -eq '') 'Successful evidence has a failure reason.'
Assert-True ($result.discontinuity_packets -eq 2) 'Discontinuities must remain diagnostic.'
Assert-True ($result.sent_frames -is [uint64]) 'Counters must retain uint64 precision.'
Assert-True ($result.target_power -is [double]) 'Power must be parsed as double.'
Assert-True ($result.target_power -eq 3.456e13) 'Scientific notation was misparsed.'

$silent = @($valid[0], 'target_power=0 cross_channel_power=0', 'second_channel_power=0')
$result = ConvertFrom-SarMatrixSignalProbeEvidence $silent 3 $false
Assert-True $result.passed 'Route-free captured silence should pass.'
Assert-True ($result.captured_frames -eq 143520) 'Silent capture frames were lost.'
Assert-Rejected $silent 'enabled_exit_code' 3
Assert-Rejected $silent 'disabled_exit_code' 0 $false
foreach ($power in @('target_power', 'cross_channel_power', 'second_channel_power')) {
  $leak = @($silent | ForEach-Object {
    [regex]::Replace($_, ('\b' + $power + '=0'), ($power + '=1e-20'))
  })
  Assert-Rejected $leak 'control_not_silent' 3 $false
}
Assert-Rejected $null 'missing_evidence'
Assert-Rejected @() 'missing_evidence'
foreach ($name in $fields) {
  $missing = @($valid | ForEach-Object {
    [regex]::Replace($_, ('\b' + $name + '=\S+'), '')
  })
  Assert-Rejected $missing "missing_field:$name"
  Assert-Rejected ($valid + "$name=1") "duplicate_field:$name"
  Assert-Rejected (Set-Field $name 'garbage') "invalid_field:$name"
  Assert-Rejected (Set-Field $name '-1') "invalid_field:$name"
  Assert-Rejected (Set-Field $name '') "invalid_field:$name"
}
foreach ($name in $fields[0..5]) {
  foreach ($value in @('18446744073709551616', '1.0', '1e3', '+1', '1,000', '0x10')) {
    Assert-Rejected (Set-Field $name $value) "invalid_field:$name"
  }
}
foreach ($name in $fields[6..8]) {
  foreach ($value in @('NaN', 'Infinity', 'inf', '-Infinity', '1e999', '1e-999', '-0', '1,234', '1.2junk')) {
    Assert-Rejected (Set-Field $name $value) "invalid_field:$name"
  }
}
Assert-Rejected ($valid + 'target_power:123') 'duplicate_field:target_power'
Assert-Rejected @('sent_frames = 144000') 'invalid_field:sent_frames'
Assert-Rejected (Set-Field 'sent_frames' '0') 'no_sent_frames'
Assert-Rejected (Set-Field 'captured_frames' '0') 'insufficient_capture'
Assert-Rejected (Set-Field 'captured_frames' '47999') 'insufficient_capture'
Assert-Rejected (Set-Field 'capture_packets' '0') 'no_capture_packets'
Assert-Rejected (Set-Field 'max_packet_frames' '0') 'no_packet_frames'
Assert-Rejected (Set-Field 'captured_frames' '0') 'insufficient_capture' 3 $false
Assert-Rejected $valid 'enabled_exit_code' 4
Assert-Rejected (Set-Field 'target_power' '9.999e11') 'insufficient_signal_power'
Assert-Rejected (Set-Field 'second_channel_power' '9.999e11') 'insufficient_signal_power'
Assert-Rejected (Set-Field 'cross_channel_power' '3.457e11') 'crosstalk'
Assert-Rejected (Set-Field 'cross_channel_power' '1e308') 'crosstalk'

$boundary = @('sent_frames=1 captured_frames=48000 silent_frames=0 capture_packets=1 discontinuity_packets=0 max_packet_frames=1',
  'target_power=1e12 cross_channel_power=1e10 second_channel_power=1e12')
Assert-True (ConvertFrom-SarMatrixSignalProbeEvidence $boundary 0 $true).passed `
  'Inclusive frame, power and crosstalk boundaries must pass.'
$largest = Set-Field 'sent_frames' '18446744073709551615'
Assert-True ((ConvertFrom-SarMatrixSignalProbeEvidence $largest 0 $true).sent_frames -eq [uint64]::MaxValue) `
  'uint64 maximum must parse exactly.'
Assert-Rejected (@('' ) * 4097) 'evidence_line_limit'
Assert-Rejected @('x' * 1048576) 'evidence_byte_limit'
Assert-Rejected @(([string][char]0x4E2D) * 400000) 'evidence_byte_limit'
Assert-Rejected @(('x' * 600000), ('x' * 600000)) 'evidence_byte_limit'
$lineLimit = $valid + (@('') * 4093)
Assert-True (ConvertFrom-SarMatrixSignalProbeEvidence $lineLimit 0 $true).passed `
  'Exactly 4096 lines should be accepted.'
$used = 0
foreach ($line in $valid) { $used += [Text.Encoding]::UTF8.GetByteCount($line) + 1 }
$byteLimit = $valid + ('x' * (1048576 - $used - 1))
Assert-True (ConvertFrom-SarMatrixSignalProbeEvidence $byteLimit 0 $true).passed `
  'Exactly 1 MiB should be accepted.'

$thread = [Threading.Thread]::CurrentThread
$previousCulture = $thread.CurrentCulture
try {
  $thread.CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo('de-DE')
  Assert-True (ConvertFrom-SarMatrixSignalProbeEvidence $valid 0 $true).passed `
    'Parsing must be invariant under comma-decimal culture.'
  Assert-Rejected (Set-Field 'target_power' '3,456e13') 'invalid_field:target_power'
} finally { $thread.CurrentCulture = $previousCulture }
Write-Output "Matrix signal evidence tests passed ($script:checks assertions)."
