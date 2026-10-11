$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath 'D:\StringLearnedIndex_Workspace').Path
$outPath = Join-Path $root '04_Consolidation_and_Verification\MANIFEST_workspace_20261011h.sha256'
if (Test-Path -LiteralPath $outPath) { throw 'Refusing to overwrite existing snapshot h' }
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
Write-Output ('h written and verified: {0} entries, 0 drift' -f $verified)