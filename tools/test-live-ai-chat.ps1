param([string]$Executable = '', [switch]$RealModel, [switch]$Research, [string]$BenchmarkConfig = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
if (!$Executable) { $Executable = Join-Path (Split-Path $root) 'generated/outputs/Debug/CG2_Setup.exe' }
$run = Join-Path $root ('generated/live-ai-tests/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
$port = Join-Path $run 'port.txt'
$server = $null
$benchmark = if ($BenchmarkConfig) { Get-Content -LiteralPath $BenchmarkConfig -Raw -Encoding UTF8 | ConvertFrom-Json }
if ($benchmark -and (!$Research -or !$RealModel)) { throw 'BenchmarkConfig requires -Research -RealModel.' }
try {
    if ($Research) {
        $source = New-Item -ItemType Directory -Path (Join-Path $run 'source')
        $outside = New-Item -ItemType Directory -Path (Join-Path $run 'outside')
        New-Item -ItemType Junction -Path (Join-Path $source.FullName 'junction') -Target $outside.FullName | Out-Null
        New-Item -ItemType File -Path (Join-Path $run 'research.txt') | Out-Null
    }
    if ($RealModel) {
        $tags = Invoke-RestMethod http://127.0.0.1:11434/api/tags -TimeoutSec 4
        $selectedModel = if ($benchmark) { $benchmark.model } else { 'qwen2.5-coder:3b' }
        if ($tags.models.name -notcontains $selectedModel) { throw "The local model $selectedModel is not available" }
        if ($benchmark) { Copy-Item -LiteralPath $BenchmarkConfig -Destination (Join-Path $run 'benchmark.json') }
        [IO.File]::WriteAllText($port, '11434')
        New-Item -ItemType File -Path (Join-Path $run 'real-model.txt') | Out-Null
    } else {
        $fixture = Join-Path $root 'tests/LiveAiMockServer.ps1'
        $shell = (Get-Process -Id $PID).Path
        $serverArgs = '-NoProfile -File "' + $fixture + '" -PortFile "' + $port + '"'
        $processArgs = @{ FilePath = $shell; ArgumentList = $serverArgs; WindowStyle = 'Hidden'; PassThru = $true
            RedirectStandardError = (Join-Path $run 'server-error.txt') }
        $server = Start-Process @processArgs
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (!(Test-Path -LiteralPath $port)) {
        if ($server.HasExited -or [DateTime]::UtcNow -ge $deadline) {
            throw ('Mock server did not start: ' + (Get-Content (Join-Path $run 'server-error.txt') -Raw))
        }
        Start-Sleep -Milliseconds 40
    }
    $env:YAN_LIVE_AI_TEST_RUN = Split-Path $run -Leaf
    $game = Start-Process -FilePath $Executable -ArgumentList '--enemy-ai-chat-test' -WorkingDirectory $root -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(35)
    while (!(Test-Path -LiteralPath (Join-Path $run 'inference-started.txt')) -and !$game.HasExited) {
        if ([DateTime]::UtcNow -ge $deadline) { Stop-Process -Id $game.Id; throw 'Game did not start live inference' }
        Start-Sleep -Milliseconds 20
    }
    if (!$game.HasExited -and !$RealModel) {
        $pipe = [IO.Pipes.NamedPipeClientStream]::new('.', "DebugAI_CG5_LiveAiTest_$($game.Id)", 'InOut', 'Asynchronous')
        try {
            $pipe.Connect(3000)
            $pipe.ReadMode = [IO.Pipes.PipeTransmissionMode]::Message
            $statusRequest = [Text.Encoding]::UTF8.GetBytes('{"protocolVersion":1,"gameId":"CG5","messageType":"StatusRequest","sequence":77}')
            $bytes = [byte[]]::new(1024 * 1024)
            $read = $pipe.ReadAsync($bytes, 0, $bytes.Length)
            $write = $pipe.WriteAsync($statusRequest, 0, $statusRequest.Length)
            if (!$write.Wait(4000)) { throw 'DebugAI status request timed out' }
            if (!$read.Wait(4000) -or !$pipe.IsMessageComplete) { throw 'DebugAI status response timed out or was incomplete' }
            $status = [Text.Encoding]::UTF8.GetString($bytes, 0, $read.Result) | ConvertFrom-Json
            if ($status.messageType -ne 'StatusResponse' -or $status.sequence -ne 77) {
                throw "DebugAIViewer status protocol mismatch during inference: $($status.messageType), sequence $($status.sequence)"
            }
            Write-Output 'PASS: DebugAIViewer Named Pipe StatusRequest responded during local AI generation.'
        } finally {
            $pipe.Dispose()
        }
    }
    $gameTimeout = if ($RealModel -and $Research) { 630000 } elseif ($RealModel) { 560000 } else { 90000 }
    if ($benchmark) {
        $gpu = Get-Command nvidia-smi -ErrorAction SilentlyContinue
        $samples = [Collections.Generic.List[object]]::new()
        $deadline = [DateTime]::UtcNow.AddSeconds(130 * $benchmark.runs * $benchmark.questions.Count)
        while (!$game.HasExited -and [DateTime]::UtcNow -lt $deadline) {
            if ($gpu) {
                $sample = & $gpu.Source --query-gpu=memory.used --format=csv,noheader,nounits -i 0 2>$null
                $used = 0
                if ([int]::TryParse([string]($sample | Select-Object -First 1), [ref]$used)) {
                    $samples.Add(@{ atMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(); usedMiB = $used })
                }
            }
            [void]$game.WaitForExit(750)
        }
        ConvertTo-Json -InputObject $samples -Depth 4 | Set-Content -LiteralPath (Join-Path $run 'gpu.json') -Encoding utf8
        if (!$game.HasExited) { Stop-Process -Id $game.Id; throw 'Live AI benchmark timed out' }
    } elseif (!$game.WaitForExit($gameTimeout)) { Stop-Process -Id $game.Id; throw 'Live AI test timed out' }
    $game.Refresh()
    $result = Get-Content -LiteralPath (Join-Path $run 'result.txt') -Raw -Encoding UTF8
    Write-Output "Report: $(Join-Path $run 'result.txt')"
    $result
    if ($game.ExitCode -ne 0 -or !$result.StartsWith('PASS:')) { throw 'Live AI test failed' }
} finally {
    Remove-Item Env:YAN_LIVE_AI_TEST_RUN -ErrorAction SilentlyContinue
    if ($game -and !$game.HasExited) { Stop-Process -Id $game.Id }
    if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id }
}
