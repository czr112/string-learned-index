$ErrorActionPreference = 'Stop'
$root = 'D:\StringLearnedIndex_Workspace'
function Read-Manifest($path) {
    $map = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($path)) { $map[$line.Substring(66)] = $line.Substring(0,64) }
    return $map
}
$old = Read-Manifest (Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011e.sha256')
$new = Read-Manifest (Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011f.sha256')
$added = @($new.Keys | Where-Object { -not $old.ContainsKey($_) } | Sort-Object)
$removed = @($old.Keys | Where-Object { -not $new.ContainsKey($_) } | Sort-Object)
$changed = @($new.Keys | Where-Object { $old.ContainsKey($_) -and $old[$_] -ne $new[$_] } | Sort-Object)
Write-Output ('e={0} f={1} added={2} removed={3} changed={4}' -f $old.Count,$new.Count,$added.Count,$removed.Count,$changed.Count)
if ($removed.Count) { throw 'Unexpected removed path' }
if ($changed.Count -ne 1 -or $changed[0] -ne '.\07_Eval_Integration\STRUCTURE_OPTIMIZATION_STATE.md') {
    throw ('Unexpected changed paths: ' + ($changed -join ', '))
}
$allowedAdded = @('.\docs\NEXT_ACTION_PLAN_20261011_MODEL.md',
    '.\04_Consolidation_and_Verification\MANIFEST_workspace_20261011e.sha256',
    '.\04_Consolidation_and_Verification\make_workspace_manifest_20261011f.ps1',
    '.\04_Consolidation_and_Verification\diff_manifest_20261011ef.ps1')
if ($added.Count -ne $allowedAdded.Count) { throw 'Unexpected addition count' }
foreach ($p in $added) { if ($p -notin $allowedAdded) { throw ('Unexpected addition: ' + $p) } }
Write-Output 'Plan/state changes only; index, adapters and existing experimental evidence unchanged.'
