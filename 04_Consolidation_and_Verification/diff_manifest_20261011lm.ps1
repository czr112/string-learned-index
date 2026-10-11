$ErrorActionPreference = 'Stop'
$root = 'D:\StringLearnedIndex_Workspace'
function Read-Manifest($path) {
    $map = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($path)) { $map[$line.Substring(66)] = $line.Substring(0,64) }
    return $map
}
$old = Read-Manifest (Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011l.sha256')
$new = Read-Manifest (Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011m.sha256')
$added = @($new.Keys | Where-Object { -not $old.ContainsKey($_) } | Sort-Object)
$removed = @($old.Keys | Where-Object { -not $new.ContainsKey($_) } | Sort-Object)
$changed = @($new.Keys | Where-Object { $old.ContainsKey($_) -and $old[$_] -ne $new[$_] } | Sort-Object)
Write-Output ('l={0} m={1} added={2} removed={3} changed={4}' -f $old.Count,$new.Count,$added.Count,$removed.Count,$changed.Count)
foreach ($p in $added) { Write-Output ('  + ' + $p) }
foreach ($p in $changed) { Write-Output ('  ~ ' + $p) }
foreach ($p in $removed) { Write-Output ('  - ' + $p) }
$expectedChanged = @(
    '.\02_Code_and_Experiments\validation_e25_20260918\index.hpp',
    '.\02_Code_and_Experiments\validation_e25_20260918\switches.hpp',
    '.\07_Eval_Integration\adapters\our_index\benchmark-adapter.json',
    '.\07_Eval_Integration\adapters\our_index\our_index_adapter.cpp',
    '.\07_Eval_Integration\adapters\our_index_lits\benchmark-adapter.json',
    '.\07_Eval_Integration\adapters\our_index_lits_routing\benchmark-adapter.json',
    '.\07_Eval_Integration\adapters\our_index_routing\benchmark-adapter.json',
    '.\07_Eval_Integration\STRUCTURE_OPTIMIZATION_STATE.md'
) | Sort-Object
$diff = Compare-Object $expectedChanged $changed
if ($diff) { throw ('Unexpected changed set: ' + ($diff | ForEach-Object { $_.InputObject + '/' + $_.SideIndicator } ) -join ', ') }
if ($added.Count -ne 16) { throw ('Unexpected addition count: ' + $added.Count) }
Write-Output 'M3 integration round changes only (production switch + adapters + records).'