$DIR = "group2-windows-x86_64"
$STAGE = "staging/$DIR"

if (Test-Path staging) { Remove-Item -Recurse -Force staging }
New-Item -ItemType Directory -Force -Path $STAGE | Out-Null

Copy-Item "build/release/group2.exe"  "$STAGE/"
Copy-Item "build/release/server.exe"  "$STAGE/"
Copy-Item "build/release/config.toml" "$STAGE/"
Copy-Item -Recurse "build/release/shaders"     "$STAGE/shaders"
Copy-Item -Recurse "build/release/shaders-new"  "$STAGE/shaders-new"
Copy-Item -Recurse "assets" "$STAGE/assets"

Compress-Archive -Path "staging/$DIR" -DestinationPath "$DIR.zip" -Force

Remove-Item -Recurse -Force staging

Write-Host "Created $DIR.zip"
