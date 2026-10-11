$root = 'D:\StringLearnedIndex_Workspace'
function Read-Manifest($path) { $map=@{}; foreach ($line in [System.IO.File]::ReadAllLines($path)) { $map[$line.Substring(66)] = $line.Substring(0,64) }; return $map }
$oldPath = Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011r.sha256'
$newPath = Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011s.sha256'
$old = Read-Manifest $oldPath
$new = Read-Manifest $newPath
$added = @($new.Keys | Where-Object { -not $old.ContainsKey($_) } | Sort-Object)
$removed = @($old.Keys | Where-Object { -not $new.ContainsKey($_) } | Sort-Object)
$changed = @($new.Keys | Where-Object { $old.ContainsKey($_) -and $old[$_] -ne $new[$_] } | Sort-Object)
Write-Output ('r={0} s={1} added={2} removed={3} changed={4}' -f $old.Count,$new.Count,$added.Count,$removed.Count,$changed.Count)
foreach ($p in $added) { Write-Output ('  + ' + $p) }
foreach ($p in $removed) { Write-Output ('  - ' + $p) }
foreach ($p in $changed) { Write-Output ('  ~ ' + $p) }
