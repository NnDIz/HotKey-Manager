# hotkey_cli.ps1 - upravlenie hotkeyami
$ConfigPath = Join-Path $env:USERPROFILE ".hotkey_manager.ini"

function Read-Config {
    if (-not (Test-Path $ConfigPath)) { return @{ hotkeys = @() } }
    $lines = Get-Content $ConfigPath -Encoding UTF8
    $hotkeys = @()
    $current = $null
    foreach ($line in $lines) {
        $line = $line.Trim()
        if ($line -match '^\[hk(\d+)\]') {
            if ($current) { $hotkeys += $current }
            $current = [PSCustomObject]@{ index=[int]$Matches[1]; name=""; combo=""; type=0; value=""; enabled=1 }
        }
        elseif ($line -match '^name=(.*)$'    -and $current) { $current.name    = $Matches[1] }
        elseif ($line -match '^combo=(.*)$'   -and $current) { $current.combo   = $Matches[1] }
        elseif ($line -match '^type=(\d+)$'   -and $current) { $current.type    = [int]$Matches[1] }
        elseif ($line -match '^value=(.*)$'   -and $current) { $current.value   = $Matches[1] }
        elseif ($line -match '^enabled=(\d)$' -and $current) { $current.enabled = [int]$Matches[1] }
    }
    if ($current) { $hotkeys += $current }
    return @{ hotkeys = $hotkeys }
}

function Write-Config($hotkeys) {
    $lines = @()
    $lines += "[hotkeys]"
    $lines += "count=$($hotkeys.Count)"
    $lines += ""
    for ($i = 0; $i -lt $hotkeys.Count; $i++) {
        $hk = $hotkeys[$i]
        $lines += "[hk$i]"
        $lines += "name=$($hk.name)"
        $lines += "combo=$($hk.combo)"
        $lines += "type=$($hk.type)"
        $lines += "value=$($hk.value)"
        $lines += "enabled=$($hk.enabled)"
        $lines += ""
    }
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllLines($ConfigPath, $lines, $utf8)
}

function Show-List {
    $cfg = Read-Config
    if ($cfg.hotkeys.Count -eq 0) {
        Write-Host "  Spisok pust. Dobav: hk add IMA KOMBO TIP ZNACH" -ForegroundColor Yellow
        return
    }
    Write-Host ""
    Write-Host "  #   Kombo                  Tip    Nazvanie / Znachenie" -ForegroundColor Cyan
    Write-Host "  -------------------------------------------------------------" -ForegroundColor DarkGray
    for ($i = 0; $i -lt $cfg.hotkeys.Count; $i++) {
        $hk = $cfg.hotkeys[$i]
        $tn = switch ($hk.type) { 0 {"URL "} 1 {"APP "} 2 {"CMD "} default {"?   "} }
        $combo = $hk.combo.PadRight(20)
        $name = if ($hk.name) { $hk.name } else { "(no name)" }
        $val = $hk.value
        if ($val.Length -gt 40) { $val = $val.Substring(0,37) + "..." }
        Write-Host ("  {0,-3} {1} {2} {3}" -f $i, $combo, $tn, $name) -ForegroundColor White
        Write-Host ("      -> $val") -ForegroundColor DarkGray
    }
    Write-Host ""
}

function Add-Hotkey($name, $combo, $typeStr, $value) {
    $type = switch ($typeStr.ToLower()) {
        "url" { 0 } "app" { 1 } "cmd" { 2 }
        default { Write-Host "Oshibka: tip = url | app | cmd" -ForegroundColor Red; return }
    }
    $cfg = Read-Config
    $hk = [PSCustomObject]@{ index=$cfg.hotkeys.Count; name=$name; combo=$combo; type=$type; value=$value; enabled=1 }
    $cfg.hotkeys += $hk
    Write-Config $cfg.hotkeys
    Write-Host "OK Dobavleno: $name -> $combo" -ForegroundColor Green
}

function Remove-Hotkey($index) {
    $cfg = Read-Config
    if ($index -lt 0 -or $index -ge $cfg.hotkeys.Count) {
        Write-Host "Oshibka: net indexa $index" -ForegroundColor Red; return
    }
    $name = $cfg.hotkeys[$index].name
    $newList = @()
    for ($i = 0; $i -lt $cfg.hotkeys.Count; $i++) { if ($i -ne $index) { $newList += $cfg.hotkeys[$i] } }
    Write-Config $newList
    Write-Host "OK Udalen: $name" -ForegroundColor Green
}

function Clear-All {
    $r = Read-Host "Udolit VSE? (y/n)"
    if ($r -eq 'y') { Write-Config @(); Write-Host "OK" -ForegroundColor Green }
}

function Restart-App {
    Get-Process HotKeyManager -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    $exe = Join-Path $PSScriptRoot "HotKeyManager.exe"
    if (Test-Path $exe) { Start-Process $exe; Write-Host "OK perezapushchen" -ForegroundColor Green }
    else { Write-Host "Oshibka: net HotKeyManager.exe" -ForegroundColor Red }
}

$cmd = $args[0]; $sub = $args[1]
if ($cmd -ne "hk") {
    Write-Host "  hk list / hk add IMA KOMBO TIP ZNACH / hk remove INDEX / hk clear / hk restart" -ForegroundColor Cyan
    return
}
switch ($sub) {
    "list"    { Show-List }
    "add"     { Add-Hotkey $args[2] $args[3] $args[4] $args[5] }
    "remove"  { Remove-Hotkey ([int]$args[2]) }
    "clear"   { Clear-All }
    "restart" { Restart-App }
    "path"    { Write-Host $ConfigPath -ForegroundColor Cyan }
}