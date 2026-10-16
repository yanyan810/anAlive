$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$bitmap = [Drawing.Bitmap]::new(640,192)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([Drawing.Color]::Transparent)
$graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$font = [Drawing.Font]::new('Yu Gothic UI',25,[Drawing.FontStyle]::Bold,[Drawing.GraphicsUnit]::Pixel)
$format = [Drawing.StringFormat]::new()
$format.Alignment = [Drawing.StringAlignment]::Center
$format.LineAlignment = [Drawing.StringAlignment]::Center
$labels = @('E：シャッターを開く','シャッター開放中（操作済み）','シャッター全開（操作済み）')
for ($row=0; $row -lt 3; $row++) {
    $rectangle = [Drawing.RectangleF]::new(0,$row*64,640,64)
    $background = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(190,15,20,25))
    $graphics.FillRectangle($background,$rectangle)
    $graphics.DrawString($labels[$row],$font,[Drawing.Brushes]::White,$rectangle,$format)
    $background.Dispose()
}
$bitmap.Save((Join-Path (Split-Path $PSScriptRoot) 'resources/ui/shutter_prompt.png'),[Drawing.Imaging.ImageFormat]::Png)
$format.Dispose(); $font.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
