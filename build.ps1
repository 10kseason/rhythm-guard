param([string]$OutputDirectory = (Join-Path $PSScriptRoot 'build'))
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
Push-Location (Join-Path $PSScriptRoot 'src')
try {
    & windres --codepage=65001 app.rc -O coff -o (Join-Path $OutputDirectory 'resources.o')
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -municode -mwindows `
        -static -static-libgcc -static-libstdc++ -finput-charset=UTF-8 `
        main.cpp (Join-Path $OutputDirectory 'resources.o') `
        '-Wl,--no-insert-timestamp' -s -o (Join-Path $OutputDirectory 'RhythmGuard.exe') `
        -luser32 -lgdi32 -lshell32 -ldwmapi
    if ($LASTEXITCODE -ne 0) { throw 'Application compilation failed.' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static `
        ..\tests\filter_tests.cpp -o (Join-Path $OutputDirectory 'filter_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
    & (Join-Path $OutputDirectory 'filter_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Key sequence tests failed.' }
} finally { Pop-Location }
