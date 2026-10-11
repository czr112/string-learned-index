$ErrorActionPreference = 'Stop'
$root = 'D:\StringLearnedIndex_Workspace'
function Read-Manifest($path) {
    $map = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($path)) { $map[$line.Substring(66)] = $line.Substring(0,64) }
    return $map
}
$old = Read-Manifest (Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011j.sha256')
$new = Read-Manifest (Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011k.sha256')
$added = @($new.Keys | Where-Object { -not $old.ContainsKey($_) } | Sort-Object)
$removed = @($old.Keys | Where-Object { -not $new.ContainsKey($_) } | Sort-Object)
$changed = @($new.Keys | Where-Object { $old.ContainsKey($_) -and $old[$_] -ne $new[$_] } | Sort-Object)
Write-Output ('j={0} k={1} added={2} removed={3} changed={4}' -f $old.Count,$new.Count,$added.Count,$removed.Count,$changed.Count)
foreach ($p in $added) { Write-Output ('  + ' + $p) }
foreach ($p in $changed) { Write-Output ('  ~ ' + $p) }
if ($removed.Count) { throw 'Unexpected removed path' }
if ($changed.Count -ne 1 -or $changed[0] -ne '.\07_Eval_Integration\STRUCTURE_OPTIMIZATION_STATE.md') {
    throw ('Unexpected changed paths: ' + ($changed -join ', '))
}
$allowedAdded = @(
    '.\04_Consolidation_and_Verification\diff_manifest_20261011jk.ps1',
    '.\04_Consolidation_and_Verification\make_workspace_manifest_20261011k.ps1',
    '.\04_Consolidation_and_Verification\MANIFEST_workspace_20261011j.sha256',
    '.\07_Eval_Integration\diagnostics\owner_entry.cpp',
    '.\07_Eval_Integration\diagnostics\run_owner_entry.py',
    '.\07_Eval_Integration\diagnostics\verify_owner_entry.py',
    '.\07_Eval_Integration\RESULTS_OWNER_ENTRY_20261011.md',
    '.\07_Eval_Integration\results\owner_entry_20261011\commands.log',
    '.\07_Eval_Integration\results\owner_entry_20261011\identity.json',
    '.\07_Eval_Integration\results\owner_entry_20261011\owner_entry',
    '.\07_Eval_Integration\results\owner_entry_20261011\summary.json',
    '.\07_Eval_Integration\results\owner_entry_20261011\verify.json',
    '.\07_Eval_Integration\results\owner_entry_20261011\work.json',
    '.\docs\NEXT_ACTION_PLAN_20261011_OWNER.md'
)
if ($added.Count -ne $allowedAdded.Count) { throw ('Unexpected addition count: ' + $added.Count) }
foreach ($p in $added) { if ($p -notin $allowedAdded) { throw ('Unexpected addition: ' + $p) } }
Write-Output 'M3 entry round changes only; index, adapters and existing experimental evidence unchanged.'