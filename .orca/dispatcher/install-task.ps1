<#
.SYNOPSIS
  Install (or remove) the dispatcher as a Windows scheduled task for the current user.

.DESCRIPTION
  Registers "OrcaDispatcher-<repo folder>" to start `pythonw dispatch.py run` at logon,
  restart it if it dies, and never time it out. The dispatcher is then independent of any
  terminal or chat session. Logs go to .orca\dispatcher\dispatcher.log.

  NOTE (v0.2): the dispatcher spawns `claude` itself. Scheduled tasks often run with a
  minimal PATH; if `dispatch.py doctor` passes in your terminal but headless runs fail
  from the task, set `dispatcher.claude_cmd` in .orca/dispatch.yml to the ABSOLUTE path
  that `where claude` prints.

  Run from anywhere:   powershell -ExecutionPolicy Bypass -File .orca\dispatcher\install-task.ps1
  Remove:              ... install-task.ps1 -Uninstall
  Show:                ... install-task.ps1 -Status
#>
[CmdletBinding()]
param(
  [switch]$Uninstall,
  [switch]$Status
)

$ErrorActionPreference = 'Stop'
$here     = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $here '..\..')).Path
$repoName = Split-Path -Leaf $repoRoot
$taskName = "OrcaDispatcher-$repoName"
$script   = Join-Path $here 'dispatch.py'

if ($Status) {
  $t = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
  if (-not $t) { Write-Host "not installed: $taskName"; exit 0 }
  $i = Get-ScheduledTaskInfo -TaskName $taskName
  Write-Host ("{0}: state={1} lastRun={2} lastResult={3}" -f $taskName, $t.State, $i.LastRunTime, $i.LastTaskResult)
  exit 0
}

if ($Uninstall) {
  $t = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
  if ($t) {
    Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    Write-Host "removed $taskName"
  } else { Write-Host "not installed: $taskName" }
  exit 0
}

# pythonw: no console window every minute, no stdout; the script logs to a file.
$pythonw = $null
$cmd = Get-Command pythonw.exe -ErrorAction SilentlyContinue
if ($cmd) { $pythonw = $cmd.Source }
if (-not $pythonw) {
  $py = (Get-Command python.exe -ErrorAction SilentlyContinue)
  if ($py) { $candidate = Join-Path (Split-Path -Parent $py.Source) 'pythonw.exe'; if (Test-Path $candidate) { $pythonw = $candidate } }
}
if (-not $pythonw) { throw "pythonw.exe not found on PATH. Install Python 3.9+ for this user." }

$action    = New-ScheduledTaskAction -Execute $pythonw -Argument "`"$script`" run" -WorkingDirectory $repoRoot
$trigger   = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$settings  = New-ScheduledTaskSettingsSet -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
               -ExecutionTimeLimit ([TimeSpan]::Zero) -StartWhenAvailable -MultipleInstances IgnoreNew `
               -DontStopIfGoingOnBatteries -AllowStartIfOnBatteries
$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Limited

$existing = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($existing) { Unregister-ScheduledTask -TaskName $taskName -Confirm:$false }
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -Principal $principal | Out-Null
Start-ScheduledTask -TaskName $taskName
Start-Sleep -Seconds 3
$info = Get-ScheduledTaskInfo -TaskName $taskName
Write-Host "installed and started $taskName (python: $pythonw)"
Write-Host "state: $((Get-ScheduledTask -TaskName $taskName).State); log: $here\dispatcher.log"
Write-Host "check:  python .orca\dispatcher\dispatch.py status"
