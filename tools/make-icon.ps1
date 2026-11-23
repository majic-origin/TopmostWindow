Add-Type -AssemblyName System.Drawing

$root  = Split-Path -Parent $PSScriptRoot          # workspace root
$src   = Join-Path $root 'assets\logo\png\favicon-512.png'
$dst   = Join-Path $PSScriptRoot 'src\app.ico'
if (-not (Test-Path $src)) { Write-Output ("MISSING " + $src); exit 1 }

$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$source = [System.Drawing.Image]::FromFile($src)

$pngBlobs = New-Object System.Collections.ArrayList
foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($source, (New-Object System.Drawing.Rectangle 0, 0, $s, $s))
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    [void]$pngBlobs.Add(@{ size = $s; bytes = $ms.ToArray() })
    $ms.Dispose()
    $bmp.Dispose()
}
$source.Dispose()

$fs = [System.IO.File]::Create($dst)
$bw = New-Object System.IO.BinaryWriter($fs)

$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$pngBlobs.Count)

$offset = 6 + 16 * $pngBlobs.Count
foreach ($e in $pngBlobs) {
    $dim = 0
    if ($e.size -lt 256) { $dim = $e.size }
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]0)
    $bw.Write([Byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$e.bytes.Length)
    $bw.Write([UInt32]$offset)
    $offset += $e.bytes.Length
}
foreach ($e in $pngBlobs) { $bw.Write($e.bytes) }

$bw.Flush(); $bw.Dispose(); $fs.Close()
Write-Output ("OK entries=" + $pngBlobs.Count + " bytes=" + (Get-Item $dst).Length)
