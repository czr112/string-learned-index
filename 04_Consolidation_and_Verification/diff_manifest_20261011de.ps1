$ErrorActionPreference = 'Stop'
$root = 'D:\StringLearnedIndex_Workspace'
function Read-Manifest($path) {
    $map = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($path)) { $map[$line.Substring(66)] = $line.Substring(0,64) }
    return $map
}
$old = Read-Manifest (Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011d.sha256')
$new = Read-Manifest (Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011e.sha256')
$added = @($new.Keys | Where-Object { -not $old.ContainsKey($_) } | Sort-Object)
$removed = @($old.Keys | Where-Object { -not $new.ContainsKey($_) } | Sort-Object)
$changed = @($new.Keys | Where-Object { $old.ContainsKey($_) -and $old[$_] -ne $new[$_] } | Sort-Object)
Write-Output ('d={0} e={1} added={2} removed={3} changed={4}' -f $old.Count,$new.Count,$added.Count,$removed.Count,$changed.Count)
$allowedChanged = @(
    '.\04_Consolidation_and_Verification\diff_manifest_20261011cd.ps1',
    '.\docs\NEXT_ACTION_PLAN_20261011.md',
    '.\07_Eval_Integration\STRUCTURE_OPTIMIZATION_STATE.md',
    '.\02_Code_and_Experiments\validation_e25_20260918\RESULTS_HOLE_REUSE_20261011.md'
)
if ($removed.Count) { throw ('Unexpected removed paths: ' + ($removed -join ', ')) }
foreach ($p in $changed) { if ($p -notin $allowedChanged) { throw ('Unexpected changed path: ' + $p) } }
foreach ($p in $added) {
    if ($p -match '^\.\\07_Eval_Integration\\results\\subblock_(entry|prototype)_20261011\\') { continue }
    if ($p -match '^\.\\07_Eval_Integration\\diagnostics\\(subblock_entry\.cpp|run_subblock_entry\.py|make_subblock_prototype\.py|lookup_subblock\.cpp|run_subblock_prototype\.py|verify_subblock_results\.py|audit_h_existing\.py)$') { continue }
    if ($p -in @('.\docs\NEXT_ACTION_PLAN_20261011_Q.md', '.\07_Eval_Integration\RESULTS_SUBBLOCK_ROUTING_20261011.md',
                 '.\04_Consolidation_and_Verification\AUDIT_H_PROTOCOL_20261011.json',
                 '.\04_Consolidation_and_Verification\MANIFEST_workspace_20261011d.sha256',
                 '.\04_Consolidation_and_Verification\make_workspace_manifest_20261011e.ps1',
                 '.\04_Consolidation_and_Verification\diff_manifest_20261011de.ps1')) { continue }
    throw ('Unexpected added path: ' + $p)
}
Write-Output 'All additions/changes accounted for; production source unchanged.'
$changed | ForEach-Object { Write-Output ('CHANGED ' + $_) }
