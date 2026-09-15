#Requires -Version 5.1
<#
.SYNOPSIS
    Clones the openFrameworks addons patternForge needs, at the commits it
    is known to build against, into this openFrameworks tree's addons/.

.DESCRIPTION
    There is only one addon -- but it is NOT the upstream one, which is
    the whole reason this script exists. patternForge builds against a
    fork of ofxImGui whose HEAD is one commit ahead of jvcleave/master,
    and that commit does not exist upstream. Clone upstream and it does
    not compile. So the URL and the commit are both pinned here.

    Safe to re-run: an addon already present and on the right commit is
    left alone.

.PARAMETER Update
    Also move an existing clone onto the pinned commit. Without it, a
    clone sitting on the wrong commit is reported and left untouched.

.EXAMPLE
    .\scripts\install-addons.ps1
.EXAMPLE
    .\scripts\install-addons.ps1 -Update
#>

[CmdletBinding()]
param([switch]$Update)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# --- what we need ----------------------------------------------------
#
# HTTPS rather than SSH on purpose: this has to work on a machine with
# no keys loaded, and the fork is public.
$Addons = @(
    [pscustomobject]@{
        Name   = 'ofxImGui'
        Url    = 'https://github.com/danzeeeman/ofxImGui.git'
        Commit = '3ddb3519ba5687e3f45257330e36d73fecef80de'
    }
)

# openFrameworks release this tree is expected to be.
$OfWantMajor = 0
$OfWantMinor = 12

function Write-Bold { param([string]$m) Write-Host $m -ForegroundColor White }
function Write-Warn { param([string]$m) Write-Host $m -ForegroundColor Yellow }
function Stop-Bad   { param([string]$m) Write-Host $m -ForegroundColor Red; exit 1 }

# Native commands do not throw on failure in PowerShell, so every git
# call has to check its exit code explicitly or a failed clone reads as
# success.
function Invoke-Git {
    param([string[]]$Arguments, [switch]$AllowFail)
    $out = & git @Arguments 2>&1
    if ($LASTEXITCODE -ne 0 -and -not $AllowFail) {
        Stop-Bad "git $($Arguments -join ' ') failed:`n$out"
    }
    return @{ Ok = ($LASTEXITCODE -eq 0); Output = ($out | Out-String).Trim() }
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Stop-Bad 'git is not installed, or is not on PATH.'
}

# --- find the openFrameworks root ------------------------------------
#
# config.make holds OF_ROOT relative to the project, which is the same
# answer the Makefile uses -- so read that rather than guessing a depth.
$here      = Split-Path -Parent $PSScriptRoot
$configMak = Join-Path $here 'config.make'
$ofRel     = '../../..'
if (Test-Path -LiteralPath $configMak) {
    $m = Select-String -LiteralPath $configMak -Pattern '^\s*OF_ROOT\s*=\s*(.+)$' |
         Select-Object -Last 1
    if ($m) { $ofRel = $m.Matches[0].Groups[1].Value.Trim() }
}

$ofRoot = $null
$candidate = Join-Path $here $ofRel
if (Test-Path -LiteralPath $candidate) {
    $ofRoot = (Resolve-Path -LiteralPath $candidate).Path
}

if (-not $ofRoot -or -not (Test-Path -LiteralPath (Join-Path $ofRoot 'libs/openFrameworks'))) {
    Stop-Bad @"
Could not find an openFrameworks tree at '$candidate'.
This project has to live in <openFrameworks>/apps/<group>/patternForge,
or config.make's OF_ROOT has to point at one.
"@
}

Write-Bold "openFrameworks: $ofRoot"

# Version check: a warning, not a failure. It may well build on a
# neighbouring release, and refusing to clone an addon over it would be
# unhelpful.
$ofc = Join-Path $ofRoot 'libs/openFrameworks/utils/ofConstants.h'
if (Test-Path -LiteralPath $ofc) {
    $txt = Get-Content -LiteralPath $ofc -Raw
    $maj = ([regex]'#define\s+OF_VERSION_MAJOR\s+(\d+)').Match($txt).Groups[1].Value
    $min = ([regex]'#define\s+OF_VERSION_MINOR\s+(\d+)').Match($txt).Groups[1].Value
    $pat = ([regex]'#define\s+OF_VERSION_PATCH\s+(\d+)').Match($txt).Groups[1].Value
    if ($maj -ne '') {
        if ($pat -eq '') { $pat = '0' }
        Write-Host "  version $maj.$min.$pat"
        if ($maj -ne "$OfWantMajor" -or $min -ne "$OfWantMinor") {
            Write-Warn "  note: built and tested against $OfWantMajor.$OfWantMinor.x"
        }
    }
}

