$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath 'D:\StringLearnedIndex_Workspace').Path
$outPath = Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011u.sha256'
if (Test-Path -LiteralPath $outPath) { throw 'Refusing to overwrite existing snapshot u' }
$prefixLen = $root.Length + 1
$rows = New-Object System.Collections.Generic.List[string]
foreach ($f in [System.IO.Directory]::GetFiles($root, '*', [System.IO.SearchOption]::AllDirectories)) {
    $rel = $f.Substring($prefixLen)
    if ($rel -match '(^|\\)(build|\.git|\.agents|\.codex|\.aws|\.trash)(\\|$)') { continue }
    $h = (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower()
    $rows.Add($h + '  .\' + $rel)
}
$sorted = $rows.ToArray()
[Array]::Sort($sorted, [System.StringComparer]::Ordinal)
[System.IO.File]::WriteAllLines($outPath, $sorted)
$verified = 0
foreach ($line in [System.IO.File]::ReadAllLines($outPath)) {
    $file = Join-Path $root $line.Substring(68)
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLower() -ne $line.Substring(0,64)) {
        throw ('Snapshot verification mismatch: ' + $file)
    }
    $verified += 1
}
$manifestRel = $outPath.Substring($prefixLen)
$listed = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($line in [System.IO.File]::ReadAllLines($outPath)) {
    [void]$listed.Add($line.Substring(68))
}
$now = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($f in [System.IO.Directory]::GetFiles($root, '*', [System.IO.SearchOption]::AllDirectories)) {
    $rel = $f.Substring($prefixLen)
    if ($rel -match '(^|\\)(build|\.git|\.agents|\.codex|\.aws|\.trash)(\\|$)') { continue }
    if ($rel -eq $manifestRel) { continue }
    [void]$now.Add($rel)
}
$added = 0; foreach ($p in $now) { if (-not $listed.Contains($p)) { $added += 1; Write-Output ('added since snapshot: ' + $p) } }
$removed = 0; foreach ($p in $listed) { if (-not $now.Contains($p)) { $removed += 1; Write-Output ('removed since snapshot: ' + $p) } }
Write-Output ('u written and verified: {0} entries, {1} added, {2} removed, 0 changed' -f $verified, $added, $removed)