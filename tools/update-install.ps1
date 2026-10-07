#requires -Version 5.1
<#
Fixed offline installer bundled in the executable. It NEVER fetches or runs remote code.
Only files listed in a verified official package are replaced; extra local files survive.
No account settings, registry entries, user cache, wallpaper directories or other
processes are modified. Backups and diagnostics remain in the transaction directory.
-NoRestart is exclusively for isolated fixture verification (parentPid may then be 0).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $PlanFile,
    [switch] $NoRestart,
    [ValidateRange(1, 120)][int] $ParentWaitSeconds = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$transactionRoot = [IO.Path]::GetFullPath((Split-Path -Parent ([IO.Path]::GetFullPath($PlanFile))))
$resultPath = Join-Path $transactionRoot 'installer-result.json'
$logPath = Join-Path $transactionRoot 'update.log'
$cancelPath = Join-Path $transactionRoot 'installer.cancel'
$journal = [Collections.Generic.List[object]]::new()
$replaced = [Collections.Generic.List[object]]::new()
$utf8 = [Text.UTF8Encoding]::new($false)
$parentProcess = $null
$restartPath = $null
$parentExited = $false
$rollbackComplete = $false
$transactionStarted = $false

# One compiled helper avoids the PowerShell filesystem provider for every ancestor
# of every package file. Paths are still checked afresh on EVERY call (no cache).
$nativeHelperSource = @'
using System;
using System.IO;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class PHSRadioUpdateFileSafety {
    [DllImport("kernel32.dll", EntryPoint="GetFileAttributesW", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern uint GetAttributes(string path);
    public static uint Attributes(string path) {
        string nativePath = path.StartsWith(@"\\?\", StringComparison.Ordinal) ? path
            : path.StartsWith(@"\\", StringComparison.Ordinal) ? @"\\?\UNC\" + path.Substring(2)
            : @"\\?\" + Path.GetFullPath(path);
        uint attributes = GetAttributes(nativePath);
        if (attributes == 0xffffffff) {
            int error = Marshal.GetLastWin32Error();
            if (error != 2 && error != 3) throw new IOException("Cannot inspect updater path: " + path, new Win32Exception(error));
        }
        return attributes;
    }
    public static bool FileExists(string path) {
        uint attributes = Attributes(path);
        return attributes != 0xffffffff && (attributes & 0x10) == 0;
    }
    public static void AssertNoReparse(string path) {
        string candidate = Path.GetFullPath(path);
        while (!String.IsNullOrEmpty(candidate)) {
            uint attributes = Attributes(candidate);
            if (attributes != 0xffffffff && (attributes & 0x400) != 0)
                throw new IOException("Updater refuses a symbolic link or junction: " + candidate);
            candidate = Path.GetDirectoryName(candidate);
        }
    }
}
'@

function Write-UpdateLog([string] $Text) {
    [IO.File]::AppendAllText($logPath, ('{0:o} {1}{2}' -f [DateTime]::UtcNow, $Text, [Environment]::NewLine), $utf8)
}

function Write-Result([bool] $Ok, [bool] $RolledBack, [string] $ErrorText) {
    $result = @{ ok = $Ok; rolledBack = $RolledBack; error = $ErrorText; logPath = $logPath }
    [IO.File]::WriteAllText($resultPath, ($result | ConvertTo-Json -Compress), $utf8)
}

function Assert-LocalAbsolute([string] $Path) {
    if ($Path -notmatch '^[A-Za-z]:[\\/]' -or $Path.StartsWith('\\')) {
        throw 'Updater paths must be absolute paths on a local Windows drive.'
    }
    return [IO.Path]::GetFullPath($Path)
}

function Assert-Under([string] $Path, [string] $Root) {
    $candidate = [IO.Path]::GetFullPath($Path)
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if (-not $candidate.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path escapes its intended directory: $candidate"
    }
}

function Assert-NoReparse([string] $Path) {
    [PHSRadioUpdateFileSafety]::AssertNoReparse([IO.Path]::GetFullPath($Path))
}

function Assert-NotCancelled {
    if ([PHSRadioUpdateFileSafety]::FileExists($cancelPath)) { throw 'Update cancelled before installation.' }
}

function Start-Player {
    if (-not $NoRestart -and $restartPath -and (Test-Path -LiteralPath $restartPath -PathType Leaf)) {
        # The installer stays hidden; the requested restarted player is interactive.
        Start-Process -FilePath $restartPath -WorkingDirectory (Split-Path -Parent $restartPath) -WindowStyle Normal | Out-Null
    }
}

function Show-InstallationFailure([string] $Message, [bool] $Restored) {
    if ($NoRestart) { return }
    # Keep this script ASCII for Windows PowerShell 5.1, while showing real Unicode.
    $title = [Regex]::Unescape('PHS Radio - \u66f4\u65b0\u5931\u8d25')
    $heading = if ($Restored) {
        [Regex]::Unescape('\u66f4\u65b0\u672a\u5b8c\u6210\uff0c\u65e7\u7248\u672c\u6587\u4ef6\u5df2\u4fdd\u7559\u6216\u6062\u590d\u3002')
    } else {
        [Regex]::Unescape('\u66f4\u65b0\u672a\u5b8c\u6210\uff0c\u81ea\u52a8\u56de\u6eda\u672a\u5b8c\u5168\u6210\u529f\u3002\u8bf7\u6839\u636e\u65e5\u5fd7\u548c\u5907\u4efd\u6587\u4ef6\u6062\u590d\uff0c\u6682\u65f6\u4e0d\u8981\u5220\u9664\u66f4\u65b0\u76ee\u5f55\u3002')
    }
    $logLabel = [Regex]::Unescape('\u8be6\u7ec6\u65e5\u5fd7\uff1a')
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PHSRadioInstallerDialog {
    [DllImport("user32.dll", EntryPoint="MessageBoxW", CharSet=CharSet.Unicode)]
    public static extern int Show(IntPtr owner, string text, string caption, uint flags);
}
'@
    $text = $heading + [Environment]::NewLine + [Environment]::NewLine + $Message +
        [Environment]::NewLine + [Environment]::NewLine + $logLabel + [Environment]::NewLine + $logPath
    # Only a real installation failure after the old player exited reaches this.
    # NoRestart fixtures never show a dialog or wait for user input.
    [PHSRadioInstallerDialog]::Show([IntPtr]::Zero, $text, $title, 0x50010) | Out-Null
}

try {
    Add-Type -TypeDefinition $nativeHelperSource
    Assert-NoReparse $transactionRoot
    if (@(Get-ChildItem -LiteralPath $transactionRoot -Force | Where-Object { $_.Name -in @('stage', 'backup', 'installer.ready', 'installer-result.json') }).Count -ne 0) {
        throw 'An installation transaction cannot be reused.'
    }
    $plan = [IO.File]::ReadAllText([IO.Path]::GetFullPath($PlanFile), $utf8) | ConvertFrom-Json
    if ($plan.schema -ne 1 -or $plan.restartExe -cne 'PHSRadio.exe' -or
        $plan.packageRoot -cnotmatch '^PHSRadio-(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-windows-x64$' -or
        $plan.expectedSha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw 'Invalid update plan or package name.'
    }
    $archivePath = Assert-LocalAbsolute ([string] $plan.archivePath)
    $installRoot = (Assert-LocalAbsolute ([string] $plan.installDir)).TrimEnd('\', '/')
    if ($installRoot -eq [IO.Path]::GetPathRoot($installRoot).TrimEnd('\', '/')) {
        throw 'Refusing to install into a drive root.'
    }
    Assert-NoReparse $archivePath
    Assert-NoReparse $installRoot
    $restartPath = Join-Path $installRoot 'PHSRadio.exe'
    if (-not (Test-Path -LiteralPath $restartPath -PathType Leaf)) { throw 'The target is not an existing PHS Radio directory.' }
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) { throw 'The downloaded archive is missing.' }
    $actualDigest = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    if ($actualDigest -ine [string] $plan.expectedSha256) { throw 'Downloaded archive SHA-256 verification failed. No installed files changed.' }
    if ([long] $plan.parentPid -gt 0) {
        try { $parentProcess = [Diagnostics.Process]::GetProcessById([int] $plan.parentPid) }
        catch { throw 'The player process exited before the installation handshake.' }
        if (-not $NoRestart) {
            if ([IO.Path]::GetFullPath($parentProcess.MainModule.FileName) -ine $restartPath) {
                throw 'The installation plan does not identify the running player.'
            }
        }
        # Open and retain a process handle now, so PID recycling cannot target a new process.
        $null = $parentProcess.Handle
    } elseif (-not $NoRestart) { throw 'Missing player process identity.' }

    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $stageRoot = Join-Path $transactionRoot 'stage'
    $backupRoot = Join-Path $transactionRoot 'backup'
    [IO.Directory]::CreateDirectory($stageRoot) | Out-Null
    [IO.Directory]::CreateDirectory($backupRoot) | Out-Null
    $files = [Collections.Generic.List[object]]::new()
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $totalSize = [long] 0
    $archive = [IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        if ($archive.Entries.Count -gt 30000) { throw 'The update archive has too many entries.' }
        foreach ($entry in $archive.Entries) {
            Assert-NotCancelled
            $name = $entry.FullName.Replace('\', '/')
            $directory = $name.EndsWith('/')
            $trimmed = $name.TrimEnd('/')
            if ($trimmed -ceq [string] $plan.packageRoot -and $directory) { continue }
            if (-not $name.StartsWith(([string] $plan.packageRoot + '/'), [StringComparison]::Ordinal)) {
                throw "Unexpected archive root: $name"
            }
            $relative = $trimmed.Substring(([string] $plan.packageRoot).Length + 1)
            if (-not $relative -or $relative.Contains(':') -or $relative.StartsWith('/')) { throw "Unsafe archive path: $name" }
            foreach ($segment in $relative.Split('/')) {
                if (-not $segment -or $segment -in @('.', '..') -or $segment -match '[<>:"|?*\x00-\x1f]' -or
                    $segment.EndsWith('.') -or $segment.EndsWith(' ') -or
                    $segment -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') {
                    throw "Unsafe Windows archive path: $name"
                }
            }
            # Unix symlinks and DOS reparse points cannot be installed from a ZIP.
            $attributeBits = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int] $entry.ExternalAttributes), 0)
            if ((($attributeBits -shr 16) -band 0xf000) -eq 0xa000 -or ($attributeBits -band 0x400)) {
                throw "Archive links are forbidden: $name"
            }
            if (-not $names.Add($relative)) { throw "Duplicate or case-colliding archive entry: $name" }
            if ($relative.Split('/')[0] -in @('wallpapers', 'wallpaper', 'backgrounds', 'accounts', 'sessions', 'cache', 'settings')) {
                throw 'An update package cannot contain user-data directories.'
            }
            $stagePath = [IO.Path]::GetFullPath((Join-Path $stageRoot $relative.Replace('/', '\')))
            $targetPath = [IO.Path]::GetFullPath((Join-Path $installRoot $relative.Replace('/', '\')))
            Assert-Under $stagePath $stageRoot
            Assert-Under $targetPath $installRoot
            Assert-NoReparse $targetPath
            if ($directory) { [IO.Directory]::CreateDirectory($stagePath) | Out-Null; continue }
            $totalSize += $entry.Length
            if ($entry.Length -gt 2147483648 -or $totalSize -gt 6442450944) { throw 'The expanded archive exceeds its safety limit.' }
            $targetAttributes = [PHSRadioUpdateFileSafety]::Attributes($targetPath)
            if ($targetAttributes -ne [uint32]::MaxValue -and ($targetAttributes -band 0x10)) {
                throw "A directory conflicts with an update file: $targetPath"
            }
            if ($targetAttributes -ne [uint32]::MaxValue -and ($targetAttributes -band 0x1)) {
                throw "An installed file is read-only. Move the portable app to a writable directory or remove the read-only attribute: $targetPath"
            }
            [IO.Directory]::CreateDirectory((Split-Path -Parent $stagePath)) | Out-Null
            $source = $entry.Open()
            $destination = [IO.File]::Open($stagePath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try {
                $buffer = [byte[]]::new(65536)
                $expanded = [long] 0
                while (($count = $source.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $expanded += $count
                    if ($expanded -gt $entry.Length) { throw "Archive data exceeds its declared length: $name" }
                    $destination.Write($buffer, 0, $count)
                }
                if ($expanded -ne $entry.Length) { throw "Archive data is truncated: $name" }
            } finally { $destination.Dispose(); $source.Dispose() }
            $files.Add(@{ relative = $relative; stage = $stagePath; target = $targetPath; existed = $false; backup = '' })
        }
    } finally { $archive.Dispose() }
    foreach ($required in @('PHSRadio.exe', 'runtime/node.exe', 'services/kugou/server.js', 'platforms/qwindows.dll')) {
        if (-not ($files | Where-Object { $_.relative -ceq $required })) { throw "The package is incomplete: $required" }
    }
    # Prove ordinary-directory write permission without touching a user's existing file.
    $writeProbe = Join-Path $installRoot ('.phsradio-update-write-' + [Guid]::NewGuid().ToString('N') + '.tmp')
    Assert-Under $writeProbe $installRoot
    try { [IO.File]::WriteAllText($writeProbe, 'permission check', $utf8) }
    finally { if (Test-Path -LiteralPath $writeProbe -PathType Leaf) { Remove-Item -LiteralPath $writeProbe -Force } }
    Assert-NotCancelled
    Write-UpdateLog ("Preflight passed: {0} files, {1} expanded bytes." -f $files.Count, $totalSize)
    [IO.File]::WriteAllText((Join-Path $transactionRoot 'installer.ready'), 'ready', $utf8)
    if ($parentProcess) {
        $deadline = [DateTime]::UtcNow.AddSeconds($ParentWaitSeconds)
        while (-not $parentProcess.WaitForExit(100)) {
            Assert-NotCancelled
            if ([DateTime]::UtcNow -gt $deadline) { throw 'The player has not exited. No files were replaced; close it and retry the update.' }
        }
    }
    Assert-NotCancelled
    $parentExited = $true
    Write-UpdateLog 'Player exited; backing up every existing package file before replacement.'
    foreach ($file in $files) {
        Assert-NoReparse $file.target
        $file.existed = [PHSRadioUpdateFileSafety]::FileExists($file.target)
        if ($file.existed) {
            # Test all existing targets after the player exited, before any mutation.
            # A remaining service/process holding a DLL leaves the old app intact.
            $accessProbe = [IO.File]::Open($file.target, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            $accessProbe.Dispose()
            $file.backup = [IO.Path]::GetFullPath((Join-Path $backupRoot $file.relative.Replace('/', '\')))
            Assert-Under $file.backup $backupRoot
            [IO.Directory]::CreateDirectory((Split-Path -Parent $file.backup)) | Out-Null
            Copy-Item -LiteralPath $file.target -Destination $file.backup -Force
        }
        $journal.Add($file)
    }
    [IO.File]::WriteAllText((Join-Path $transactionRoot 'journal.json'), ($journal | ConvertTo-Json -Depth 4), $utf8)
    Write-UpdateLog 'Backups complete; beginning package replacement.'
    $faultAfter = 0
    if ($NoRestart -and $env:PHSRADIO_UPDATER_TEST_FAIL_AFTER) {
        if (-not [int]::TryParse($env:PHSRADIO_UPDATER_TEST_FAIL_AFTER, [ref] $faultAfter) -or $faultAfter -le 0) {
            throw 'Invalid isolated-test fault injection.'
        }
    }
    foreach ($file in $files) {
        Assert-NoReparse $file.target
        [IO.Directory]::CreateDirectory((Split-Path -Parent $file.target)) | Out-Null
        # Record BEFORE writing: a failing copy may already have truncated its destination.
        $replaced.Add($file)
        $transactionStarted = $true
        Copy-Item -LiteralPath $file.stage -Destination $file.target -Force
        if ($faultAfter -gt 0 -and $replaced.Count -ge $faultAfter) { throw 'Injected copy failure for isolated rollback verification.' }
    }
    Write-UpdateLog 'All package files replaced successfully; extra local files were preserved.'
    Write-Result $true $false ''
    Start-Player
    exit 0
} catch {
    $message = $_.Exception.Message
    try { Write-UpdateLog ("ERROR: " + $message) } catch { }
    if ($transactionStarted) {
        $rollbackComplete = $true
        for ($index = $replaced.Count - 1; $index -ge 0; --$index) {
            $file = $replaced[$index]
            try {
                Assert-Under $file.target $installRoot
                Assert-NoReparse $file.target
                if ($file.existed) { Copy-Item -LiteralPath $file.backup -Destination $file.target -Force }
                elseif (Test-Path -LiteralPath $file.target -PathType Leaf) { Remove-Item -LiteralPath $file.target -Force }
            } catch {
                $rollbackComplete = $false
                try { Write-UpdateLog ("ROLLBACK ERROR: " + $_.Exception.Message) } catch { }
            }
        }
        if ($rollbackComplete) { $message += ' Previous application files were restored.' }
        else { $message += " Rollback needs manual recovery; backups are in: $backupRoot" }
    }
    try { Write-Result $false $rollbackComplete $message } catch { }
    # A preflight error leaves the still-running player alone. After installation
    # failure, restart only if the previous executable can be restored safely.
    if ($parentExited -and (-not $transactionStarted -or $rollbackComplete)) {
        try { Start-Player } catch {
            $message += " The restored player could not restart automatically: $($_.Exception.Message)"
            try { Write-UpdateLog $message; Write-Result $false $rollbackComplete $message } catch { }
        }
    }
    if ($parentExited -and -not $NoRestart) {
        try { Show-InstallationFailure $message (-not $transactionStarted -or $rollbackComplete) }
        catch { try { Write-UpdateLog ("Unable to show failure dialog: " + $_.Exception.Message) } catch { } }
    }
    exit 1
} finally {
    if ($parentProcess) { $parentProcess.Dispose() }
}
