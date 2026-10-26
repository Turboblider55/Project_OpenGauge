# Soros naplózó hosszú mérésekhez (pl. ESP-NOW tartósmérés, autós mérés).
#
# Mit csinál: több soros portot olvas egyszerre, minden sort a PC időbélyegével azonnal fájlba ír
#             (AutoFlush), és ha egy port eltűnik (pl. kihúzott kábel), 2 s-onként újranyitja.
# Miért:      egy megszakadt mérés adatai így sem vesznek el; az eszközök halmozott számlálói
#             (vett / elveszett csomag) miatt a félbemaradt napló is kiértékelhető.
# Használat:  powershell -ExecutionPolicy Bypass -File serial_logger.ps1 -OutDir meresek/nev -Minutes 120 COM5=c3_obd COM6=s3_dash
# Megjegyzés: a fájl UTF-8 BOM-mal mentendő, különben a Windows PowerShell 5.1 a magyar szövegeket
#             ANSI-ként olvassa, és a jelölősorok hibás kódolással kerülnek a naplóba.
param(
    [Parameter(Mandatory)] [string] $OutDir,
    [Parameter(Mandatory)] [int] $Minutes,
    [Parameter(Mandatory, ValueFromRemainingArguments)] [string[]] $Ports
)

New-Item -ItemType Directory -Force $OutDir | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding($false)
$ansi = [regex]'\x1b\[[0-9;]*m'
$state = @{}
foreach ($pair in $Ports) {
    $name, $label = $pair -split '=', 2
    $w = New-Object System.IO.StreamWriter((Join-Path $OutDir "$label.txt"), $true, $utf8)
    $w.AutoFlush = $true
    $state[$name] = @{ label = $label; writer = $w; port = $null; buf = ''; next_try = [datetime]::MinValue }
}

function Stamp { (Get-Date).ToString('yyyy-MM-dd HH:mm:ss.fff') }

$end = (Get-Date).AddMinutes($Minutes)
foreach ($k in $state.Keys) { $state[$k].writer.WriteLine("[$(Stamp)] === Naplózás indul: $k, $Minutes perc ===") }

while ((Get-Date) -lt $end) {
    foreach ($k in @($state.Keys)) {
        $s = $state[$k]
        if ($null -eq $s.port) {
            if ((Get-Date) -lt $s.next_try) { continue }
            try {
                $p = New-Object System.IO.Ports.SerialPort $k, 115200
                $p.DtrEnable = $false; $p.RtsEnable = $false; $p.Encoding = $utf8
                $p.Open()
                $s.port = $p
                $s.writer.WriteLine("[$(Stamp)] === Port megnyitva ===")
            } catch {
                $s.writer.WriteLine("[$(Stamp)] === Port nem nyitható: $($_.Exception.Message) ===")
                $s.next_try = (Get-Date).AddSeconds(2)
            }
            continue
        }
        try {
            $s.buf += $s.port.ReadExisting()
        } catch {
            $s.writer.WriteLine("[$(Stamp)] === Port hiba, újranyitás: $($_.Exception.Message) ===")
            try { $s.port.Close() } catch {}
            $s.port = $null
            $s.next_try = (Get-Date).AddSeconds(2)
            continue
        }
        $lines = $s.buf -split "`n"
        $s.buf = $lines[-1]
        for ($i = 0; $i -lt $lines.Count - 1; $i++) {
            $t = $ansi.Replace($lines[$i], '').TrimEnd("`r")
            if ($t.Length) { $s.writer.WriteLine("[$(Stamp)] $t") }
        }
    }
    Start-Sleep -Milliseconds 50
}

foreach ($k in $state.Keys) {
    $s = $state[$k]
    $s.writer.WriteLine("[$(Stamp)] === Naplózás vége ===")
    if ($s.port) { $s.port.Close() }
    $s.writer.Close()
}
