param([string]$Executable = '', [string]$CacheNamespace = '', [ValidateSet('baseline','cold','warm')][string[]]$Variants = @('baseline','cold','warm'))
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
if (!$Executable) { $Executable = Join-Path (Split-Path $projectRoot) 'generated/outputs/Debug/CG2_Setup.exe' }
if (!$CacheNamespace) { $CacheNamespace = 'benchmark-' + [Guid]::NewGuid().ToString('N') }
if ($CacheNamespace -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid cache namespace.' }
$testDir = Join-Path $projectRoot 'generated/loading-tests'
New-Item -ItemType Directory -Force $testDir | Out-Null
$report = Join-Path $testDir (Split-Path (Split-Path $Executable) -Leaf)
New-Item -ItemType Directory -Force $report | Out-Null
Set-Content -LiteralPath (Join-Path $report 'cache-namespace.txt') -Value $CacheNamespace
$summaries = @{}
foreach ($variant in $Variants) {
    $resultPath = Join-Path $testDir 'result.txt'
    Set-Content -LiteralPath $resultPath -Value 'PENDING'
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Executable
    $info.Arguments = '--asset-loading-test'
    $info.WorkingDirectory = $projectRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.Environment['YAN_LOAD_PROFILE'] = '1'
    $info.Environment['YAN_ASSET_CACHE_NAMESPACE'] = $CacheNamespace
    $info.Environment['YAN_ASSET_CACHE'] = $(if ($variant -eq 'baseline') { '0' } else { '1' })
    $info.Environment['YAN_TEXTURE_BATCH'] = $(if ($variant -eq 'baseline') { '0' } else { '1' })
    $info.Environment['YAN_EAGER_BONE_DEBUG'] = $(if ($variant -eq 'baseline') { '1' } else { '0' })
    $process = [Diagnostics.Process]::Start($info)
    if (!$process.WaitForExit(240000)) {
        Stop-Process -Id $process.Id
        throw "Asset loading $variant tests timed out (240s)."
    }
    $result = Get-Content -LiteralPath $resultPath -Raw
    if ($process.ExitCode -ne 0 -or !$result.StartsWith('PASS:')) { throw "Asset loading $variant failed (exit $($process.ExitCode)): $result" }
    Copy-Item -LiteralPath (Join-Path $testDir 'models.csv') -Destination (Join-Path $testDir "$variant-models.csv")
    Copy-Item -LiteralPath (Join-Path $testDir 'stages.csv') -Destination (Join-Path $testDir "$variant-stages.csv")
    Copy-Item -LiteralPath (Join-Path $testDir 'models.csv') -Destination (Join-Path $report "$variant-models.csv")
    Copy-Item -LiteralPath (Join-Path $testDir 'stages.csv') -Destination (Join-Path $report "$variant-stages.csv")
    Copy-Item -LiteralPath $resultPath -Destination (Join-Path $report "$variant-result.txt")
    $summaries[$variant] = @(Import-Csv -LiteralPath (Join-Path $testDir 'models.csv'))
    Write-Output "$variant $result"
}
if ($Variants.Count -lt 3) {
    Write-Output 'PASS: selected asset loading conditions. Run all three conditions for the full comparison.'
    return
}
foreach ($variant in @('cold', 'warm')) {
    foreach ($row in $summaries[$variant]) {
        $original = $summaries['baseline'] | Where-Object model -eq $row.model
        if ($original.model_digest -ne $row.model_digest) { throw "Model data changed for $($row.model) in $variant." }
    }
}
$rows = foreach ($row in $summaries['baseline']) {
    $cold = $summaries['cold'] | Where-Object model -eq $row.model
    $warm = $summaries['warm'] | Where-Object model -eq $row.model
    [PSCustomObject]@{
        model = $row.model
        baseline_ms = $row.milliseconds
        cold_ms = $cold.milliseconds
        warm_ms = $warm.milliseconds
        baseline_uploads = $row.upload_submissions
        cold_uploads = $cold.upload_submissions
        warm_uploads = $warm.upload_submissions
    }
}
$rows | Export-Csv -LiteralPath (Join-Path $testDir 'comparison.csv') -NoTypeInformation -Encoding utf8
$rows | Export-Csv -LiteralPath (Join-Path $report 'comparison.csv') -NoTypeInformation -Encoding utf8
$rows | Format-Table -AutoSize
Write-Output 'PASS: baseline/cold/warm model geometry, weights, materials, nodes and animation data are identical.'
