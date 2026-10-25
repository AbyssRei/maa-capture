# Resolve the current stable JNA release without pinning a jar in source control.
$ErrorActionPreference = 'Stop'
$repository = 'https://repo.maven.apache.org/maven2/net/java/dev/jna/jna'
[xml]$metadata = (Invoke-WebRequest "$repository/maven-metadata.xml").Content
$version = [string]$metadata.metadata.versioning.release
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Maven Central did not return a stable JNA release: $version"
}
$destination = Join-Path $PSScriptRoot '.deps'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$jar = Join-Path $destination 'jna.jar'
Invoke-WebRequest "$repository/$version/jna-$version.jar" -OutFile $jar
$expected = ((Invoke-WebRequest "$repository/$version/jna-$version.jar.sha1").Content).Trim().Split(' ')[0]
$actual = (Get-FileHash -LiteralPath $jar -Algorithm SHA1).Hash
if ($actual -ine $expected) { throw 'JNA SHA-1 verification failed' }
Write-Host "JNA $version -> $jar"
