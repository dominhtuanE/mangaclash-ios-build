# MangaClash iOS (arm64) app. Two ways to get Cocos2d-x:
#  - MC_COCOS_LIB=<libcocos2d iOS.a>  : link the engine prebuilt by cocos' own Xcode project (CI does this; most reliable)
#  - otherwise                         : add the engine through its CMake files (needs an older CMake)
#   cmake -S native -B build/ios -G Xcode -DCOCOS2DX_ROOT=<cocos2d-x-3.17.1> -DMC_COCOS_LIB=<.a> -DCMAKE_SYSTEM_NAME=iOS
cmake_minimum_required(VERSION 3.10)
project(MangaClash CXX C OBJC OBJCXX)

cmake_policy(SET CMP0077 NEW)
set(CMAKE_CXX_STANDARD 14)
set(COCOS2DX_ROOT "" CACHE PATH "Cocos2d-x 3.17.1 root (with external/ downloaded)")
set(MC_COCOS_LIB "" CACHE FILEPATH "Prebuilt 'libcocos2d iOS.a'")
if(NOT COCOS2DX_ROOT)
  message(FATAL_ERROR "Set -DCOCOS2DX_ROOT=<path to cocos2d-x-3.17.1>")
endif()

file(GLOB LUA_SRC third_party/lua/*.c)
set(MC_SOURCES
  core/bundle.cpp core/host.cpp core/natives.cpp core/netbind.cpp core/net.cpp core/http.cpp core/rlog.cpp core/resmgr.cpp
  third_party/miniz/miniz.c ${LUA_SRC}
  ui/lua_node.cpp ui/images.cpp ui/text.cpp ui/scene.cpp ui/res.cpp ui/touch.cpp ui/actions.cpp ui/widgets.cpp ui/misc.cpp ui/spine.cpp ui/compat.cpp
  app/AppDelegate.cpp app/CrashReport.cpp
  ios/main.m ios/AppController.mm)

if(MC_COCOS_LIB)
  add_executable(MangaClash MACOSX_BUNDLE ${MC_SOURCES})
  target_include_directories(MangaClash PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${COCOS2DX_ROOT} ${COCOS2DX_ROOT}/cocos
    ${COCOS2DX_ROOT}/cocos/platform/ios ${COCOS2DX_ROOT}/cocos/editor-support ${COCOS2DX_ROOT}/extensions
    ${COCOS2DX_ROOT}/external ${COCOS2DX_ROOT}/external/chipmunk/include)
  # must match the defines libcocos2d.xcodeproj builds with (Node's layout depends on CC_USE_PHYSICS)
  target_compile_definitions(MangaClash PRIVATE MC_IOS=1 CC_TARGET_OS_IPHONE USE_FILE32API CC_ENABLE_CHIPMUNK_INTEGRATION=1)
  file(GLOB EXT_LIBS ${COCOS2DX_ROOT}/external/*/prebuilt/ios/*.a)
  foreach(l ${EXT_LIBS})
    if(NOT l MATCHES "spidermonkey|luajit|lua|glfw|sqlite|v8|js_static")
      list(APPEND MC_EXT ${l})
    endif()
  endforeach()
  message(STATUS "external libs: ${MC_EXT}")
  target_link_libraries(MangaClash ${MC_COCOS_LIB} ${MC_EXT} z iconv
    "-framework OpenGLES" "-framework UIKit" "-framework QuartzCore" "-framework CoreGraphics" "-framework CoreText"
    "-framework AudioToolbox" "-framework OpenAL" "-framework AVFoundation" "-framework Foundation" "-framework CFNetwork"
    "-framework GameController" "-framework MediaPlayer" "-framework SystemConfiguration" "-framework MobileCoreServices"
    "-framework CoreMotion" "-framework CoreVideo" "-framework Security")
else()
  set(COCOS2DX_ROOT_PATH ${COCOS2DX_ROOT})
  set(CMAKE_MODULE_PATH ${COCOS2DX_ROOT}/cmake/Modules/)
  set(BUILD_LUA_LIBS OFF)
  set(BUILD_JS_LIBS OFF)
  set(BUILD_EXTENSIONS OFF)
  set(BUILD_EDITOR_SPINE OFF)
  set(BUILD_EDITOR_COCOSTUDIO OFF)
  set(BUILD_EDITOR_COCOSBUILDER OFF)
  include(CocosBuildSet)
  add_subdirectory(${COCOS2DX_ROOT}/cocos ${ENGINE_BINARY_PATH}/cocos/core)
  add_executable(MangaClash MACOSX_BUNDLE ${MC_SOURCES})
  target_include_directories(MangaClash PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${COCOS2DX_ROOT} ${COCOS2DX_ROOT}/cocos ${COCOS2DX_ROOT}/cocos/platform/ios)
  target_link_libraries(MangaClash cocos2d)
  target_compile_definitions(MangaClash PRIVATE MC_IOS=1)
endif()

set_target_properties(MangaClash PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.emti.mangaclash"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "12.0"
  XCODE_ATTRIBUTE_ARCHS "arm64"
  XCODE_ATTRIBUTE_ENABLE_BITCODE "NO")
