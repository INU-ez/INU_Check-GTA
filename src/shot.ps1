# shot.ps1 <game folder> <theme 0..3> <model row> <out.png>  — hidden run, screenshot of the window in the given theme
param([string]$game, [int]$theme, [string]$row, [string]$out, [int]$scale = 100, [int]$hue = 0)
$d = "F:\GitHub\INU_Check-GTA\bin\win-amd64-d3d9\Release"
$ini = "$d\gta_check.ini"
$bak = if (Test-Path $ini) { Get-Content $ini -Raw } else { $null }
@("lang=ru", "theme=$theme", "right=1", "rightw=560", "info=0", "hidevanilla=1", "crt=1", "scale=$scale", "termhue=$hue") | Set-Content $ini -Encoding ASCII
$ppm = "$d\gta_check_shot.ppm"
if (Test-Path $ppm) { Remove-Item $ppm }
$env:GTACHECK_HIDDEN = "1"; $env:GTACHECK_MVTEST = $row; $env:GTACHECK_SHOT = $ppm
$p = Start-Process -PassThru -WorkingDirectory $d -FilePath "$d\gta_check.exe" -ArgumentList @("`"$game`"")
$p.WaitForExit(180000) | Out-Null
if (-not $p.HasExited) { $p.Kill(); "TIMEOUT" } else { "exit " + $p.ExitCode }
$env:GTACHECK_HIDDEN = $null; $env:GTACHECK_MVTEST = $null; $env:GTACHECK_SHOT = $null
if ($bak -ne $null) { Set-Content $ini -Value $bak -NoNewline } else { Remove-Item $ini }
python -c "from PIL import Image; im = Image.open(r'$ppm'); im.save(r'$out'); print(im.size)"
