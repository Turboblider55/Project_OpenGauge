# Ketiranyu soros konzol az ECU-szimulatorhoz (es barmely ESP-IDF konzolhoz): kiirja a naplot, es elkuldi a beirt sorokat.
#
# Mit csinal: megnyitja a megadott COM-portot, a vett szoveget a kepernyore irja, a begepelt sort Enterre elkuldi
#             az eszkoznek (pl. "drive on", "key off", "status", "help"). Kilepes: Ctrl+C.
# Hogyan:     System.IO.Ports.SerialPort, a DTR es az RTS KIKAPCSOLVA. Az ESP32-S2 USB-konzolja (ROM CDC) a DTR/RTS
#             allapotvaltasokat ujrainditas / letoltesi mod jelnek ertelmezheti, a legtobb terminal (PuTTY is) pedig
#             nyitaskor bekapcsolja a DTR-t, ezert nem mukodik vele.
# Hasznalat:  .\sim_console.ps1                       (interaktiv, COM4)
#             .\sim_console.ps1 -Port COM4 -Send "drive on","status"    (egyszeri: elkuldi a sorokat, kiirja a valaszt, kilep)
#             A portot elobb szabadda kell tenni (pl. az idf.py monitor vagy mas terminal ne tartsa nyitva).
# Fontos:     a port megnyitasa ujrainditja az ESP32-S2-t (a USB-CDC ujra felcsatlakozik), ezert az egyszeri modban a script
#             -StartWaitMs ideig (alap: 3500 ms) var, mielott az elso sort elkuldi; az interaktiv modban is varj 3-4 masodpercet.
# Megjegyzes: a fajl szandekosan ekezet nelkuli (a Windows PowerShell 5.1 BOM nelkul ANSI-nak olvasna az UTF-8 fajlt).
param(
    [string]$Port = "COM4",
    [int]$Baud = 115200,
    [string[]]$Send,
    [int]$WaitMs = 1500,
    [int]$StartWaitMs = 3500
)

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$p = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$p.DtrEnable = $false
$p.RtsEnable = $false
$p.Encoding = New-Object System.Text.UTF8Encoding($false)   # BOM nelkul: a BOM az elso kuldott sor elejere kerulne
$p.ReadTimeout = 200
try {
    $p.Open()
} catch {
    Write-Host "A port ($Port) nem nyithato meg: $($_.Exception.Message)"
    Write-Host "Ellenorizd, hogy nem tartja-e nyitva mas program (idf.py monitor, PuTTY, VS Code), es hogy a COM-szam jo-e."
    exit 1
}

function Read-Available {
    $t = ""
    try { $t = $p.ReadExisting() } catch { }
    if ($t) { Write-Host -NoNewline $t }
}

try {
    if ($Send) {
        Start-Sleep -Milliseconds $StartWaitMs   # a port megnyitasa ujrainditja az eszkozt
        Read-Available
        foreach ($line in $Send) {
            $p.Write($line + "`n")
            Start-Sleep -Milliseconds $WaitMs
            Read-Available
        }
        exit 0
    }
    Write-Host "Csatlakozva: $Port, $Baud baud. Gepelj parancsot (pl. help), Enter kuldi. Kilepes: Ctrl+C."
    $buf = ""
    while ($true) {
        Read-Available
        while ([Console]::KeyAvailable) {
            $k = [Console]::ReadKey($true)
            if ($k.Key -eq 'Enter') {
                $p.Write($buf + "`n")
                Write-Host ""
                $buf = ""
            } elseif ($k.Key -eq 'Backspace') {
                if ($buf.Length -gt 0) { $buf = $buf.Substring(0, $buf.Length - 1); Write-Host -NoNewline "`b `b" }
            } elseif ($k.KeyChar -ne [char]0) {
                $buf += $k.KeyChar
                Write-Host -NoNewline $k.KeyChar
            }
        }
        Start-Sleep -Milliseconds 50
    }
} finally {
    if ($p.IsOpen) { $p.Close() }
}
