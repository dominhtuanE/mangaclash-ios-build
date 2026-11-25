# Tao repo public mangaclash-ios-build, dat secret, day len va chay workflow. Chay: powershell -File push_and_build.ps1
$ErrorActionPreference = 'Stop'
$gh = Join-Path $env:LOCALAPPDATA 'gh\bin\gh.exe'
Set-Location $PSScriptRoot
python sync_native.py
$user = (& $gh api user --jq .login).Trim()
if (-not (Test-Path .git)) { git init -b main | Out-Null }
git config user.name $user; git config user.email "$user@users.noreply.github.com"
git add -A; git diff --cached --quiet
if ($LASTEXITCODE -ne 0) { git commit -q -m "MangaClash iOS shell build" }
& $gh repo view "$user/mangaclash-ios-build" --json name 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) { & $gh repo create mangaclash-ios-build --public --source . --remote origin --push } else { git remote remove origin 2>$null; git remote add origin "https://github.com/$user/mangaclash-ios-build.git"; git push -u origin main }
$pw = Read-Host 'Mat khau goi script (MC_SCRIPT_PASSWORD)'
$pw | & $gh secret set MC_SCRIPT_PASSWORD --repo "$user/mangaclash-ios-build"
& $gh workflow run build-ios.yml --repo "$user/mangaclash-ios-build" --ref main
& $gh run list --repo "$user/mangaclash-ios-build" --limit 3
