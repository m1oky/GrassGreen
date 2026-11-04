Add-Type -AssemblyName System.Drawing

function Create-GrassIco([string]$outPath) {
    $bmp = New-Object System.Drawing.Bitmap 128, 128
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.Clear([System.Drawing.Color]::Transparent)

    $c1 = [System.Drawing.ColorTranslator]::FromHtml('#00b359')
    $c2 = [System.Drawing.ColorTranslator]::FromHtml('#00ffcc')
    $cBg = [System.Drawing.ColorTranslator]::FromHtml('#0d1117')
    $cStem = [System.Drawing.ColorTranslator]::FromHtml('#00ff88')

    $p1 = New-Object System.Drawing.Point 0, 128
    $p2 = New-Object System.Drawing.Point 128, 0
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush $p1, $p2, $c1, $c2
    
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = 28
    $d = $r * 2
    $p.AddArc(8, 8, $d, $d, 180, 90)
    $p.AddArc(8 + 112 - $d, 8, $d, $d, 270, 90)
    $p.AddArc(8 + 112 - $d, 8 + 112 - $d, $d, $d, 0, 90)
    $p.AddArc(8, 8 + 112 - $d, $d, $d, 90, 90)
    $p.CloseFigure()

    $brushBg = New-Object System.Drawing.SolidBrush $cBg
    $g.FillPath($brushBg, $p)

    $penBorder = New-Object System.Drawing.Pen $grad, 6
    $g.DrawPath($penBorder, $p)

    $penCurve = New-Object System.Drawing.Pen $grad, 10
    $penCurve.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $penCurve.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $g.DrawBezier($penCurve, 40, 88, 40, 52, 68, 36, 88, 28)
    $g.DrawBezier($penCurve, 88, 28, 76, 54, 72, 72, 74, 92)

    $penBranch = New-Object System.Drawing.Pen (New-Object System.Drawing.SolidBrush $cStem), 8
    $penBranch.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $penBranch.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $g.DrawBezier($penBranch, 36, 92, 52, 72, 72, 64, 92, 60)

    $brushDot = New-Object System.Drawing.SolidBrush $c2
    $g.FillEllipse($brushDot, 82, 22, 12, 12)

    $hIcon = $bmp.GetHicon()
    $icon = [System.Drawing.Icon]::FromHandle($hIcon)
    $fs = New-Object System.IO.FileStream $outPath, ([System.IO.FileMode]::Create)
    $icon.Save($fs)
    $fs.Close()
    $g.Dispose()
    $bmp.Dispose()
    Write-Host "Generated: $outPath"
}

function Create-GdbIco([string]$outPath) {
    $bmp = New-Object System.Drawing.Bitmap 128, 128
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $c1 = [System.Drawing.ColorTranslator]::FromHtml('#00ffcc')
    $c2 = [System.Drawing.ColorTranslator]::FromHtml('#0088ff')
    $cBg = [System.Drawing.ColorTranslator]::FromHtml('#0d1117')
    $cCard = [System.Drawing.ColorTranslator]::FromHtml('#161b22')

    $p1 = New-Object System.Drawing.Point 0, 0
    $p2 = New-Object System.Drawing.Point 128, 128
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush $p1, $p2, $c1, $c2

    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = 28
    $d = $r * 2
    $p.AddArc(8, 8, $d, $d, 180, 90)
    $p.AddArc(8 + 112 - $d, 8, $d, $d, 270, 90)
    $p.AddArc(8 + 112 - $d, 8 + 112 - $d, $d, $d, 0, 90)
    $p.AddArc(8, 8 + 112 - $d, $d, $d, 90, 90)
    $p.CloseFigure()

    $brushBg = New-Object System.Drawing.SolidBrush $cBg
    $g.FillPath($brushBg, $p)

    $penBorder = New-Object System.Drawing.Pen $grad, 6
    $g.DrawPath($penBorder, $p)

    $penCyl = New-Object System.Drawing.Pen $grad, 5
    $brushCyl = New-Object System.Drawing.SolidBrush $cCard
    $g.FillEllipse($brushCyl, 30, 24, 68, 24)
    $g.DrawEllipse($penCyl, 30, 24, 68, 24)

    $g.DrawArc($penCyl, 30, 52, 68, 24, 0, 180)
    $g.DrawLine($penCyl, 30, 36, 30, 64)
    $g.DrawLine($penCyl, 98, 36, 98, 64)

    $g.DrawArc($penCyl, 30, 80, 68, 24, 0, 180)
    $g.DrawLine($penCyl, 30, 64, 30, 92)
    $g.DrawLine($penCyl, 98, 64, 98, 92)

    $brushDot1 = New-Object System.Drawing.SolidBrush $c1
    $brushDot2 = New-Object System.Drawing.SolidBrush $c2
    $g.FillEllipse($brushDot1, 76, 52, 8, 8)
    $g.FillEllipse($brushDot2, 76, 80, 8, 8)

    $hIcon = $bmp.GetHicon()
    $icon = [System.Drawing.Icon]::FromHandle($hIcon)
    $fs = New-Object System.IO.FileStream $outPath, ([System.IO.FileMode]::Create)
    $icon.Save($fs)
    $fs.Close()
    $g.Dispose()
    $bmp.Dispose()
    Write-Host "Generated: $outPath"
}

Create-GrassIco "installer/grass.ico"
Create-GdbIco "installer/gdb.ico"
