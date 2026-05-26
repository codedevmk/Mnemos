#!/usr/bin/env pwsh
# Convert a binary PPM (P6) to PNG. Used by the mnemos_player offline
# screenshot path so we can read framebuffers with image tools.

param(
    [Parameter(Mandatory)][string]$InputPath,
    [Parameter(Mandatory)][string]$OutputPath
)

if (-not (Test-Path $InputPath)) { throw "no input: $InputPath" }
Add-Type -AssemblyName System.Drawing

$bytes = [System.IO.File]::ReadAllBytes($InputPath)
# Parse PPM header: "P6\n<w> <h>\n255\n" (whitespace can be space or newline)
$pos = 0
function Read-Token {
    param([byte[]]$Buf, [ref]$Pos)
    while ($Buf[$Pos.Value] -eq 10 -or $Buf[$Pos.Value] -eq 32 -or $Buf[$Pos.Value] -eq 13 -or $Buf[$Pos.Value] -eq 9) { $Pos.Value++ }
    $start = $Pos.Value
    while (-not ($Buf[$Pos.Value] -eq 10 -or $Buf[$Pos.Value] -eq 32 -or $Buf[$Pos.Value] -eq 13 -or $Buf[$Pos.Value] -eq 9)) { $Pos.Value++ }
    [System.Text.Encoding]::ASCII.GetString($Buf, $start, $Pos.Value - $start)
}
$posRef = [ref]$pos
$magic = Read-Token -Buf $bytes -Pos $posRef
if ($magic -ne 'P6') { throw "not a P6 PPM: $magic" }
$w = [int](Read-Token -Buf $bytes -Pos $posRef)
$h = [int](Read-Token -Buf $bytes -Pos $posRef)
$max = [int](Read-Token -Buf $bytes -Pos $posRef)
# Skip exactly one whitespace byte after the maxval (PPM spec)
$pos = $posRef.Value + 1
$expected = $w * $h * 3
if ($bytes.Length - $pos -lt $expected) { throw "PPM truncated: have $($bytes.Length - $pos) need $expected" }

$bmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
$data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)
$stride = $data.Stride
$buf = New-Object byte[] ($stride * $h)
for ($y = 0; $y -lt $h; $y++) {
    for ($x = 0; $x -lt $w; $x++) {
        $r = $bytes[$pos++]; $g = $bytes[$pos++]; $b = $bytes[$pos++]
        $row = $y * $stride + $x * 3
        # BMP scanlines are BGR
        $buf[$row + 0] = $b
        $buf[$row + 1] = $g
        $buf[$row + 2] = $r
    }
}
[System.Runtime.InteropServices.Marshal]::Copy($buf, 0, $data.Scan0, $buf.Length)
$bmp.UnlockBits($data)
$bmp.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "wrote $OutputPath ($w x $h)"
