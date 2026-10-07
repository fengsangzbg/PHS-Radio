#requires -Version 5.1
<#
Exercise the actual updater installer with two complete, isolated portable copies.
The existing distribution and running player are never modified or launched.

Example:
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/test-portable-update.ps1 `
    -OldPackageDir dist/PHSRadio-0.2.1-windows-x64 `
    -NewPackageDir dist/PHSRadio-0.2.2-windows-x64 -RunSmoke

Artifacts are retained under build-release-bin/update-rehearsal for review.
The installer's explicit -NoRestart test mode is required; it uses no real account.
#>
[CmdletBinding()]
param(
    [string] $OldPackageDir = 'dist/PHSRadio-0.2.1-windows-x64',
    [Parameter(Mandatory = $true)] [string] $NewPackageDir,
    [string] $InstallerPath = 'tools/update-install.ps1',
    [ValidatePattern('^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$')]
    [string] $Version = '0.2.2',
    [string] $ReuseRehearsalDir = '',
    [switch] $RunSmoke
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$workspaceRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$utf8 = New-Object Text.UTF8Encoding($false)
Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class PortableUpdateErrorMode {
    [DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint mode);
}
'@
# Rehearsal children inherit this mode, avoiding modal OS error dialogs.
[PortableUpdateErrorMode]::SetErrorMode(3) | Out-Null

function Resolve-WorkspacePath([string] $Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $workspaceRoot $Path))
}

function Assert-Below([string] $Path, [string] $Root) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($fullRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Test path must remain below ${Root}: $fullPath"
    }
}

function Assert-PlainPath([string] $Path) {
    $candidate = [IO.Path]::GetFullPath($Path)
    while ($candidate) {
        if (Test-Path -LiteralPath $candidate) {
            $entry = Get-Item -LiteralPath $candidate -Force
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Refusing a symlink or junction in the rehearsal: $candidate"
            }
        }
        $candidate = Split-Path -Parent $candidate
    }
}

function Copy-IsolatedTree([string] $Source, [string] $Destination) {
    Assert-PlainPath $Source
    Assert-Below $Destination $rehearsalRoot
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    foreach ($entry in Get-ChildItem -LiteralPath $Source -Force) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing a linked package entry: $($entry.FullName)"
        }
        $target = Join-Path $Destination $entry.Name
        if ($entry.PSIsContainer) { Copy-IsolatedTree $entry.FullName $target }
        else { Copy-Item -LiteralPath $entry.FullName -Destination $target }
    }
}

function Get-TreeHashes([string] $Directory) {
    $hashes = @{}
    foreach ($entry in Get-ChildItem -LiteralPath $Directory -File -Force -Recurse) {
        $relative = $entry.FullName.Substring($Directory.TrimEnd('\').Length + 1)
        $hashes[$relative] = (Get-FileHash -LiteralPath $entry.FullName -Algorithm SHA256).Hash
    }
    return ,$hashes
}

function Assert-TreeMatches([string] $Directory, [hashtable] $Expected, [bool] $AllowExtra = $false) {
    $actual = Get-TreeHashes $Directory
    if (-not $AllowExtra -and $actual.Count -ne $Expected.Count) {
        throw "Unexpected installed file count: expected $($Expected.Count), received $($actual.Count)"
    }
    foreach ($relative in $Expected.Keys) {
        if (-not $actual.ContainsKey($relative) -or $actual[$relative] -ne $Expected[$relative]) {
            throw "An installed file changed unexpectedly: $relative"
        }
    }
}

function Invoke-InstallerCase([string] $Name, [string] $Sha256, [int] $FailAfter = 0) {
    $caseRoot = Join-Path $rehearsalRoot $Name
    if (Test-Path -LiteralPath $caseRoot) {
        # Keep stage/backup paths below the legacy PowerShell/.NET MAX_PATH
        # limit, including the longest license filenames in the real package.
        $caseRoot += '-retry-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
    }
    [IO.Directory]::CreateDirectory($caseRoot) | Out-Null
    $planPath = Join-Path $caseRoot 'plan.json'
    $plan = [ordered]@{
        schema = 1; archivePath = $archivePath; expectedSha256 = $Sha256
        installDir = $installRoot; packageRoot = $packageName; parentPid = 0
        restartExe = 'PHSRadio.exe'
    }
    [IO.File]::WriteAllText($planPath, ($plan | ConvertTo-Json), $utf8)
    $startInfo = New-Object Diagnostics.ProcessStartInfo
    $startInfo.FileName = $powerShellExe
    $startInfo.Arguments = '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
        $installerSource + '" -PlanFile "' + $planPath + '" -NoRestart'
    $startInfo.WorkingDirectory = $caseRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.EnvironmentVariables['APPDATA'] = $settingsRoot
    $startInfo.EnvironmentVariables['LOCALAPPDATA'] = $settingsRoot
    if ($FailAfter -gt 0) {
        $startInfo.EnvironmentVariables['PHSRADIO_UPDATER_TEST_FAIL_AFTER'] = [string]$FailAfter
    } else {
        $startInfo.EnvironmentVariables.Remove('PHSRADIO_UPDATER_TEST_FAIL_AFTER')
    }
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw "Cannot start the isolated installer case: $Name" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(600000)) {
        # This is only the exact helper started here, never the running player.
        $process.Kill()
        throw "Installer rehearsal timed out: $Name"
    }
    $exitCode = $process.ExitCode
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $caseRoot 'stdout.txt'), $stdout, $utf8)
    [IO.File]::WriteAllText((Join-Path $caseRoot 'stderr.txt'), $stderr, $utf8)
    $process.Dispose()
    $resultPath = Join-Path $caseRoot 'installer-result.json'
    if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
        throw "Installer did not produce its result for ${Name}: $stderr"
    }
    $result = Get-Content -LiteralPath $resultPath -Encoding UTF8 -Raw | ConvertFrom-Json
    return [PSCustomObject]@{ Name = $Name; Directory = $caseRoot; ExitCode = $exitCode; Result = $result }
}

