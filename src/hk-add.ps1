# hk-add.ps1 - zapisat klavishi i srazu dobavit hotkey
# Ispolzovanie: .\hk-add.ps1 IMA TIP ZNACHENIE
# Primer:      .\hk-add.ps1 YouTube url https://youtube.com

param(
    [Parameter(Mandatory=$true)][string]$Name,
    [Parameter(Mandatory=$true)][string]$Type,
    [Parameter(Mandatory=$true)][string]$Value
)

$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rec = Join-Path $dir "hotkey_record.ps1"
$cli = Join-Path $dir "hotkey_cli.ps1"

Write-Host ""
Write-Host "  IMA:      $Name"  -ForegroundColor Cyan
Write-Host "  TIP:      $Type"  -ForegroundColor Cyan
Write-Host "  ZNACH:    $Value" -ForegroundColor Cyan
Write-Host ""

$combo = & $rec
if (-not $combo) { Write-Host "Otmeneno." -ForegroundColor Red; exit }

Write-Host ""
Write-Host "  Dobavlyayu: $Name -> $combo" -ForegroundColor Green
& $cli hk add $Name $combo $Type $Value
& $cli hk restart

Write-Host ""
Write-Host "  Gotovo! Nazhmi: $combo" -ForegroundColor Green