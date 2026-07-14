# Windows WinRM Slot Retention

The local archive test and measurement entry points can optionally remove
finished remote slots. Cleanup is disabled by default, so existing invocations
retain their current behavior.

Every normally unwound remote run writes `.sar-slot-finished.json` with a
`success` or `failure` outcome. Failed builds and measurements therefore become
retention candidates instead of accumulating forever. Abruptly terminated runs
retain their active token and use the stale-token policy below.

Use `-CleanupDryRun` first to print the slots that match the default policy
without deleting them:

```powershell
scripts\windows-winrm-local-test.ps1 -HostName 192.168.123.123 `
  -UserName codex -Password <password> -Slot engineer-a -CleanupDryRun
```

Enable deletion explicitly and tune the bounded policy when needed:

```powershell
scripts\windows-winrm-local-measure.ps1 -HostName 192.168.123.123 `
  -UserName codex -Password <password> -Slot engineer-a `
  -CleanupCompletedSlots -RetentionDays 14 -RetentionCount 8 -CleanupLimit 2
```

`RetentionDays` selects finished slots older than the cutoff.
`RetentionCount` selects finished slots beyond the newest retained count.
`CleanupLimit` caps removals per invocation. A slot matching either age or
count is eligible, but no more than the limit are removed, oldest first.

The `.cmd` wrappers expose the same settings through environment variables:

```bat
set SAR_SLOT_CLEANUP_DRY_RUN=true
set SAR_SLOT_RETENTION_DAYS=14
set SAR_SLOT_RETENTION_COUNT=8
set SAR_SLOT_CLEANUP_LIMIT=2
set SAR_SLOT_STALE_ACTIVE_HOURS=24
scripts\windows-winrm-local-test.cmd 192.168.123.123 codex password engineer-a
```

Set `SAR_SLOT_CLEANUP=true` to enable deletion. Dry-run alone enables selection
and reporting but never removal. Numeric values are validated by PowerShell;
the stale-active threshold cannot be less than 24 hours.

Only direct children of `%USERPROFILE%\src` whose complete names match the
`sys-audio-router-<slot>` pattern and contain `.sar-slot-finished.json` are
considered. Reparse points, the current slot, unmarked directories, and slots
with a fresh `.sar-slot-active\<slot>\*.active` token are excluded. Stale tokens
remain protective until they exceed `StaleActiveHours` and a successful
`Win32_Process.CommandLine` inspection finds no process containing that exact,
validated repository path. Failed process inspection retains the slot. This
policy defaults to 24 hours and can only be made more conservative.
Active registration and cleanup share `.sar-slot-retention.lock`, preventing a
new run from becoming active between selection and removal.

Active and finished markers contain timestamps, process/run identifiers,
validated paths, and outcomes only. Credentials remain in memory for session
creation and are not written to markers or retention output. Marker, retention,
and temporary-file cleanup errors are warning-only and cannot replace an
original build or measurement failure.

Run the focused local selection and dry-run tests with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\windows-winrm-slot-retention.tests.ps1
```
