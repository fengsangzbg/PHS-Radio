#requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$tools=Join-Path $project '.toolchain'
[IO.Directory]::CreateDirectory($tools)|Out-Null
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
$xmlText=(Invoke-WebRequest -UseBasicParsing -Uri 'https://dl.google.com/android/repository/repository2-1.xml').Content
[xml]$repo=$xmlText
Add-Type -AssemblyName System.IO.Compression.FileSystem
$lock=@()
foreach($selection in @(@{Package='platforms;android-35';File='platform-35_r02.zip';Folder='platform'},@{Package='build-tools;35.0.0';File='build-tools_r35_windows.zip';Folder='build-tools'})){
    $found=$null
    foreach($package in $repo.SelectNodes('//*[local-name()="remotePackage"]')){
        if($package.GetAttribute('path') -ne $selection.Package){continue}
        foreach($archive in $package.SelectNodes('./*[local-name()="archives"]/*[local-name()="archive"]')){
            $complete=$archive.SelectSingleNode('./*[local-name()="complete"]')
            if($complete -and $complete.SelectSingleNode('./*[local-name()="url"]').InnerText -eq $selection.File){$found=$complete;break}
        }
        if($found){break}
    }
    if(-not $found){throw ('Google repository entry unavailable: '+$selection.Package)}
    $sha=$found.SelectSingleNode('./*[local-name()="checksum"]').InnerText.Trim()
    $archive=Join-Path $tools $selection.File
    if(-not(Test-Path -LiteralPath $archive)){
        Write-Host ('Downloading official Android '+$selection.Package)
        $client=[Net.WebClient]::new()
        try{$client.DownloadFile(('https://dl.google.com/android/repository/'+$selection.File),$archive)}finally{$client.Dispose()}
    }
    if((Get-FileHash -LiteralPath $archive -Algorithm SHA1).Hash.ToLowerInvariant() -cne $sha.ToLowerInvariant()){throw ('Google archive checksum mismatch: '+$selection.File)}
    $extract=Join-Path $tools $selection.Folder
    if(-not(Test-Path -LiteralPath $extract)){[IO.Compression.ZipFile]::ExtractToDirectory($archive,$extract)}
    $lock += [pscustomobject]@{package=$selection.Package;url=('https://dl.google.com/android/repository/'+$selection.File);sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()}
}
[IO.File]::WriteAllText((Join-Path $tools 'verified-downloads.json'),(ConvertTo-Json -InputObject @($lock)),[Text.UTF8Encoding]::new($false))
Write-Output 'Android API 35 and Build Tools 35.0.0 are verified and ready.'
