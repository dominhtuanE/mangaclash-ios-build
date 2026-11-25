#!/bin/bash
set -uo pipefail
ROOT="$(pwd)"; ENG="$ROOT/engine"
mkdir -p out
COMMON=(-sdk iphoneos ARCHS=arm64 VALID_ARCHS=arm64 ONLY_ACTIVE_ARCH=NO IPHONEOS_DEPLOYMENT_TARGET=12.0 ENABLE_BITCODE=NO
  GCC_TREAT_WARNINGS_AS_ERRORS=NO GCC_TREAT_IMPLICIT_FUNCTION_DECLARATIONS_AS_ERRORS=NO GCC_TREAT_INCOMPATIBLE_POINTER_TYPE_WARNINGS_AS_ERRORS=NO
  CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" DEVELOPMENT_TEAM="")

# 1) engine bang xcodeproj chinh thuc cua cocos
xcodebuild -project "$ENG/build/cocos2d_libs.xcodeproj" -target "libcocos2d iOS" -configuration Release "${COMMON[@]}" \
  SYMROOT="$ROOT/build/engine" OTHER_CFLAGS='$(inherited) -Wno-error' build > out/engine.log 2>&1
RC=$?; grep -E "error:|BUILD (SUCCEEDED|FAILED)" out/engine.log | sort -u | head -40
[ $RC -eq 0 ] || exit $RC
LIB=$(find build/engine -name "libcocos2d iOS.a" | head -1); echo "engine lib: $LIB"
cp "$LIB" build/libcocos2d_ios.a

# 2) app (CMake -> Xcode)
cmake -S native -B build/ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -DCOCOS2DX_ROOT="$ENG" -DMC_COCOS_LIB="$ROOT/build/libcocos2d_ios.a" > out/cmake.log 2>&1 || { tail -60 out/cmake.log; exit 1; }
xcodebuild -project build/ios/MangaClash.xcodeproj -target MangaClash -configuration Release "${COMMON[@]}" \
  SYMROOT="$ROOT/build/products" OTHER_CFLAGS='$(inherited) -Wno-error' build > out/build.log 2>&1
RC=$?
grep -E "error:|BUILD (SUCCEEDED|FAILED)" out/build.log | sort -u | head -80
[ $RC -eq 0 ] || exit $RC
APP=$(find build/products -name "MangaClash.app" -maxdepth 3 | head -1)
echo "App: $APP"
rm -rf out/Payload && mkdir -p out/Payload && cp -R "$APP" out/Payload/
(cd out && zip -qry MangaClash_shell.ipa Payload)
ls -la out/MangaClash_shell.ipa
