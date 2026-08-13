# KERS HUD - Speicher-/CPU-Sampler
# ================================
# Schreibt alle paar Sekunden den Verbrauch aller beteiligten Prozesse in eine CSV.
#
# Die entscheidende Frage, die das beantwortet:
#   * bleibt der Speicher KONSTANT hoch  -> Grundverbrauch (Compositing-Ebenen, Caches)
#   * WAECHST er ueber die Zeit          -> Leck; dann ist alles andere Nebensache
#
# Erfasst werden:
#   python.exe / pythonw.exe        -> Server (main.py) und HUD (kers_hud.py)
#   QtWebEngineProcess.exe          -> Chromium: Renderer, GPU, Utility (mehrere!)
#
# Aufruf (aus dem hud-Ordner):
#   powershell -ExecutionPolicy Bypass -File measure.ps1
#   powershell -ExecutionPolicy Bypass -File measure.ps1 -Seconds 300 -Interval 2 -Label opti-30hz
#
# Fuer einen fairen Vergleich immer dieselbe Last fahren - am besten ein .f1rec-Replay
# ueber testgui.py, nicht ein echtes Rennen.

param(
    [int]$Seconds = 300,          # Gesamtdauer
    [int]$Interval = 2,           # Abstand zwischen zwei Messungen
    [string]$Label = "messung"    # landet im Dateinamen
)

$ErrorActionPreference = "Stop"

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$csv = Join-Path $PSScriptRoot "perf_$Label`_$stamp.csv"

# CPU-Prozent muss selbst gerechnet werden: die CPU-Eigenschaft eines Prozesses ist
# die VERBRAUCHTE Gesamtzeit seit Start, kein Momentanwert. Also Differenz zweier
# Messungen durch verstrichene Zeit und durch die Kernanzahl.
$cores = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
$prevCpu = @{}
$prevTime = $null

"zeit;sekunde;gruppe;prozesse;ram_mb;cpu_prozent" | Out-File -FilePath $csv -Encoding utf8

Write-Host "Messe $Seconds s alle $Interval s -> $csv"
Write-Host "(mit Strg+C vorzeitig abbrechen, die CSV bleibt verwertbar)"
Write-Host ""

$groups = @{
    "server_hud" = { Get-Process -Name python, pythonw -ErrorAction SilentlyContinue }
    "chromium"   = { Get-Process -Name QtWebEngineProcess -ErrorAction SilentlyContinue }
}

$end = (Get-Date).AddSeconds($Seconds)
$t0 = Get-Date

while ((Get-Date) -lt $end) {
    $now = Get-Date
    $elapsed = if ($prevTime) { ($now - $prevTime).TotalSeconds } else { 0 }
    $line = @()

    foreach ($name in $groups.Keys) {
        $procs = & $groups[$name]
        if (-not $procs) {
            $ramMb = 0; $cpuPct = 0; $count = 0
        } else {
            $count = @($procs).Count
            $ramMb = [math]::Round((($procs | Measure-Object WorkingSet64 -Sum).Sum) / 1MB, 1)
            $cpuSec = ($procs | Measure-Object TotalProcessorTime -Sum -ErrorAction SilentlyContinue).Sum
            if ($null -eq $cpuSec) { $cpuSec = 0 } else { $cpuSec = $cpuSec }
            $cpuNow = 0
            foreach ($p in $procs) {
                try { $cpuNow += $p.TotalProcessorTime.TotalSeconds } catch { }
            }
            if ($elapsed -gt 0 -and $prevCpu.ContainsKey($name)) {
                $cpuPct = [math]::Round((($cpuNow - $prevCpu[$name]) / $elapsed / $cores) * 100, 1)
                if ($cpuPct -lt 0) { $cpuPct = 0 }   # Prozess neu gestartet
            } else {
                $cpuPct = 0
            }
            $prevCpu[$name] = $cpuNow
        }

        $sec = [math]::Round(($now - $t0).TotalSeconds, 0)
        $line += "{0};{1};{2};{3};{4};{5}" -f `
            $now.ToString("HH:mm:ss"), $sec, $name, $count, $ramMb, $cpuPct
        Write-Host ("{0,-6}s  {1,-11} {2,2} Prozesse  {3,8} MB  {4,5} % CPU" -f $sec, $name, $count, $ramMb, $cpuPct)
    }

    $line | Out-File -FilePath $csv -Encoding utf8 -Append
    $prevTime = $now
    Start-Sleep -Seconds $Interval
}

Write-Host ""
Write-Host "Fertig. CSV: $csv"

# Kurzauswertung: erster und letzter Wert je Gruppe - waechst der Speicher?
$rows = Import-Csv -Path $csv -Delimiter ";"
foreach ($g in ($rows | Select-Object -ExpandProperty gruppe -Unique)) {
    $sub = @($rows | Where-Object { $_.gruppe -eq $g })
    if ($sub.Count -lt 2) { continue }
    $first = [double]$sub[0].ram_mb
    $last  = [double]$sub[-1].ram_mb
    $peak  = ($sub | Measure-Object -Property ram_mb -Maximum).Maximum
    $cpu   = [math]::Round((($sub | Measure-Object -Property cpu_prozent -Average).Average), 1)
    $delta = [math]::Round($last - $first, 1)
    $trend = if ($delta -gt 100) { "WAECHST -> Verdacht auf Leck" } else { "stabil" }
    Write-Host ("{0,-11} Start {1,8} MB  Ende {2,8} MB  Spitze {3,8} MB  ({4:+#;-#;0} MB, {5})  CPU im Mittel {6} %" `
        -f $g, $first, $last, $peak, $delta, $trend, $cpu)
}
