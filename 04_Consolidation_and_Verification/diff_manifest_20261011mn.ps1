$ErrorActionPreference = 'Stop'
$root = 'D:\StringLearnedIndex_Workspace'
function Read-Manifest($path) {
    $map = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($path)) { $map[$line.Substring(66)] = $line.Substring(0,64) }
    return $map
}
$old = Read-Manifest (Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011m.sha256')
$new = Read-Manifest (Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011n.sha256')
$added = @($new.Keys | Where-Object { -not $old.ContainsKey($_) } | Sort-Object)
$removed = @($old.Keys | Where-Object { -not $new.ContainsKey($_) } | Sort-Object)
$changed = @($new.Keys | Where-Object { $old.ContainsKey($_) -and $old[$_] -ne $new[$_] } | Sort-Object)
Write-Output ('m={0} n={1} added={2} removed={3} changed={4}' -f $old.Count,$new.Count,$added.Count,$removed.Count,$changed.Count)
Write-Output '--- changed ---'
foreach ($p in $changed) { Write-Output ('  ~ ' + $p) }
Write-Output '--- added by top-level dir ---'
$added | ForEach-Object { ($_ -split '\\')[1..2] -join '\' } | Group-Object | Sort-Object Count -Descending | Select-Object Count,Name | Format-Table -AutoSize | Out-String -Width 200
Write-Output '--- removed ---'
foreach ($p in $removed) { Write-Output ('  - ' + $p) }