function Assert-AccountSentinels {
    foreach ($entry in $sentinelHashes.GetEnumerator()) {
        if ((Get-FileHash -LiteralPath $entry.Key -Algorithm SHA256).Hash -ne $entry.Value) {
            throw "An isolated account or wallpaper sentinel changed: $($entry.Key)"
        }
    }
}

function Remove-VerifiedCaseBuffers([string] $CaseRoot) {
    # Called only after this script has awaited the exact helper and verified
    # every restored/installed byte. Keep the journal, log and result as evidence.
    Assert-Below $CaseRoot $rehearsalRoot
    Assert-PlainPath $CaseRoot
    foreach ($name in @('stage', 'backup')) {
        $bufferRoot = [IO.Path]::GetFullPath((Join-Path $CaseRoot $name))
        Assert-Below $bufferRoot $CaseRoot
        Assert-Below $bufferRoot $rehearsalRoot
        Assert-PlainPath $bufferRoot
        if (Test-Path -LiteralPath $bufferRoot -PathType Container) {
            Remove-Item -LiteralPath $bufferRoot -Recurse -Force
        }
    }
}

$oldRoot = Resolve-WorkspacePath $OldPackageDir
$newRoot = Resolve-WorkspacePath $NewPackageDir
$installerSource = Resolve-WorkspacePath $InstallerPath
foreach ($required in @((Join-Path $oldRoot 'PHSRadio.exe'), (Join-Path $newRoot 'PHSRadio.exe'),
                        (Join-Path $newRoot 'runtime/node.exe'),
                        (Join-Path $newRoot 'services/kugou/server.js'), $installerSource)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Rehearsal input missing: $required" }
}
Assert-PlainPath $oldRoot
Assert-PlainPath $newRoot
Assert-PlainPath $installerSource
$versionInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $newRoot 'PHSRadio.exe'))
$numericVersion = '{0}.{1}.{2}' -f $versionInfo.FileMajorPart, $versionInfo.FileMinorPart, $versionInfo.FileBuildPart
if ($numericVersion -ne $Version) { throw "New package executable version is $numericVersion; expected $Version" }
$packageName = "PHSRadio-$Version-windows-x64"
$rehearsalParent = Join-Path $workspaceRoot 'build-release-bin/update-rehearsal'
Assert-PlainPath $rehearsalParent
if ($ReuseRehearsalDir) {
    $rehearsalRoot = Resolve-WorkspacePath $ReuseRehearsalDir
    Assert-PlainPath $rehearsalRoot
    if (-not (Test-Path -LiteralPath $rehearsalRoot -PathType Container)) {
        throw 'The isolated rehearsal directory to reuse is missing'
    }
} else {
    $rehearsalRoot = Join-Path $rehearsalParent ('升级 演练 ' + [Guid]::NewGuid().ToString('N'))
}
Assert-Below $rehearsalRoot $rehearsalParent
[IO.Directory]::CreateDirectory($rehearsalRoot) | Out-Null
$installRoot = Join-Path $rehearsalRoot '旧版本 安装目录'
$newCopy = Join-Path $rehearsalRoot ('新版本 待发布包/' + $packageName)
$settingsRoot = Join-Path $rehearsalRoot '用户 配置区'
[IO.Directory]::CreateDirectory($settingsRoot) | Out-Null
$powerShellExe = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)) 'System32/WindowsPowerShell/v1.0/powershell.exe'

