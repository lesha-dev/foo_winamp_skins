# Build .fb2k-component from the VS Release builds: x86 DLL at the root, x64 DLL in x64\.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$ver = [regex]::Match((Get-Content "$root\src\version.h" -Raw), 'COMPONENT_VERSION\s+"([^"]+)"').Groups[1].Value
$stage = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid())
New-Item -ItemType Directory -Path "$stage\x64" | Out-Null
try {
    Copy-Item "$root\out\Win32\Release\foo_winamp_skins.dll" $stage
    Copy-Item "$root\out\x64\Release\foo_winamp_skins.dll" "$stage\x64"
    Copy-Item "$root\LICENSE" "$stage\LICENSE.txt"
    Copy-Item "$root\3rdParty\miniz\LICENSE" "$stage\LICENSE-miniz.txt"
    Copy-Item "$root\licenses\webamp-LICENSE.txt" "$stage\LICENSE-webamp.txt"
    $zip = "$root\out\foo_winamp_skins-$ver.zip"
    $pkg = [IO.Path]::ChangeExtension($zip, '.fb2k-component')
    Remove-Item $zip, $pkg -ErrorAction SilentlyContinue
    Compress-Archive -Path "$stage\*" -DestinationPath $zip
    Move-Item $zip $pkg
    $pkg
} finally {
    Remove-Item $stage -Recurse -Force
}
