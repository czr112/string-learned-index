$root = 'D:\StringLearnedIndex_Workspace'
$oldPath = Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011c.sha256'
$newPath = Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011d.sha256'
function Load($p) { $h=@{}; foreach ($l in [System.IO.File]::ReadAllLines($p)) { if ($l.Length -lt 70) { continue }; $h[$l.Substring(66)] = $l.Substring(0,64) }; return $h }
$o = Load $oldPath
$n = Load $newPath
$added = @(); $removed = @(); $changed = @()
foreach ($k in $n.Keys) { if (-not $o.ContainsKey($k)) { $added += $k } elseif ($o[$k] -ne $n[$k]) { $changed += $k } }
foreach ($k in $o.Keys) { if (-not $n.ContainsKey($k)) { $removed += $k } }
Write-Output ('old={0} new={1} added={2} removed={3} changed={4}' -f $o.Count, $n.Count, $added.Count, $removed.Count, $changed.Count)
Write-Output '--- ADDED ---'; $added | Sort-Object | ForEach-Object { Write-Output $_ }
Write-Output '--- REMOVED ---'; $removed | Sort-Object | ForEach-Object { Write-Output $_ }
Write-Output '--- CHANGED ---'; $changed | Sort-Object | ForEach-Object { Write-Output $_ }