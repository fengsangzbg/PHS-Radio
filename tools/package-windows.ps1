#requires -Version 5.1
<#
Build the portable Windows distribution without installing anything or launching the player.
Example: powershell -NoProfile -File tools/package-windows.ps1 -BuildDir build-release-bin -ExePath build-release-bin/publish/PHSRadio.exe
Use -ValidateOnly to check the selected inputs without creating package files.
The output directory must be new or empty; this script never removes existing files.
#>
[CmdletBinding()]
param(
    [string] $BuildDir = 'build-release-bin',
    [string] $OutputRoot = 'dist',
    [string] $ExePath = '',
    [ValidateSet('Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string] $Configuration = 'Release',
    [ValidatePattern('^[0-9A-Za-z][0-9A-Za-z._+-]*$')]
    [string] $Version = '0.2.1',
    [switch] $ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$workspaceRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$distRoot = [IO.Path]::GetFullPath((Join-Path $workspaceRoot 'dist'))

function Get-WorkspacePath([string] $Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $workspaceRoot $Path))
}

function Assert-UnderPath([string] $Path, [string] $Root) {
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if ($fullPath -ne $fullRoot -and
        -not $fullPath.StartsWith($fullRoot + [IO.Path]::DirectorySeparatorChar,
                                 [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path must stay under ${fullRoot}: $fullPath"
    }
}

function Assert-NoReparsePoint([string] $Path) {
    $candidate = [IO.Path]::GetFullPath($Path)
    while ($candidate) {
        if (Test-Path -LiteralPath $candidate) {
            $item = Get-Item -LiteralPath $candidate -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Refusing a symlink or junction in packaging paths: $candidate"
            }
        }
        $candidate = Split-Path -Parent $candidate
    }
}

function Copy-ExactFile([string] $Source, [string] $Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required file is missing: $Source"
    }
    Assert-NoReparsePoint $Source
    Assert-UnderPath $Destination $packageRoot
    $parent = Split-Path -Parent $Destination
    [IO.Directory]::CreateDirectory($parent) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Copy-ExactTree([string] $Source, [string] $Destination,
                        [switch] $SkipNestedNodeModules,
                        [switch] $ProductionPackage) {
    Assert-NoReparsePoint $Source
    Assert-UnderPath $Destination $packageRoot
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
        if ($ProductionPackage -and (Test-PrivatePackageEntry $item.Name $item.PSIsContainer)) { continue }
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing a symlink or junction in package contents: $($item.FullName)"
        }
        if ($item.PSIsContainer) {
            if ($SkipNestedNodeModules -and $item.Name -eq 'node_modules') { continue }
            Copy-ExactTree $item.FullName (Join-Path $Destination $item.Name) `
                -SkipNestedNodeModules:$SkipNestedNodeModules -ProductionPackage:$ProductionPackage
        } else {
            Copy-ExactFile $item.FullName (Join-Path $Destination $item.Name)
        }
    }
}

function Test-PrivatePackageEntry([string] $Name, [bool] $Directory) {
    if ($Name -match '^\.env(?:\..*)?$' -or $Name -in @('.npmrc', '.yarnrc', '.yarnrc.yml')) { return $true }
    if ($Directory) {
        return $Name -in @('.git', '.hg', '.svn', '.cache', '.npm-cache', 'logs', 'accounts', 'sessions')
    }
    return $Name -match '\.(log|cookie)$' -or $Name -in @('cookie.txt', 'cookies.txt',
        'credentials.json', 'session.json', 'token.json', 'auth.json', 'anonymous_token')
}

function Get-CMakeValue([string] $Name) {
    $entry = @($cmakeLines | Where-Object { $_ -match ('^' + [regex]::Escape($Name) + ':[^=]+=') })
    if ($entry.Count -eq 0) { return '' }
    return ($entry[0] -split '=', 2)[1]
}

$buildRoot = Get-WorkspacePath $BuildDir
$outputPath = Get-WorkspacePath $OutputRoot
Assert-UnderPath $outputPath $distRoot
Assert-NoReparsePoint $outputPath
$packageRoot = Join-Path $outputPath "PHSRadio-$Version-windows-x64"
Assert-UnderPath $packageRoot $distRoot
Assert-NoReparsePoint $packageRoot
if (Test-Path -LiteralPath $packageRoot) {
    if (-not (Test-Path -LiteralPath $packageRoot -PathType Container)) {
        throw "The package destination is not a directory: $packageRoot"
    }
    if (@(Get-ChildItem -LiteralPath $packageRoot -Force).Count -ne 0) {
        throw "Package directory is not empty. Choose a new Version or OutputRoot; no files were removed: $packageRoot"
    }
}

# Infer the toolchain from this build instead of binding releases to one local build directory.
$ucrtRoot = 'D:\DevTools\MSYS2\ucrt64'
$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
$cmakeLines = @()
if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
    # CMake writes UTF-8 paths; Windows PowerShell 5.1 otherwise decodes a
    # non-ASCII project directory with its legacy local code page.
    $cmakeLines = @(Get-Content -LiteralPath $cachePath -Encoding UTF8)
    $qtDirectory = Get-CMakeValue 'Qt6_DIR'
    if ($qtDirectory) {
        $ucrtRoot = [IO.Path]::GetFullPath((Join-Path $qtDirectory '..\..\..'))
    }
}
if ((Get-CMakeValue 'PHSRADIO_KUGOU_ONLY') -notin @('ON', 'TRUE', '1')) {
    throw 'This public package requires a build configured with -DPHSRADIO_KUGOU_ONLY=ON. Other platforms are not published.'
}
$ucrtBin = Join-Path $ucrtRoot 'bin'
$deployTool = Join-Path $ucrtBin 'windeployqt6.exe'
if (-not (Test-Path -LiteralPath $deployTool -PathType Leaf)) {
    $deployTool = Join-Path $ucrtBin 'windeployqt.exe'
}
$objdumpTool = Join-Path $ucrtBin 'objdump.exe'
$nodeSource = Join-Path $ucrtBin 'node.exe'
$configuredOutput = Get-CMakeValue 'PHSRADIO_APP_OUTPUT_DIR'
if (-not $configuredOutput) { $configuredOutput = Get-CMakeValue 'CMAKE_RUNTIME_OUTPUT_DIRECTORY' }
if ($ExePath) {
    $exeSource = Get-WorkspacePath $ExePath
} else {
    $exeRoot = $buildRoot
    if ($configuredOutput) {
        if ([IO.Path]::IsPathRooted($configuredOutput)) { $exeRoot = [IO.Path]::GetFullPath($configuredOutput) }
        else { $exeRoot = [IO.Path]::GetFullPath((Join-Path $buildRoot $configuredOutput)) }
    }
    $candidates = @((Join-Path $exeRoot 'PHSRadio.exe'), (Join-Path $exeRoot "$Configuration\PHSRadio.exe"))
    if ((Get-CMakeValue 'CMAKE_GENERATOR') -match 'Visual Studio|Multi-Config|Xcode') {
        $candidates = @($candidates[1], $candidates[0])
    }
    $exeSource = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $exeSource) {
        throw "The configured executable is missing: $($candidates -join ', '). Build PHSRadio first or pass -ExePath. No older output was selected."
    }
}

$serviceSource = Join-Path $workspaceRoot 'services\kugou'
$releaseNotes = Join-Path $workspaceRoot 'release-notes.txt'
$projectLicense = Join-Path $workspaceRoot 'LICENSE'
$thirdPartyNotice = Join-Path $workspaceRoot 'THIRD_PARTY.md'
foreach ($required in @($exeSource, $deployTool, $objdumpTool, $nodeSource, $releaseNotes, $projectLicense, $thirdPartyNotice,
                        (Join-Path $serviceSource 'server.js'),
                        (Join-Path $serviceSource 'package.json'),
                        (Join-Path $serviceSource 'package-lock.json'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required packaging input is missing: $required"
    }
}

Assert-NoReparsePoint $exeSource
$exeVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($exeSource)
if ($Version -match '^(\d+)\.(\d+)\.(\d+)(?:[.+-].*)?$') {
    $wanted = @([int]$Matches[1], [int]$Matches[2], [int]$Matches[3])
    if ($exeVersion.FileMajorPart -ne $wanted[0] -or $exeVersion.FileMinorPart -ne $wanted[1] -or
        $exeVersion.FileBuildPart -ne $wanted[2]) {
        throw "Executable version '$($exeVersion.FileVersion)' does not match package version '$Version': $exeSource"
    }
}

# Resolve the exact production package list before touching the distribution.
# PowerShell 5.1 rejects the npm packages table's empty root key; rename it only in memory.
$lockText = Get-Content -LiteralPath (Join-Path $serviceSource 'package-lock.json') -Encoding UTF8 -Raw
$lockText = $lockText -replace '(?m)^(\s*)""(\s*:)', '$1"__phs_root_package__"$2'
$lock = $lockText | ConvertFrom-Json
if (-not $lock.PSObject.Properties['packages']) {
    throw 'The service lockfile must contain the npm v2/v3 packages table.'
}
$productionPackages = @()
foreach ($entry in $lock.packages.PSObject.Properties) {
    if ($entry.Name -eq '__phs_root_package__') { continue }
    if ($entry.Name -notmatch '^node_modules/(?:[^/]+/)*[^/]+$' -or ($entry.Name -split '/') -contains '..') {
        throw "Unexpected npm package path in lockfile: $($entry.Name)"
    }
    if ($entry.Value.PSObject.Properties['dev'] -and $entry.Value.dev) { continue }
    $source = Join-Path $serviceSource ($entry.Name -replace '/', '\')
    Assert-UnderPath $source $serviceSource
    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        if ($entry.Value.PSObject.Properties['optional'] -and $entry.Value.optional) { continue }
        throw "Locked production dependency is missing. Install service dependencies first: $source"
    }
    Assert-NoReparsePoint $source
    $productionPackages += [PSCustomObject]@{ Source = $source; Relative = ($entry.Name -replace '/', '\') }
}
if ($ValidateOnly) {
    Write-Host 'Packaging inputs validated; no files were copied and the player was not launched.'
    Write-Output ([PSCustomObject]@{ Version = $Version; Executable = $exeSource;
        FileVersion = $exeVersion.FileVersion; Platform = 'Kugou';
        QtTools = $ucrtBin; ProductionPackages = $productionPackages.Count; Destination = $packageRoot })
    return
}
[IO.Directory]::CreateDirectory($packageRoot) | Out-Null
$packageExe = Join-Path $packageRoot 'PHSRadio.exe'
Copy-ExactFile $exeSource $packageExe
Write-Host "Deploying Qt into $packageRoot"
$originalPath = $env:PATH
try {
    # This affects only this process and its packaging tools, not system settings.
    $env:PATH = $ucrtBin + ';' + $originalPath
    & $deployTool --release --no-translations --dir $packageRoot $packageExe
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }
} finally {
    $env:PATH = $originalPath
}

$runtimeRoot = Join-Path $packageRoot 'runtime'
Copy-ExactFile $nodeSource (Join-Path $runtimeRoot 'node.exe')
$serviceDestination = Join-Path $packageRoot 'services\kugou'
# Deliberately do not copy the service root wholesale or export QSettings/registry data.
foreach ($name in @('server.js', 'package.json', 'package-lock.json')) {
    Copy-ExactFile (Join-Path $serviceSource $name) (Join-Path $serviceDestination $name)
}
$packageCount = 0
foreach ($entry in $productionPackages) {
    # Full locked paths preserve nested/scoped packages; runtime state is excluded.
    Copy-ExactTree $entry.Source (Join-Path $serviceDestination $entry.Relative) `
        -SkipNestedNodeModules -ProductionPackage
    ++$packageCount
}
# .bin and node_modules/.package-lock.json are unnecessary for require('kugoumusicapi').

$systemRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
function Test-SystemImport([string] $Name) {
    if ($Name -match '^(api-ms-win-|ext-ms-win-)') { return $true }
    # Visual C++ redist libraries are not guaranteed on a fresh Windows install.
    if ($Name -match '^(vcruntime\d+|msvcp\d+|concrt\d+|vcomp\d+|msvcr\d+)(?:_[^.]*)?\.dll$') { return $false }
    # A redistributable MSYS2 DLL stays a package dependency even when this
    # developer machine happens to have another copy installed in System32.
    if (Test-Path -LiteralPath (Join-Path $ucrtBin $Name) -PathType Leaf) { return $false }
    foreach ($folder in @('System32', 'SysWOW64')) {
        if (Test-Path -LiteralPath (Join-Path (Join-Path $systemRoot $folder) $Name) -PathType Leaf) {
            return $true
        }
    }
    return $false
}

# Inspect each PE file and walk imports to a fixed point. Qt's deployment tool does not
# include every MSYS2 FFmpeg/ICU/MinGW dependency, and Node needs its own DLL search root.
$pending = New-Object 'System.Collections.Generic.Queue[object]'
$inspected = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$runtimePrefix = $runtimeRoot.TrimEnd('\') + '\'
$servicePrefix = $serviceDestination.TrimEnd('\') + '\'
foreach ($binary in Get-ChildItem -LiteralPath $packageRoot -Recurse -File -Force) {
    if ($binary.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "A deployment tool created an unsupported link: $($binary.FullName)"
    }
    if ($binary.Extension -notin @('.exe', '.dll', '.node')) { continue }
    $dllRoot = $packageRoot
    if ($binary.FullName.StartsWith($runtimePrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $binary.FullName.StartsWith($servicePrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $binary.Extension -eq '.node') { $dllRoot = $runtimeRoot }
    if ($binary.Extension -eq '.exe' -and $binary.FullName.StartsWith($servicePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        $dllRoot = $binary.DirectoryName
    }
    $pending.Enqueue([PSCustomObject]@{ Path = $binary.FullName; DllRoot = $dllRoot })
}
$copiedDlls = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$systemImports = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
while ($pending.Count -gt 0) {
    $binary = $pending.Dequeue()
    # One DLL can serve both Qt and a Node child; resolve each search-root context.
    if (-not $inspected.Add($binary.DllRoot + '|' + $binary.Path)) { continue }
    # GNU binutils may reject absolute paths containing Chinese characters.
    # Pass just the binary name from its actual directory; PowerShell keeps
    # the Unicode working directory intact when launching the native tool.
    Push-Location -LiteralPath (Split-Path -Parent $binary.Path)
    try {
        $dump = @(& $objdumpTool -p ([IO.Path]::GetFileName($binary.Path)) 2>&1)
        $dumpExitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($dumpExitCode -ne 0) { throw "Cannot inspect PE imports: $($binary.Path)`n$($dump -join "`n")" }
    foreach ($line in $dump) {
        if ([string]$line -notmatch '^\s*DLL Name:\s*(\S+)\s*$') { continue }
        $name = $Matches[1]
        if ([IO.Path]::GetFileName($name) -ne $name) { throw "Invalid imported DLL name: $name" }
        if (Test-SystemImport $name) {
            $systemImports.Add($name) | Out-Null
            continue
        }
        # Package-local companions take priority; otherwise place imports in the EXE's directory.
        $local = Join-Path (Split-Path -Parent $binary.Path) $name
        $destination = Join-Path $binary.DllRoot $name
        if (Test-Path -LiteralPath $local -PathType Leaf) {
            $pending.Enqueue([PSCustomObject]@{ Path = $local; DllRoot = $binary.DllRoot })
            continue
        }
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            $source = Join-Path $ucrtBin $name
            if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
                throw "Unresolved non-system dependency '$name' imported by '$($binary.Path)'. No DLL was downloaded or guessed."
            }
            Copy-ExactFile $source $destination
            $copiedDlls.Add($destination) | Out-Null
        }
        $pending.Enqueue([PSCustomObject]@{ Path = $destination; DllRoot = $binary.DllRoot })
    }
}

Copy-ExactFile $releaseNotes (Join-Path $packageRoot '使用说明.txt')
Copy-ExactFile $projectLicense (Join-Path $packageRoot 'LICENSE')
Copy-ExactFile $thirdPartyNotice (Join-Path $packageRoot 'THIRD_PARTY.md')
$licenseSource = Join-Path $ucrtRoot 'share\licenses'
if (Test-Path -LiteralPath $licenseSource -PathType Container) {
    # Keep installed third-party texts verbatim, including Qt, Node's bundled notices,
    # OpenSSL and compiler runtimes. npm package licenses remain beside their code.
    Copy-ExactTree $licenseSource (Join-Path $packageRoot 'licenses\msys2')
} else {
    throw "The third-party license directory is required for public distribution: $licenseSource"
}
$commonLicenseSource = Join-Path (Split-Path -Parent $ucrtRoot) 'usr\share\licenses\common'
if (Test-Path -LiteralPath $commonLicenseSource -PathType Container) {
    Copy-ExactTree $commonLicenseSource (Join-Path $packageRoot 'licenses\common')
}
# FFmpeg is sometimes installed without a share/licenses entry; its tool prints the
# license corresponding to the exact installed build. -L does not open or play media.
$ffmpegTool = Join-Path $ucrtBin 'ffmpeg.exe'
if (Test-Path -LiteralPath $ffmpegTool -PathType Leaf) {
    # Windows PowerShell 5.1 turns redirected native stderr (FFmpeg's banner)
    # into ErrorRecords even on success. Capture it without treating the banner
    # as a terminating PowerShell error, then check the actual exit code.
    $previousErrorPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $ffmpegLicense = @(& $ffmpegTool -L 2>&1)
        $ffmpegExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorPreference
    }
    if ($ffmpegExitCode -eq 0) {
        [IO.Directory]::CreateDirectory((Join-Path $packageRoot 'licenses')) | Out-Null
        [IO.File]::WriteAllLines((Join-Path $packageRoot 'licenses\FFmpeg-build-license.txt'),
            [string[]]$ffmpegLicense, (New-Object Text.UTF8Encoding($true)))
    } else { Write-Warning 'Could not obtain the installed FFmpeg build license using ffmpeg -L.' }
}

$manifest = @(
    "PHS Radio $Version - portable Windows x64",
    "Build configuration: $Configuration (Kugou only)",
    "Executable file version: $($exeVersion.FileVersion)",
    "Executable SHA-256: $((Get-FileHash -LiteralPath $packageExe -Algorithm SHA256).Hash)",
    "Production npm package entries: $packageCount",
    "Additional runtime DLL copies: $($copiedDlls.Count)",
    'System DLLs/API sets are supplied by Windows and were not copied.',
    'No account settings, registry exports, development caches or source build directories were packaged.',
    '', 'Additional DLLs:')
$manifest += @($copiedDlls | Sort-Object | ForEach-Object { $_.Substring($packageRoot.Length + 1) })
$manifest += @('', 'Windows imports:')
$manifest += @($systemImports | Sort-Object)
[IO.File]::WriteAllLines((Join-Path $packageRoot 'package-manifest.txt'), [string[]]$manifest,
    (New-Object Text.UTF8Encoding($true)))
foreach ($entry in Get-ChildItem -LiteralPath $packageRoot -Force -Recurse) {
    if (Test-PrivatePackageEntry $entry.Name $entry.PSIsContainer) {
        throw "Private runtime state appeared in the package: $($entry.FullName)"
    }
}
if (Test-Path -LiteralPath (Join-Path $packageRoot 'services\netease')) {
    throw 'A Kugou-only public package must not contain the NetEase service.'
}
Write-Host "Portable package ready: $packageRoot"
Write-Host 'The player was not launched. Keep this entire directory together when distributing it.'
Write-Output $packageRoot
