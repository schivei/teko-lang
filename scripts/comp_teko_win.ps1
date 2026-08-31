# comp_teko_win.ps1 — link a usable teko.exe compiler from the committed bootstrap/teko.c on Windows.
#
# Usage:  scripts\comp_teko_win.ps1 [out_path]
#   out_path  where to write the binary (default: <repo>\teko.exe)
# Env:
#   CC            C compiler to use (default: clang)
#   CFLAGS_EXTRA  extra flags (space-separated) appended after the fixed ones
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$CC   = if ($env:CC) { $env:CC } else { "clang" }
$Out  = if ($args.Count -ge 1) { $args[0] } else { Join-Path $Root "teko.exe" }

$TekoC = Join-Path $Root "bootstrap\teko.c"
if (-not (Test-Path $TekoC)) { Write-Error "comp_teko_win: missing $TekoC"; exit 1 }

$Ver = "0.0.0.0-dev"
$DeriveSh = Join-Path $Root "scripts\derive_version.sh"
if (Get-Command sh -ErrorAction SilentlyContinue) {
    if (Test-Path $DeriveSh) {
        try { $Ver = (& sh $DeriveSh).Trim().TrimStart("v") } catch { }
    }
}

$Extra = @()
if ($env:CFLAGS_EXTRA) { $Extra = $env:CFLAGS_EXTRA.Split(" ", [StringSplitOptions]::RemoveEmptyEntries) }

Write-Host "comp_teko_win: CC=$CC -> $Out (version $Ver)"
try { & $CC --version 2>$null | Select-Object -First 1 } catch { }

# No -pthread on Windows: teko_rt.c uses CreateThread there (guarded by #ifdef _WIN32).
& $CC -std=c2x -w -O2 "-DTEKO_VERSION_STRING=$Ver" @Extra `
    "-I$Root\src\runtime" "-I$Root\src\assert" `
    $TekoC "$Root\src\runtime\teko_rt.c" "$Root\src\assert\assert.c" `
    -o $Out
if ($LASTEXITCODE -ne 0) { Write-Error "comp_teko_win: link failed ($LASTEXITCODE)"; exit $LASTEXITCODE }

if (-not (Test-Path $Out)) { Write-Error "comp_teko_win: link reported success but $Out is missing"; exit 1 }
Write-Host "comp_teko_win: wrote $Out"