$addonsDir = Join-Path $ofRoot 'addons'
New-Item -ItemType Directory -Force -Path $addonsDir | Out-Null

# --- clone / verify each addon ---------------------------------------
$failed = $false
foreach ($a in $Addons) {
    $dir   = Join-Path $addonsDir $a.Name
    $short = $a.Commit.Substring(0, 8)
    Write-Host ''
    Write-Bold $a.Name

    if (-not (Test-Path -LiteralPath $dir)) {
        Write-Host "  cloning $($a.Url)"
        Invoke-Git @('clone', '--quiet', $a.Url, $dir) | Out-Null
        Invoke-Git @('-C', $dir, 'checkout', '--quiet', $a.Commit) | Out-Null
        $at = (Invoke-Git @('-C', $dir, 'rev-parse', '--short', 'HEAD')).Output
        Write-Host "  at $at"
    }
    elseif (-not (Test-Path -LiteralPath (Join-Path $dir '.git'))) {
        Write-Warn "  $dir exists but is not a git clone -- leaving it alone."
        Write-Warn "  Expected $($a.Url) @ $short. Remove it and re-run to replace it."
    }
    else {
        $have = (Invoke-Git @('-C', $dir, 'rev-parse', 'HEAD')).Output
        if ($have -eq $a.Commit) {
            Write-Host "  already at the pinned commit $short"
        }
        elseif ($Update) {
            Write-Host "  fetching $($a.Url)"
            $f = Invoke-Git @('-C', $dir, 'fetch', '--quiet', $a.Url, $a.Commit) -AllowFail
            if (-not $f.Ok) { Invoke-Git @('-C', $dir, 'fetch', '--quiet', '--all') | Out-Null }

            $exists = Invoke-Git @('-C', $dir, 'cat-file', '-e', "$($a.Commit)^{commit}") -AllowFail
            if ($exists.Ok) {
                $dirty = (Invoke-Git @('-C', $dir, 'status', '--porcelain')).Output
                if ($dirty) {
                    Write-Warn '  has local changes -- not checking out. Commit or stash them first.'
                    $failed = $true
                }
                else {
                    Invoke-Git @('-C', $dir, 'checkout', '--quiet', $a.Commit) | Out-Null
                    Write-Host "  moved to $short"
                }
            }
            else {
                Write-Warn "  could not fetch $($a.Commit)"
                $failed = $true
            }
        }
        else {
            Write-Warn "  at $($have.Substring(0,8)), pinned is $short"
            Write-Warn '  re-run with -Update to move it (this project may not build otherwise)'
        }
    }
}

# --- check the addons the project asks for are all covered -----------
#
# addons.make is what the build actually reads. If it grows an entry this
# script does not know about, say so rather than let the build fail later
# with a missing header.
Write-Host ''
$addonsMak = Join-Path $here 'addons.make'
if (Test-Path -LiteralPath $addonsMak) {
    foreach ($line in Get-Content -LiteralPath $addonsMak) {
        $want = $line.Trim()
        if ($want -eq '' -or $want.StartsWith('#')) { continue }
        if (-not (Test-Path -LiteralPath (Join-Path $addonsDir $want))) {
            Write-Warn "addons.make asks for '$want' and it is not installed, and this"
            Write-Warn 'script does not know where to get it. Add it to $Addons above.'
            $failed = $true
        }
    }
}

# --- prove it is usable ----------------------------------------------
$imguiH   = Join-Path $addonsDir 'ofxImGui/src/ofxImGui.h'
$imguiCpp = Join-Path $addonsDir 'ofxImGui/libs/imgui/src/imgui.cpp'
if ((Test-Path -LiteralPath $imguiH) -and (Test-Path -LiteralPath $imguiCpp)) {
    Write-Host 'ofxImGui headers and vendored imgui sources present.'
}
else {
    Write-Warn 'ofxImGui looks incomplete: expected src/ofxImGui.h and'
    Write-Warn 'libs/imgui/src/imgui.cpp. imgui is vendored in-tree, not a'
    Write-Warn 'submodule, so a shallow or partial clone will miss it.'
    $failed = $true
}

Write-Host ''
if ($failed) {
    Stop-Bad 'Finished with problems -- see above.'
}
Write-Bold 'Done. Build with:  make -j8      (msys2 on Windows, or Visual Studio)'
exit 0
