# MangaClash iOS (arm64) build. Run on macOS, see BUILD_IOS.md.
#   cmake -S native -B build-ios -G Xcode -DCOCOS2DX_ROOT=<cocos2d-x-3.17.1> -DCMAKE_TOOLCHAIN_FILE=<cocos>/cmake/ios.toolchain.cmake -DIOS_PLATFORM=OS
cmake_minimum_required(VERSION 3.10)
project(MangaClash CXX C OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 14)
set(COCOS2DX_ROOT "" CACHE PATH "Cocos2d-x 3.17.1 root (with external/ downloaded via download-deps.py)")
if(NOT COCOS2DX_ROOT)
  message(FATAL_ERROR "Set -DCOCOS2DX_ROOT=<path to cocos2d-x-3.17.1>")
endif()

set(CMAKE_MODULE_PATH ${COCOS2DX_ROOT}/cmake/Modules/)
set(BUILD_LUA_LIBS OFF)
set(BUILD_JS_LIBS OFF)
set(BUILD_EXTENSIONS OFF)
set(BUILD_EDITOR_SPINE OFF)
set(BUILD_EDITOR_COCOSTUDIO OFF)
set(BUILD_EDITOR_COCOSBUILDER OFF)
include(CocosBuildSet)
add_subdirectory(${COCOS2DX_ROOT}/cocos ${ENGINE_BINARY_PATH}/cocos/core)

file(GLOB LUA_SRC third_party/lua/*.c)
set(MC_SOURCES
  core/bundle.cpp core/host.cpp core/natives.cpp core/netbind.cpp core/net.cpp core/http.cpp
  third_party/miniz/miniz.c ${LUA_SRC}
  ui/lua_node.cpp ui/images.cpp ui/text.cpp
  app/AppDelegate.cpp
  ios/main.m ios/AppController.mm)

add_executable(MangaClash MACOSX_BUNDLE ${MC_SOURCES})
target_include_directories(MangaClash PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${COCOS2DX_ROOT} ${COCOS2DX_ROOT}/cocos ${COCOS2DX_ROOT}/cocos/platform/ios)
target_link_libraries(MangaClash cocos2d)
target_compile_definitions(MangaClash PRIVATE MC_IOS=1)
set_target_properties(MangaClash PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.emti.mangaclash"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "12.0"
  XCODE_ATTRIBUTE_ARCHS "arm64"
  XCODE_ATTRIBUTE_ENABLE_BITCODE "NO")

# Game data: resource/ (from the server's res folders) + config.cnf + zhunyuan.ttf are copied into the .app by tools/pack_ios_resources.py.
