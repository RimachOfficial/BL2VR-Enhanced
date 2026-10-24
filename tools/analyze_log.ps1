# analyze_log.ps1 - turn an openvr_proxy.log into the numbers behind the docs.
#
#   powershell -ExecutionPolicy Bypass -File tools\analyze_log.ps1 [-Path <log>]
#
# Default path: the log next to the game DLL. Reports:
#   * how many distinct threads called ovr_GetTrackingState
#   * t-backwards rate   (samples whose absTime went BACKWARDS on the same filter state -
#                         the metric that exposed the mixed-timeline bug; should be ~0)
#   * dt distribution    (should average ~7-14 ms per state, never stuck at the 1e-4 floor)
#   * jitter meter per filter profile (raw vs filtered deg/call, and the smoother-x)
#   * trigger-gate flips
param(
    [string]$Path = 'C:\Program Files (x86)\Steam\steamapps\common\Borderlands 2 VR\Binaries\Win64\openvr_proxy.log'
)
if (!(Test-Path $Path)) { Write-Error "log not found: $Path"; exit 1 }
$lines = Get-Content $Path
Write-Host "log: $Path"
Write-Host "     $((Get-Item $Path).Length) bytes, written $((Get-Item $Path).LastWriteTime)"
Write-Host ""

# ---- threads -----------------------------------------------------------------
$threads = @{}
foreach ($l in $lines) {
    if ($l -match 'polling thread seen: tid=(\d+)') { $threads[$Matches[1]] = $true }
    elseif ($l -match 'tid=(\d+)') { $threads[$Matches[1]] = $true }
}
Write-Host "distinct polling threads: $($threads.Count)  ($(($threads.Keys | Sort-Object) -join ', '))"

# ---- dt ----------------------------------------------------------------------
$dt = [System.Collections.Generic.List[double]]::new()
foreach ($l in $lines) { foreach ($m in [regex]::Matches($l, 'dt=([0-9.]+)')) { $dt.Add([double]$m.Groups[1].Value) } }
if ($dt.Count) {
    $s = $dt | Measure-Object -Minimum -Maximum -Average
    Write-Host ("dt: n={0} min={1:N5} avg={2:N5} max={3:N4}  (avg ~= 1/call-rate; {4:N1} Hz per state)" -f `
        $dt.Count, $s.Minimum, $s.Average, $s.Maximum, (1.0 / $s.Average))
}

# ---- jitter meter, tagged with the profile in force --------------------------
$prof = $null; $rows = @()
foreach ($l in $lines) {
    if ($l -match 'prof=([0-9.]+/[0-9.]+/[0-9.]+/[0-9.]+)') { $prof = $Matches[1] }
    elseif ($l -match 'jitter R-rot: raw ([0-9.]+) deg/call -> filtered ([0-9.]+) deg/call\s+\(([0-9.]+)x smoother, (\d+) samples\)') {
        # capture FIRST: a second -match on the same line would overwrite $Matches
        $jRaw = [double]$Matches[1]; $jFilt = [double]$Matches[2]
        $jX = [double]$Matches[3];   $jN = [int]$Matches[4]
        $back = $null; $tot = 0
        if ($l -match 't-backwards=(\d+)/(\d+)') { $back = [int]$Matches[1]; $tot = [int]$Matches[2] }
        $rows += [pscustomobject]@{ prof = $prof; raw = $jRaw; filt = $jFilt;
                                    x = $jX; n = $jN; back = $back; tot = $tot }
    }
}
if ($rows.Count) {
    $tb = ($rows | Where-Object { $_.back -ne $null } | Measure-Object back -Sum).Sum
    $tt = ($rows | Where-Object { $_.tot } | Measure-Object tot -Sum).Sum
    if ($tt) { Write-Host ("t-backwards: {0}/{1} samples = {2:N2}%   (was 44% before per-thread state; ~0 is correct)" -f $tb, $tt, (100.0 * $tb / $tt)) }
    Write-Host ""
    Write-Host "jitter meter per profile (prof = pos-cutoff / pos-beta / rot-cutoff / rot-beta):"
    Write-Host ("{0,-24} {1,8} {2,9} {3,11} {4,9} {5,9}" -f 'prof', 'reports', 'samples', 'smoother-x', 'raw d/c', 'filt d/c')
    foreach ($g in ($rows | Group-Object prof | Sort-Object { -($_.Group | Measure-Object n -Sum).Sum })) {
        $gg = $g.Group
        Write-Host ("{0,-24} {1,8} {2,9} {3,11:N2} {4,9:N3} {5,9:N3}" -f $g.Name, $gg.Count,
            ($gg | Measure-Object n -Sum).Sum, ($gg | Measure-Object x -Average).Average,
            ($gg | Measure-Object raw -Average).Average, ($gg | Measure-Object filt -Average).Average)
    }
}

# ---- gate --------------------------------------------------------------------
$flips = ($lines | Select-String -Pattern 'trigger gate ->' | Measure-Object).Count
Write-Host ""
Write-Host "trigger-gate flips: $flips"