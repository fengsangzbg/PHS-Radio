param([string]$OutputDirectory = (Join-Path $PSScriptRoot '..\resources'))

$ErrorActionPreference = 'Stop'
$logoWorkspace = Split-Path -Parent $PSScriptRoot
$logoBuildDirectory = Join-Path $logoWorkspace 'build-release-bin'
$logoOutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$logoExecutable = Join-Path $logoBuildDirectory 'generate-app-icon.exe'
$previousLogoPath = $env:PATH
Push-Location -LiteralPath $logoWorkspace
try {
    $env:PATH = 'D:\DevTools\MSYS2\ucrt64\bin;D:\DevTools\MSYS2\usr\bin;' + $env:PATH
    $logoCompiler = (Get-Command 'g++' -ErrorAction Stop).Source
    $logoPkgConfig = (Get-Command 'pkg-config' -ErrorAction Stop).Source
    $logoFlags = (& $logoPkgConfig --cflags --libs Qt6Gui) -split '\s+'
    if ($LASTEXITCODE -ne 0) { throw 'Qt6Gui development files are required to regenerate the icon.' }
    New-Item -ItemType Directory -Path $logoBuildDirectory -Force | Out-Null
    # GCC's narrow Windows argv can corrupt absolute Chinese paths; these source
    # and output arguments stay ASCII relative to the workspace instead.
    & $logoCompiler '-std=c++17' '-O2' 'tools/generate-app-icon.cpp' @logoFlags '-lgdi32' '-o' 'build-release-bin/generate-app-icon.exe'
    if ($LASTEXITCODE -ne 0) { throw 'The app icon generator failed to compile.' }
    & $logoExecutable $logoOutputDirectory (Join-Path $logoBuildDirectory 'app-logo-review.png')
    if ($LASTEXITCODE -ne 0) { throw 'The app icon generator or native Windows ICO validation failed.' }
    Write-Output "Generated and validated SVG, PNG and Windows ICO in $logoOutputDirectory"
} finally {
    $env:PATH = $previousLogoPath
    Pop-Location
}
