#requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $project
$tools=Join-Path $project '.toolchain'
$androidJar=@(Get-ChildItem -LiteralPath (Join-Path $tools 'platform') -Filter android.jar -File -Recurse)[0].FullName
$aapt=@(Get-ChildItem -LiteralPath (Join-Path $tools 'build-tools') -Filter aapt2.exe -File -Recurse)[0].FullName
$buildTools=Split-Path -Parent $aapt
$java=(Get-Command java.exe -ErrorAction Stop).Source
$jdk=Split-Path -Parent $java
$build=Join-Path $project 'app\build\manual'
foreach($folder in @($build,(Join-Path $build 'generated'),(Join-Path $build 'classes'),(Join-Path $build 'dex'))){[IO.Directory]::CreateDirectory($folder)|Out-Null}
function Invoke-Tool([string]$Executable,[string[]]$Arguments){
    # AAPT2 on Windows expects ANSI paths; keep every project argument relative.
    $relative=@($Arguments|ForEach-Object{if($_.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase)){'.\'+$_.Substring($project.Length+1)}else{$_}})
    & $Executable @relative
    if($LASTEXITCODE -ne 0){throw "Build command failed ($LASTEXITCODE): $Executable"}
}
$compiled=Join-Path $build 'resources.zip'
Invoke-Tool $aapt @('compile','--dir',(Join-Path $project 'app\src\main\res'),'-o',$compiled)
$manifest=[IO.File]::ReadAllText((Join-Path $project 'app\src\main\AndroidManifest.xml'))
$manifest=$manifest.Replace('<manifest xmlns:android=','<manifest package="io.github.fengsangzbg.phsradio.mobile" xmlns:android=')
$manifestFile=Join-Path $build 'AndroidManifest.xml'
[IO.File]::WriteAllText($manifestFile,$manifest,[Text.UTF8Encoding]::new($false))
$unsigned=Join-Path $build 'unsigned.apk'
Invoke-Tool $aapt @('link','-I',$androidJar,'--manifest',$manifestFile,'--java',(Join-Path $build 'generated'),'--min-sdk-version','28','--target-sdk-version','35','--version-code','30','--version-name','0.2.0-preview','-o',$unsigned,$compiled)
$qrJar=Join-Path $project 'app\libs\core-3.5.3.jar'
$sources=@(Get-ChildItem -LiteralPath (Join-Path $project 'app\src\main\java'),(Join-Path $build 'generated') -File -Recurse -Filter '*.java'|ForEach-Object{$_.FullName})
Invoke-Tool (Join-Path $jdk 'javac.exe') (@('--release','17','-encoding','UTF-8','-classpath',($androidJar+';'+$qrJar),'-d',(Join-Path $build 'classes'))+$sources)
$classes=@(Get-ChildItem -LiteralPath (Join-Path $build 'classes') -File -Recurse -Filter '*.class'|ForEach-Object{$_.FullName})
Invoke-Tool $java (@('-cp',(Join-Path $buildTools 'lib\d8.jar'),'com.android.tools.r8.D8','--lib',$androidJar,'--min-api','28','--output',(Join-Path $build 'dex'))+$classes+@($qrJar))
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::Open($unsigned,[IO.Compression.ZipArchiveMode]::Update)
try{foreach($dex in Get-ChildItem -LiteralPath (Join-Path $build 'dex') -Filter '*.dex' -File){[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$dex.FullName,$dex.Name,[IO.Compression.CompressionLevel]::Optimal)|Out-Null}}finally{$zip.Dispose()}
$aligned=Join-Path $build 'aligned.apk'
Invoke-Tool (Join-Path $buildTools 'zipalign.exe') @('-f','4',$unsigned,$aligned)
$key=Join-Path $tools 'preview.keystore'
if(-not(Test-Path -LiteralPath $key)){Invoke-Tool (Join-Path $jdk 'keytool.exe') @('-genkeypair','-keystore',$key,'-storepass','android','-keypass','android','-alias','androiddebugkey','-keyalg','RSA','-keysize','2048','-validity','3650','-dname','CN=PHS Radio Android Preview,O=PHS Radio,C=JP')}
$output=Join-Path $project 'app\build\outputs\apk\preview'
[IO.Directory]::CreateDirectory($output)|Out-Null
$apk=Join-Path $output 'PHS-Radio-Android-0.2.0-preview.apk'
Invoke-Tool $java @('-jar',(Join-Path $buildTools 'lib\apksigner.jar'),'sign','--ks',$key,'--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android','--out',$apk,$aligned)
Invoke-Tool $java @('-jar',(Join-Path $buildTools 'lib\apksigner.jar'),'verify','--verbose',$apk)
Invoke-Tool (Join-Path $buildTools 'zipalign.exe') @('-c','4',$apk)
$digest=(Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS.txt'),($digest+'  '+[IO.Path]::GetFileName($apk)+"`n"),[Text.UTF8Encoding]::new($false))
Write-Output "Android preview: $apk"
Write-Output "SHA256: $digest"
