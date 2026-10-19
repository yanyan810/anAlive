$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot
$entries = @(
    @{ Name = 'enemy_ai_entry.png'; Width = 410; Height = 48; Text = 'F6 : Enemy AI Test（敵AIテスト）' },
    @{ Name = 'enemy_ai_help.png'; Width = 1000; Height = 80; Text = "敵AIテスト　Tab: UI／FPS切替　Esc: カーソル解放　WASD: 移動　マウス: 視点`nP: 全AI停止／再開　F5: 全AIリセット　F6: タイトル　開始時は全AI停止中です" }
)
foreach ($entry in $entries) {
    $bitmap = [Drawing.Bitmap]::new($entry.Width, $entry.Height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $font = [Drawing.Font]::new('Meiryo', 20, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
    try {
        $graphics.Clear([Drawing.Color]::FromArgb(210, 12, 18, 26))
        $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $graphics.DrawString($entry.Text, $font, [Drawing.Brushes]::White, [Drawing.RectangleF]::new(10, 8, $entry.Width - 20, $entry.Height - 8))
        $bitmap.Save((Join-Path $root ('resources/ui/' + $entry.Name)), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $font.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
}
