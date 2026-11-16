param([ValidateRange(2, 5)][int]$Runs = 2, [switch]$ColdLoad)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$directory = Join-Path $root ('generated/project-research-benchmark/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $directory | Out-Null
$rubric = Get-Content -LiteralPath (Join-Path $root 'tests/ProjectResearchBenchmark.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$questions = @($rubric | ForEach-Object { @{ id = $_.id; question = $_.question; followup = [bool]$_.followup } })
$local = 'http://127.0.0.1:11434'
$tags = (Invoke-RestMethod "$local/api/tags" -TimeoutSec 5).models
$report = [ordered]@{
    commit = (git -C $root rev-parse HEAD)
    dirty = [bool](git -C $root status --porcelain)
    runs = $Runs
    coldLoadRequested = [bool]$ColdLoad
    generation = @{ temperature = 0; num_ctx = 4096; num_predict = 680; repeat_penalty = 1.15 }
    gpu = if (Get-Command nvidia-smi -ErrorAction SilentlyContinue) { & nvidia-smi --query-gpu=name,memory.total --format=csv,noheader -i 0 }
    result = @()
}
$table = @('# YanEngine Project Research Benchmark', '', "Git ``$($report.commit)`` / dirty=$($report.dirty)", '',
    '自動評価は根拠範囲・SHA-256の鮮度・ホスト検証・状態保持を確認します。モデル文章の意味の採点は別途行います。',
    'FPSは実D3Dのテストループの描画数から算出します。最大フレーム時間は入力遅延の実測と区別します。', '',
    '| モデル | 試行数 | 根拠範囲一致 | 平均応答 ms | 初回ロード ms | 最大 VRAM MiB | 平均FPS | 状態保持 |',
    '|---|---:|---:|---:|---:|---:|---:|---:|')
foreach ($model in 'qwen2.5-coder:3b', 'qwen2.5-coder:7b') {
    $installed = $tags | Where-Object name -eq $model | Select-Object -First 1
    if (!$installed) {
        $report.result += @{ model = $model; status = 'not_installed' }
        $table += "| $model | 未導入 | — | — | — | — | — | — |"
        continue
    }
    $record = [ordered]@{ model = $model; quantization = $installed.details.quantization_level; modelDigest = $installed.digest;
        sizeBytes = $installed.size; status = 'measured'; firstLoad = 'resident_or_unknown'; questions = @() }
    try {
        if ($ColdLoad) {
            $loaded = (Invoke-RestMethod "$local/api/ps" -TimeoutSec 5).models
            if ($loaded.name -contains $model) {
                $body = @{ model = $model; keep_alive = 0; stream = $false } | ConvertTo-Json -Compress
                Invoke-RestMethod "$local/api/generate" -ContentType 'application/json' -Method Post -Body $body -TimeoutSec 15 | Out-Null
            }
            if ((Invoke-RestMethod "$local/api/ps" -TimeoutSec 5).models.name -notcontains $model) { $record.firstLoad = 'cold' }
        }
        $config = Join-Path $directory ($model.Replace(':', '-') + '.json')
        @{ model = $model; runs = $Runs; questions = $questions } | ConvertTo-Json -Depth 5 |
            Set-Content -LiteralPath $config -Encoding utf8NoBOM
        $log = & (Join-Path $PSScriptRoot 'test-live-ai-chat.ps1') -Research -RealModel -BenchmarkConfig $config
        $path = $log | Where-Object { $_ -is [string] -and $_.StartsWith('Report: ') } | Select-Object -Last 1
        if (!$path) { throw 'Game report path missing.' }
        $run = Split-Path $path.Substring(8)
        $record.runtimeDirectory = $run
        $samples = @(Get-Content -LiteralPath (Join-Path $run 'gpu.json') -Raw | ConvertFrom-Json)
        $rows = @(Get-Content -LiteralPath (Join-Path $run 'benchmark-results.json') -Raw -Encoding UTF8 | ConvertFrom-Json)
        foreach ($row in $rows) {
            $expected = $rubric | Where-Object id -eq $row.id
            $missing = @($expected.evidence | Where-Object {
                $pathPrefix, $fragment = $_
                @($row.references | Where-Object { $_.path.StartsWith($pathPrefix) -and $_.excerpt.Contains($fragment) -and $_.fresh }).Count -eq 0
            })
            $peak = ($samples | Where-Object { $_.atMs -ge $row.startedAtMs -and $_.atMs -le $row.finishedAtMs } |
                Measure-Object -Property usedMiB -Maximum).Maximum
            $auto = @{ evidenceCoverage = !$row.error -and ($missing.Count -eq 0) -and $expected.evidence.Count -gt 0;
                expectedEvidenceMissing = $missing; verifiedClaims = @($row.claims | Where-Object verified).Count;
                absenceLeftUnconfirmed = if ($row.id -eq 'Q7') { !$row.error -and @($row.claims | Where-Object verified).Count -eq 0 } else { $null } }
            $row | Add-Member -NotePropertyMembers @{ gpuPeakMiB = $peak; automatic = $auto; manualAssessment = 'pending'; inputLatencyMs = $null }
        }
        $record.questions = $rows
        $ms = [math]::Round(($rows | Measure-Object elapsedMs -Average).Average)
        $fps = [math]::Round(($rows | Measure-Object fps -Average).Average, 1)
        $peak = ($rows | Where-Object { $null -ne $_.gpuPeakMiB } | Measure-Object gpuPeakMiB -Maximum).Maximum
        $evidence = @($rows | Where-Object { $_.automatic.evidenceCoverage }).Count
        $state = @($rows | Where-Object statePreserved).Count
        $load = if ($null -ne $rows[0].loadMs) { [math]::Round($rows[0].loadMs) } else { '—' }
        $table += "| $model | $($rows.Count) | $evidence | $ms | $load | $peak | $fps | $state/$($rows.Count) |"
    } catch {
        $record.status = 'error'
        $record.error = $_.Exception.Message
        $table += "| $model | 測定エラー | — | — | — | — | — | — |"
    }
    $report.result += $record
}
$report | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (Join-Path $directory 'report.json') -Encoding utf8
$table | Set-Content -LiteralPath (Join-Path $directory 'report.md') -Encoding utf8
Write-Output $directory
Get-Content -LiteralPath (Join-Path $directory 'report.md') -Encoding UTF8
if (@($report.result | Where-Object status -eq 'measured').Count -eq 0 -or @($report.result | Where-Object status -eq 'error').Count) { exit 1 }
