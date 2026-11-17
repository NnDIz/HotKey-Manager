# hotkey_record.ps1 - zapis kombinacii klavish v terminale
$keys = New-Object System.Collections.ArrayList

Write-Host ""
Write-Host "  === Zapis kombinacii ===" -ForegroundColor Cyan
Write-Host "  Nazhimay klavishi." -ForegroundColor Gray
Write-Host "  [Enter]     - sohranit i vyyti" -ForegroundColor Gray
Write-Host "  [Esc]       - otmenit" -ForegroundColor Gray
Write-Host "  [Backspace] - udalit poslednyuyu" -ForegroundColor Gray
Write-Host ""

function Get-KeyName($key) {
    switch ($key) {
        'LeftCtrl'   { return 'Ctrl' }
        'RightCtrl'  { return 'Ctrl' }
        'LeftAlt'    { return 'Alt' }
        'RightAlt'   { return 'Alt' }
        'LeftShift'  { return 'Shift' }
        'RightShift' { return 'Shift' }
        'LWin'       { return 'Win' }
        'RWin'       { return 'Win' }
        'Spacebar'   { return 'Space' }
        'Return'     { return 'Enter' }
        'Escape'     { return 'Esc' }
        'Back'       { return 'Backspace' }
        'Delete'     { return 'Delete' }
        'Tab'        { return 'Tab' }
        'UpArrow'    { return 'Up' }
        'DownArrow'  { return 'Down' }
        'LeftArrow'  { return 'Left' }
        'RightArrow' { return 'Right' }
        'Home'       { return 'Home' }
        'End'        { return 'End' }
        'PageUp'     { return 'PageUp' }
        'PageDown'   { return 'PageDown' }
        'Insert'     { return 'Insert' }
        default {
            if ($key -match '^D(\d)$')       { return $Matches[1] }
            if ($key -match '^NumPad(\d)$')  { return "Num" + $Matches[1] }
            if ($key -match '^F\d+$')        { return $key }
            return $key
        }
    }
}

while ($true) {
    $ki = $host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    $vk = $ki.VirtualKeyCode

    if ($vk -eq 27) { Write-Host "  Otmena." -ForegroundColor Red; exit 1 }
    if ($vk -eq 13) {
        if ($keys.Count -eq 0) { Write-Host "  Pusto." -ForegroundColor Yellow; continue }
        break
    }
    if ($vk -eq 8) {
        if ($keys.Count -gt 0) {
            $keys.RemoveAt($keys.Count - 1)
            $c = if ($keys.Count) { $keys -join "+" } else { "(pusto)" }
            Write-Host "  -> $c" -ForegroundColor Yellow
        }
        continue
    }

    $name = Get-KeyName $ki.Key
    if ($name -match '^(Ctrl|Alt|Shift|Win)$') {
        if ($keys -notcontains $name) {
            $keys.Insert(0, $name)
            Write-Host "  -> $($keys -join '+')" -ForegroundColor Green
        }
    } else {
        if ($keys -notcontains $name) {
            $keys.Add($name) | Out-Null
            Write-Host "  -> $($keys -join '+')" -ForegroundColor Green
        }
    }
}

$final = $keys -join "+"
Write-Host ""
Write-Host "  KOMBINACIYA: $final" -ForegroundColor Cyan
return $final