#requires -Version 5.1
[CmdletBinding()]
param([string] $Installer = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $Installer) { $Installer = Join-Path $PSScriptRoot '..\tools\update-install.ps1' }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$utf8 = [Text.UTF8Encoding]::new($false)
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$fixtureRoot = Join-Path $workspace ('build-release-bin\updater-test-' + [Guid]::NewGuid().ToString('N'))
$packageName = 'PHSRadio-0.2.2-windows-x64'
$required = @('PHSRadio.exe', 'runtime/node.exe', 'services/kugou/server.js', 'platforms/qwindows.dll')
$powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

function Assert([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}

function Create-Zip([string] $Path, [string] $ExtraEntry = '') {
    $zip = [IO.Compression.ZipFile]::Open($Path, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($relative in $required) {
            $entry = $zip.CreateEntry($packageName + '/' + $relative)
            $stream = $entry.Open()
            try { $data = $utf8.GetBytes('new package ' + $relative); $stream.Write($data, 0, $data.Length) }
            finally { $stream.Dispose() }
        }
        if ($ExtraEntry) {
            $entry = $zip.CreateEntry($ExtraEntry)
            $stream = $entry.Open()
            try { $data = $utf8.GetBytes('invalid fixture'); $stream.Write($data, 0, $data.Length) }
            finally { $stream.Dispose() }
        }
    } finally { $zip.Dispose() }
}

function Run-Case([string] $Name, [string] $ExtraEntry = '', [switch] $BadHash,
                  [switch] $ReadOnly, [switch] $Fault, [switch] $WaitForParent, [switch] $Cancelled) {
    $caseRoot = Join-Path $fixtureRoot $Name
    # Non-ASCII literals are generated from code points so Windows PowerShell 5.1
    # can read this test as ASCII regardless of the system's legacy code page.
    $unicode = ([string][char]0x4e2d) + [char]0x6587
    $installRoot = Join-Path $caseRoot ($unicode + ' portable app')
    $workRoot = Join-Path $caseRoot 'transaction'
    [IO.Directory]::CreateDirectory($installRoot) | Out-Null
    [IO.Directory]::CreateDirectory($workRoot) | Out-Null
    foreach ($relative in $required) {
        $path = Join-Path $installRoot $relative.Replace('/', '\')
        [IO.Directory]::CreateDirectory((Split-Path -Parent $path)) | Out-Null
        [IO.File]::WriteAllText($path, ('old package ' + $relative), $utf8)
    }
    $wallpaper = Join-Path $installRoot ('wallpapers\' + $unicode + ' favorite.png')
    [IO.Directory]::CreateDirectory((Split-Path -Parent $wallpaper)) | Out-Null
    [IO.File]::WriteAllText($wallpaper, 'private wallpaper sentinel', $utf8)
    $settings = Join-Path $installRoot 'my-account-settings.ini'
    [IO.File]::WriteAllText($settings, '[fixture-only] fake-account=preserved', $utf8)
    $zipPath = Join-Path $workRoot 'package.zip'
    Create-Zip $zipPath $ExtraEntry
    $digest = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
    if ($BadHash) { $digest = '0' * 64 }
    $plan = @{ schema = 1; archivePath = $zipPath; expectedSha256 = $digest; installDir = $installRoot;
        packageRoot = $packageName; parentPid = 0; restartExe = 'PHSRadio.exe' }
    if ($WaitForParent) { $plan.parentPid = $PID }
    if ($ReadOnly) { [IO.File]::SetAttributes((Join-Path $installRoot 'PHSRadio.exe'), [IO.FileAttributes]::ReadOnly) }
    if ($Cancelled) { [IO.File]::WriteAllText((Join-Path $workRoot 'installer.cancel'), 'cancel', $utf8) }
    $planPath = Join-Path $workRoot 'plan.json'
    [IO.File]::WriteAllText($planPath, ($plan | ConvertTo-Json -Compress), $utf8)
    $savedFault = $env:PHSRADIO_UPDATER_TEST_FAIL_AFTER
    $env:PHSRADIO_UPDATER_TEST_FAIL_AFTER = if ($Fault) { '3' } else { '' }
    $clock = [Diagnostics.Stopwatch]::StartNew()
    try {
        & $powershell -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $Installer -PlanFile $planPath -NoRestart -ParentWaitSeconds 1
        $exitCode = $LASTEXITCODE
    } finally { $env:PHSRADIO_UPDATER_TEST_FAIL_AFTER = $savedFault }
    $resultPath = Join-Path $workRoot 'installer-result.json'
    Assert (Test-Path -LiteralPath $resultPath -PathType Leaf) "$Name must produce an explicit result file."
    $result = [IO.File]::ReadAllText($resultPath, $utf8) | ConvertFrom-Json
    $expectedSuccess = -not ($BadHash -or $ReadOnly -or $Fault -or $WaitForParent -or $Cancelled -or $ExtraEntry)
    Assert ($result.ok -eq $expectedSuccess) "$Name has an unexpected installer result: $($result.error)"
    Assert ($exitCode -eq $(if ($expectedSuccess) { 0 } else { 1 })) "$Name has the wrong exit status."
    if ($Fault) { Assert $result.rolledBack 'An injected mid-copy failure must restore every changed old file.' }
    if ($WaitForParent) {
        Assert ($clock.ElapsedMilliseconds -ge 900) 'The helper must actually wait for the still-running parent.'
        Assert (Test-Path -LiteralPath (Join-Path $workRoot 'installer.ready')) 'The parent-wait fixture must pass preflight first.'
    }
    foreach ($relative in $required) {
        $expected = $(if ($expectedSuccess) { 'new package ' } else { 'old package ' }) + $relative
        Assert ([IO.File]::ReadAllText((Join-Path $installRoot $relative.Replace('/', '\')), $utf8) -ceq $expected) "$Name did not preserve/replace the expected package file: $relative"
    }
    Assert ([IO.File]::ReadAllText($wallpaper, $utf8) -ceq 'private wallpaper sentinel') "$Name changed an imported wallpaper."
    Assert ([IO.File]::ReadAllText($settings, $utf8) -ceq '[fixture-only] fake-account=preserved') "$Name changed user account settings."
    Assert (-not (Test-Path -LiteralPath (Join-Path $caseRoot 'escape.txt'))) "$Name must not extract outside the stage root."
    Write-Output "PASS $Name"
}

Run-Case 'bad-sha' -BadHash
Run-Case 'read-only' -ReadOnly
Run-Case 'rollback' -Fault
Run-Case 'zip-traversal' ($packageName + '/../escape.txt')
Run-Case 'zip-case-collision' ($packageName + '/phsradio.exe')
Run-Case 'zip-unexpected-root' 'OtherApp/PHSRadio.exe'
Run-Case 'zip-ads' ($packageName + '/runtime/file.dll:stream')
Run-Case 'cancelled' -Cancelled
Run-Case 'parent-still-running' -WaitForParent
Run-Case 'successful-update'
Write-Output 'Updater installer safety fixtures passed; no real executable was launched.'
