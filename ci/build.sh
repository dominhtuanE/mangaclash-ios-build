#!/bin/bash
set -uo pipefail
ROOT="$(pwd)"; ENG="$ROOT/engine"
mkdir -p out
{
cmake -S native -B build/ios -G Xcode \
  -DCOCOS2DX_ROOT="$ENG" \
  -DCMAKE_TOOLCHAIN_FILE="$ENG/cmake/ios.toolchain.cmake" \
  -DIOS_PLATFORM=OS -DCMAKE_OSX_ARCHITECTURES=arm64
} > out/cmake.log 2>&1 || { tail -60 out/cmake.log; exit 1; }
xcodebuild -project build/ios/MangaClash.xcodeproj -target MangaClash -configuration Release -sdk iphoneos \
  SYMROOT="$ROOT/build/products" ARCHS=arm64 VALID_ARCHS=arm64 ONLY_ACTIVE_ARCH=NO \
  IPHONEOS_DEPLOYMENT_TARGET=12.0 ENABLE_BITCODE=NO GCC_TREAT_WARNINGS_AS_ERRORS=NO \
  OTHER_CFLAGS='$(inherited) -Wno-error -Wno-deprecated-declarations' \
  OTHER_CPLUSPLUSFLAGS='$(inherited) -Wno-error -Wno-deprecated-declarations' \
  CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" DEVELOPMENT_TEAM="" \
  build > out/build.log 2>&1
RC=$?
grep -E "error:|BUILD (SUCCEEDED|FAILED)" out/build.log | sort -u | head -80
[ $RC -eq 0 ] || exit $RC
APP=$(find build/products -name "MangaClash.app" -maxdepth 3 | head -1)
echo "App: $APP"
rm -rf out/Payload && mkdir -p out/Payload && cp -R "$APP" out/Payload/
(cd out && zip -qry MangaClash_shell.ipa Payload)
ls -la out/MangaClash_shell.ipa