if (-not $ReuseRehearsalDir) {
    Write-Host 'Creating two isolated full portable package copies...'
    Copy-IsolatedTree $oldRoot $installRoot
    Copy-IsolatedTree $newRoot $newCopy
} else {
    Write-Host 'Reusing the isolated copies and archive; verifying the installed copy is still the old package...'
    foreach ($isolatedPath in @($installRoot, $newCopy)) {
        Assert-Below $isolatedPath $rehearsalRoot
        Assert-PlainPath $isolatedPath
        if (-not (Test-Path -LiteralPath $isolatedPath -PathType Container)) {
            throw "An isolated package copy is missing: $isolatedPath"
        }
    }
}
$accountIni = Join-Path $settingsRoot 'PHS Radio.ini'
$customIni = Join-Path $installRoot '用户自定义.ini'
$customWallpaper = Join-Path $installRoot '我的 壁纸/用户壁纸.txt'
[IO.Directory]::CreateDirectory((Split-Path -Parent $customWallpaper)) | Out-Null
$sentinelText = @{}
$sentinelText[$accountIni] = "[accounts]`nkugou/session=isolated-fake-account-do-not-replace`n"
$sentinelText[$customIni] = "[custom]`nvalue=preserve-existing-user-file`n"
$sentinelText[$customWallpaper] = 'isolated-user-wallpaper-sentinel'
foreach ($sentinel in $sentinelText.Keys) {
    if (-not $ReuseRehearsalDir) { [IO.File]::WriteAllText($sentinel, $sentinelText[$sentinel], $utf8) }
    elseif (-not (Test-Path -LiteralPath $sentinel -PathType Leaf) -or
            [IO.File]::ReadAllText($sentinel, $utf8) -cne $sentinelText[$sentinel]) {
        throw "A rehearsal sentinel was altered or lost: $sentinel"
    }
}
$sentinelHashes = @{}
foreach ($sentinel in @($accountIni, $customIni, $customWallpaper)) {
    $sentinelHashes[$sentinel] = (Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash
}
$originalHashes = Get-TreeHashes $oldRoot
foreach ($sentinel in @($customIni, $customWallpaper)) {
    $originalHashes[$sentinel.Substring($installRoot.Length + 1)] = $sentinelHashes[$sentinel]
}
Assert-TreeMatches $installRoot $originalHashes
$newHashes = Get-TreeHashes $newCopy

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archivePath = Join-Path $rehearsalRoot "PHS-Radio-$Version-windows-x64.zip"
if (-not $ReuseRehearsalDir) {
    [IO.Compression.ZipFile]::CreateFromDirectory((Split-Path -Parent $newCopy), $archivePath,
        [IO.Compression.CompressionLevel]::Fastest, $false)
} elseif (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
    throw 'The isolated archive to reuse is missing'
}
$archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
# Inject after replacing the executable rather than after an arbitrary DLL;
# unchanged dependencies alone cannot prove that the old executable is restored.
$rollbackAfter = 0
$archiveForOrder = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $fileNumber = 0
    foreach ($entry in $archiveForOrder.Entries) {
        if ($entry.FullName.EndsWith('/')) { continue }
        ++$fileNumber
        if ($entry.FullName.Replace('\', '/') -ceq ($packageName + '/PHSRadio.exe')) {
            $rollbackAfter = $fileNumber
            break
        }
    }
} finally { $archiveForOrder.Dispose() }
if ($rollbackAfter -le 0) { throw 'The executable is missing from the rehearsal archive' }

$badHash = ('0' * 64)
if ($archiveHash -eq $badHash) { $badHash = ('1' * 64) }
if (-not $ReuseRehearsalDir) {
    Write-Host 'Checking that checksum failure cannot overwrite the old package...'
    $checksum = Invoke-InstallerCase 'checksum-rejected' $badHash
    if ($checksum.ExitCode -eq 0 -or $checksum.Result.ok) { throw 'Wrong archive checksum was accepted' }
    Assert-TreeMatches $installRoot $originalHashes
    Assert-AccountSentinels
}

if (-not $ReuseRehearsalDir) {
Write-Host 'Checking that a read-only installed executable is rejected before replacement...'
$readOnlyExe = Join-Path $installRoot 'PHSRadio.exe'
$previousAttributes = [IO.File]::GetAttributes($readOnlyExe)
try {
    [IO.File]::SetAttributes($readOnlyExe, ($previousAttributes -bor [IO.FileAttributes]::ReadOnly))
    $readOnly = Invoke-InstallerCase 'read-only-install-rejected' $archiveHash
    if ($readOnly.ExitCode -eq 0 -or $readOnly.Result.ok) { throw 'Read-only installation was overwritten' }
    Assert-TreeMatches $installRoot $originalHashes
    Assert-AccountSentinels
} finally {
    [IO.File]::SetAttributes($readOnlyExe, $previousAttributes)
}
Remove-VerifiedCaseBuffers $readOnly.Directory
} else {
    foreach ($completedCase in @('checksum-rejected', 'read-only-install-rejected')) {
        $previousResultPath = Join-Path (Join-Path $rehearsalRoot $completedCase) 'installer-result.json'
        $previousResult = Get-Content -LiteralPath $previousResultPath -Encoding UTF8 -Raw | ConvertFrom-Json
        if ($previousResult.ok) { throw "The previous rejection test was not successful: $completedCase" }
    }
    Write-Host 'The earlier checksum and read-only rejection checks were preserved; continuing rollback and success cases.'
}

Write-Host 'Checking rollback after a simulated partial write...'
$rollback = Invoke-InstallerCase 'rollback-after-partial-write' $archiveHash $rollbackAfter
if ($rollback.ExitCode -eq 0 -or $rollback.Result.ok -or -not $rollback.Result.rolledBack) {
    throw 'The simulated partial update did not report a successful rollback'
}
Assert-TreeMatches $installRoot $originalHashes
Assert-AccountSentinels
Remove-VerifiedCaseBuffers $rollback.Directory

Write-Host 'Installing the verified full package in the Chinese and space-containing path...'
$success = Invoke-InstallerCase 'full-portable-upgrade' $archiveHash
if ($success.ExitCode -ne 0 -or -not $success.Result.ok) {
    throw "Full portable upgrade failed: $($success.Result.error)"
}
Assert-TreeMatches $installRoot $newHashes $true
Assert-AccountSentinels

if ($RunSmoke) {
    Write-Host 'Checking upgraded runtime with isolated settings and Windows-only PATH...'
    $smokeStart = New-Object Diagnostics.ProcessStartInfo
    $smokeStart.FileName = Join-Path $installRoot 'PHSRadio.exe'
    $smokeStart.Arguments = '--smoke-test'
    $smokeStart.WorkingDirectory = $settingsRoot
    $smokeStart.UseShellExecute = $false
    $smokeStart.CreateNoWindow = $true
    $smokeStart.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $windowsRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
    $smokeStart.EnvironmentVariables['PATH'] = (Join-Path $windowsRoot 'System32') + ';' + $windowsRoot
    $smokeStart.EnvironmentVariables['APPDATA'] = $settingsRoot
    $smokeStart.EnvironmentVariables['LOCALAPPDATA'] = $settingsRoot
    $smoke = New-Object Diagnostics.Process
    $smoke.StartInfo = $smokeStart
    if (-not $smoke.Start()) { throw 'Cannot launch the upgraded isolated runtime smoke check' }
    if (-not $smoke.WaitForExit(30000)) {
        $smoke.Kill()
        throw 'The upgraded isolated runtime smoke check timed out'
    }
    $smokeCode = $smoke.ExitCode
    $smoke.Dispose()
    if ($smokeCode -ne 0) { throw "The upgraded runtime smoke failed with exit code $smokeCode" }
    Assert-AccountSentinels
}
Remove-VerifiedCaseBuffers $success.Directory

$summary = [ordered]@{ ok = $true; version = $Version; root = $rehearsalRoot;
    checksumRejected = $true; readOnlyInstallRejected = $true;
    partialFailureRolledBack = $true; fullPackageUpgraded = $true;
    accountAndCustomFilesPreserved = $true; runtimeSmokeChecked = [bool]$RunSmoke;
    originalFileCount = $originalHashes.Count; newPackageFileCount = $newHashes.Count;
    zipSha256 = $archiveHash }
[IO.File]::WriteAllText((Join-Path $rehearsalRoot 'rehearsal-result.json'),
    ($summary | ConvertTo-Json), $utf8)
Write-Output ([PSCustomObject]$summary)
