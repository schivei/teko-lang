# comp_teko_win.ps1 — link a usable teko.exe compiler from the committed bootstrap/teko.c on Windows.
#
# Usage:  scripts\comp_teko_win.ps1 [out_path]
#   out_path  where to write the binary (default: <repo>\teko.exe)
# Env:
#   CC            C compiler to use (default: clang)
#   CFLAGS_EXTRA  extra flags (space-separated) appended after the fixed ones
#
# On Windows, clang targets the MSVC ABI and needs the MSVC CRT + Windows SDK headers
# (<stdlib.h>, <windows.h>, ...). Those live in the environment only inside a "Developer
# PowerShell for VS". This script auto-enters that environment (via vswhere) when it is not
# already set up, so it works from a plain PowerShell too. MSYS2/MinGW clang carries its own
# headers and skips this step.
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$CC   = if ($env:CC) { $env:CC } else { "clang" }
$Out  = if ($args.Count -ge 1) { $args[0] } else { Join-Path $Root "teko.exe" }

$TekoC = Join-Path $Root "bootstrap\teko.c"
if (-not (Test-Path $TekoC)) { Write-Error "comp_teko_win: missing $TekoC"; exit 1 }

# Ensure the MSVC CRT / Windows SDK headers are reachable. If INCLUDE is unset (plain
# PowerShell), find the VS install with vswhere and enter its developer shell for x64.
if (-not $env:INCLUDE) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = (& $vswhere -latest -products * -property installationPath 2>$null | Select-Object -First 1)
        if ($vsPath) {
            $devShell = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
            if (Test-Path $devShell) {
                Import-Module $devShell
                Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
                    -DevCmdArguments "-arch=x64 -no_logo" | Out-Null
                Write-Host "comp_teko_win: entered VS dev shell at $vsPath"
            }
        }
    }
}
if (-not $env:INCLUDE) {
    Write-Warning "comp_teko_win: MSVC/Windows SDK headers not in environment. Run from 'Developer PowerShell for VS 2022', or install VS Build Tools with the 'Desktop development with C++' workload, or use MSYS2/MinGW clang (see COMPILE.md)."
}

$Ver = "0.0.0.0-dev"
$DeriveSh = Join-Path $Root "scripts\derive_version.sh"
if ((Get-Command sh -ErrorAction SilentlyContinue) -and (Test-Path $DeriveSh)) {
    try { $Ver = (& sh $DeriveSh).Trim().TrimStart("v") } catch { }
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
