param([ValidateRange(0, 1000)][int]$KeepUnused = 8, [switch]$Apply, [string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$root = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path (Split-Path $PSScriptRoot) 'generated/live-enemy-diagnostics'
}
if (!(Test-Path -LiteralPath $root -PathType Container)) { return }
if ((Get-Item -LiteralPath $root).Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw 'The live diagnostics root is a reparse point.'
}

$publishLock = [IO.File]::Open((Join-Path $root 'publish.lock'), 'OpenOrCreate', 'Write', 'None')
try {
    $historyLock = [IO.File]::Open((Join-Path $root 'history.lock'), 'OpenOrCreate', 'Write', 'None')
    try {
        $keep = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $latest = Join-Path $root 'latest.json'
        if (Test-Path -LiteralPath $latest) {
            $published = Get-Content -LiteralPath $latest -Raw | ConvertFrom-Json
            if ($published.generation -cnotmatch '^[0-9a-f]{32}$') { throw 'Invalid latest.json.' }
            [void]$keep.Add($published.generation)
        }
        $historyPath = Join-Path $root 'history.json'
        if (Test-Path -LiteralPath $historyPath) {
            $history = Get-Content -LiteralPath $historyPath -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($history.schemaVersion -ne 1 -or $null -eq $history.generations -or $null -eq $history.operations) {
                throw 'Unsupported or invalid history.json.'
            }
            foreach ($generation in $history.generations) {
                if ($generation.generation -cnotmatch '^[0-9a-f]{32}$') { throw 'Invalid history generation.' }
                [void]$keep.Add($generation.generation)
            }
            foreach ($operation in $history.operations) {
                foreach ($change in $operation.changes) {
                    if ($change.resource -eq 'EnemyAIDiagnostics') {
                        if ($change.before -cmatch '^[0-9a-f]{32}$') { [void]$keep.Add($change.before) }
                        if ($change.after -cmatch '^[0-9a-f]{32}$') { [void]$keep.Add($change.after) }
                    }
                }
            }
        }
        $unused = 0
        foreach ($dir in (Get-ChildItem -LiteralPath $root -Directory | Sort-Object LastWriteTimeUtc -Descending)) {
            $id = $dir.Name
            if ($id -cnotmatch '^[0-9a-f]{32}$' -or $keep.Contains($id) -or
                ($dir.Attributes -band [IO.FileAttributes]::ReparsePoint)) { continue }
            $manifestPath = Join-Path $dir.FullName 'generation.json'
            if (!(Test-Path -LiteralPath $manifestPath -PathType Leaf)) { continue }
            $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
            if ($manifest.schemaVersion -ne 1 -or $manifest.generation -cne $id) { continue }
            if ($unused++ -lt $KeepUnused) { continue }
            if (!$Apply) { Write-Output "Would prune $id"; continue }
            $base = Join-Path $dir.FullName "EnemyAIDiagnostics-$id"
            try {
                foreach ($ext in '.dll', '.pdb', '.lib', '.exp') {
                    $path = "$base$ext"
                    if (Test-Path -LiteralPath $path -PathType Leaf) {
                        [IO.File]::Open($path, 'Open', 'Read', 'None').Dispose()
                    }
                }
                foreach ($ext in '.dll', '.pdb', '.lib', '.exp') {
                    $path = "$base$ext"
                    if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path }
                }
                Remove-Item -LiteralPath $manifestPath
                if (!(Get-ChildItem -LiteralPath $dir.FullName -Force)) {
                    [IO.Directory]::Delete($dir.FullName)
                }
                Write-Output "Pruned $id"
            } catch [IO.IOException] {
                Write-Warning "Skipped $id (file in use or unavailable): $_"
            } catch [UnauthorizedAccessException] {
                Write-Warning "Skipped $id (file in use or unavailable): $_"
            }
        }
    } finally {
        $historyLock.Dispose()
    }
} finally {
    $publishLock.Dispose()
}
