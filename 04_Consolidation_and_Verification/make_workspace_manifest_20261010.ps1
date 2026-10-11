$root = 'D:\StringLearnedIndex_Workspace'
$outPath = Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261010.sha256'
$rootFull = (Resolve-Path -LiteralPath $root).Path
$prefixLen = $rootFull.Length + 1

$all = [System.IO.Directory]::GetFiles($rootFull, '*', [System.IO.SearchOption]::AllDirectories)
$keep = New-Object System.Collections.Generic.List[string]
foreach ($f in $all) {
    $rel = $f.Substring($prefixLen)
    if ($rel -match '(^|\\)(build|\.git|\.agents|\.codex|\.aws|\.trash)(\\|$)') { continue }
    $keep.Add($rel)
}

$rows = New-Object System.Collections.Generic.List[string]
foreach ($rel in $keep) {
    $h = (Get-FileHash -LiteralPath (Join-Path $rootFull $rel) -Algorithm SHA256).Hash.ToLower()
    $rows.Add(($h + '  .\' + $rel))
}
$sorted = $rows.ToArray()
[Array]::Sort($sorted, [System.StringComparer]::Ordinal)
[System.IO.File]::WriteAllLines($outPath, $sorted)
Write-Output ('written {0} entries to {1}' -f $sorted.Count, $outPath)
