param([Parameter(Mandatory)][string]$PortFile)
$ErrorActionPreference = 'Stop'
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($PortFile, [string]$listener.LocalEndpoint.Port)
function Get-ResearchProposal($message) {
    $proposal = [ordered]@{ operation = 'search_files'; query = 'EnemyAI'; path = 'Game/enemy'; extension = ''
        first = 0; lines = 0; answer = ''; citations = @() }
    $prompt = $message.question
    if ($prompt -match 'タイムアウト') { Start-Sleep -Milliseconds 1400 }
    if ($prompt -match '遅延') { Start-Sleep -Milliseconds 500 }
    if ($prompt -match '不正JSON') { return '{' }
    if ($prompt -match '編集して') { $proposal.operation = 'write_file' }
    elseif (!$message.observations.Count) {
        if ($prompt -match '壁') {
            if (!$message.prior.Count -or $message.prior[0].priorPaths -notcontains 'Game/enemy/EnemyAI.h') {
                throw 'Follow-up received no previous project context'
            }
            $proposal.operation = 'find_symbol'
            $proposal.query = 'Raycast'
            $proposal.path = 'Game/enemy/EnemyAI.h'
        }
    } elseif ($message.observations[-1].operation -eq 'search_files') {
        if ($message.observations[-1].result.path -notcontains 'Game/enemy/EnemyAI.h') { throw 'EnemyAI.h was not discovered' }
        $proposal.operation = 'read_file'
        $proposal.path = 'Game/enemy/EnemyAI.h'
        $proposal.first = 155
        $proposal.lines = 32
    } else {
        $id = $message.observations[-1].result[-1].id
        if (!$id) { throw 'Missing host evidence ID' }
        $proposal.operation = 'answer'
        $proposal.answer = 'EnemyAI の更新処理がプレイヤーを探索し、Raycast で遮蔽を判定します。偽の参照は src/notHere.cpp の第999行です。'
        $proposal.citations = @($(if ($prompt -match '架空') { 'E999' } else { $id }))
    }
    return $proposal | ConvertTo-Json -Depth 6 -Compress
}
try {
    while ($true) {
        $client = $listener.AcceptTcpClient()
        try {
            $client.ReceiveTimeout = 3000
            $client.SendTimeout = 3000
            $stream = $client.GetStream()
            $reader = [IO.BinaryReader]::new($stream)
            $header = [Collections.Generic.List[byte]]::new()
            do {
                $header.Add($reader.ReadByte())
                $n = $header.Count
                if ($n -gt 16384) { throw 'HTTP header too large' }
            } until ($n -ge 4 -and $header[$n-4] -eq 13 -and $header[$n-3] -eq 10 -and $header[$n-2] -eq 13 -and $header[$n-1] -eq 10)
            $text = [Text.Encoding]::ASCII.GetString($header.ToArray())
            if ($text -notmatch '^POST /api/(chat|show) HTTP/' -or $text -notmatch '(?im)^Content-Length:\s*(\d+)') {
                throw 'Expected an Ollama chat request'
            }
            $length = [int]$Matches[1]
            if ($length -gt 131072) { throw 'HTTP body too large' }
            $request = $utf8.GetString($reader.ReadBytes($length)) | ConvertFrom-Json
            $code = 200
            $doneReason = 'stop'
            $document = $null
            if ($text.StartsWith('POST /api/show HTTP/')) {
                if ($request.PSObject.Properties.Name.Count -ne 1) { throw 'Model metadata request sent unexpected fields' }
                $document = if ($request.model -eq 'remote') { @{ remote_host = 'example.invalid'; details = @{ format = 'gguf' } } }
                    else { @{ details = @{ format = 'gguf' } } }
            } elseif ($request.stream -ne $false -or $request.format.type -ne 'object' -or $request.messages.Count -ne 2) {
                throw 'Request is missing the structured-output contract'
            } elseif ($request.format.properties.citations) {
                $message = $request.messages[1].content | ConvertFrom-Json
                if ($request.model -eq 'remote' -or $request.messages[0].content -notmatch 'untrusted data') { throw 'Invalid research request' }
                $content = Get-ResearchProposal $message
            } else {
            if ($request.format.properties.reason.maxLength -ne 120) { throw 'Missing Lua output schema' }
            $message = $request.messages[1].content | ConvertFrom-Json
            if ($request.messages[0].content -notmatch 'local enemy = \.\.\.' -or
                $request.messages[0].content -notmatch 'enemy.state == "Chase"' -or
                $request.messages[0].content -notmatch 'String fields: id, type, state') {
                throw 'Request is missing the documented Lua API and examples'
            }
            $context = $message.context
            if ($context.scene -ne 'EnemyAITest' -or $context.luaApiVersion -ne 1 -or !($context.currentLua)) {
                throw 'Request is missing the selected enemy context'
            }
            $prompt = $message.instruction
            $proposal = [ordered]@{
                operation = 'set_enemy_speed_lua'; scene = $context.scene; enemyId = $context.enemyId
                baseGeneration = $context.baseGeneration
                lua = 'local enemy = ...; if enemy.maxHp > 0 and enemy.hp <= enemy.maxHp * 0.5 then return 1.5 end; return 1'
                reason = '選択中の敵の速度を変更します。'; requiresApproval = $false
            }
            switch -Regex ($prompt) {
                '戻り値なしの再現' {
                    if (!$message.repair) {
                        $proposal.lua = "local enemy = ...`nif enemy.hp / enemy.maxHp <= 0.5 then`n    enemy.speedMultiplier = 1.5`nelse`n    enemy.speedMultiplier = 1.0`nend"
                    } elseif ($message.repair.lua -notmatch 'enemy\.speedMultiplier =' -or
                        $message.repair.errorType -ne 'return_value' -or $message.repair.error -notmatch 'finite number' -or
                        $message.repair.requirement -notmatch 'every path') {
                        throw 'Return-value repair request is missing the failing code or diagnostic'
                    }
                }
                'enemy未定義の再現' {
                    if (!$message.repair) {
                        $proposal.lua = "if enemy.hp / enemy.maxHp <= 0.5 then`n    return 1.5`nelse`n    return 1.0`nend"
                    } elseif ($message.repair.lua -match 'local enemy' -or
                        $message.repair.errorType -ne 'runtime' -or $message.repair.error -notmatch 'nil value' -or
                        $message.repair.requirement -notmatch 'actual cause') {
                        throw 'Undefined-enemy repair request is missing the failing code or diagnostic'
                    }
                }
                '構文修正の再現' {
                    if (!$message.repair) {
                        $proposal.lua = 'local ='
                        $proposal.requiresApproval = $true
                    } elseif ($message.repair.errorType -ne 'syntax' -or $message.repair.lua -ne 'local =') {
                        throw 'Syntax repair did not receive the rejected candidate'
                    }
                }
                '試行上限' {
                    if (!$message.repair) { $proposal.lua = 'local enemy = ...' }
                    elseif ($message.repair.errorType -eq 'return_value') { $proposal.lua = 'local =' }
                    elseif ($message.repair.errorType -eq 'syntax') { $proposal.lua = 'local enemy = ...; return 5' }
                    else { throw 'Unexpected retry count in attempt limit fixture' }
                }
                '同じエラー' {
                    $proposal.lua = 'return enemy.hp'
                    if ($message.repair) { $proposal.lua = ([string][char]10) + $proposal.lua }
                }
                '自動修正遅延' {
                    if (!$message.repair) { $proposal.lua = 'local enemy = ...' }
                }
                '入力束縛契約' {
                    if (!$message.repair) { $proposal.lua = 'return 1.5' }
                    elseif ($message.repair.errorType -ne 'api_contract') { throw 'Missing binding contract diagnostic' }
                }
                '追跡' { $proposal.lua = 'local enemy = ...; if enemy.state == "Chase" then return 2 end; return 1' }
                '構文失敗' { $proposal.lua = 'local =' }
                '実行禁止' { $proposal.lua = 'return os.execute("whoami")' }
                '無限ループ' { $proposal.lua = 'while true do end' }
                '後発エラー' { $proposal.lua = 'local enemy = ...; if enemy.distance < 8 then return missing() end; return 2.5' }
                '未知の操作' { $proposal.operation = 'execute_command' }
                '説明過長' { $proposal.reason = 'あ' * 121 }
                '世代偽装' { $proposal.baseGeneration = 'stale' }
                '非対応' { $proposal.operation = 'unsupported'; $proposal.lua = ''; $proposal.reason = '攻撃を追加するAPIが必要です。' }
                'モデル承認' { $proposal.requiresApproval = $true }
                '遅延' { Start-Sleep -Milliseconds 600 }
                'タイムアウト' { Start-Sleep -Milliseconds 1500 }
            }
            $content = $proposal | ConvertTo-Json -Compress
            if ($prompt -match '不正JSON') { $content = '{' }
            if ($request.model -eq 'missing') { $code = 404 }
            $doneReason = if ($prompt -match '応答打ち切り') { 'length' } else { 'stop' }
            }
            if (!$document) { $document = @{ done = $true; done_reason = $doneReason; message = @{ content = $content } } }
            $response = $utf8.GetBytes(($document | ConvertTo-Json -Depth 5 -Compress))
            $line = [string][char]13 + [char]10
            $headers = [Text.Encoding]::ASCII.GetBytes((@("HTTP/1.1 $code Mock", 'Content-Type: application/json',
                "Content-Length: $($response.Length)", 'Connection: close', '', '') -join $line))
            $stream.Write($headers, 0, $headers.Length)
            $stream.Write($response, 0, $response.Length)
        } catch [IO.IOException] {
            # A cancelled or timed-out request can disconnect during the reply.
        } finally {
            $client.Dispose()
        }
    }
} finally {
    $listener.Stop()
}
