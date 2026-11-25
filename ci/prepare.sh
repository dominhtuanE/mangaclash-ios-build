#!/bin/bash
set -euo pipefail
git clone --depth 1 --branch "$COCOS_TAG" https://github.com/cocos2d/cocos2d-x.git engine
cd engine
python3 -m pip install --quiet --break-system-packages setuptools || python3 -m pip install --quiet setuptools
# download-deps.py: tai ~140 MB thu vien ngoai (prebuilt iOS) vao external/
python3 download-deps.py --remove-download=yes
ls external | head -30

# Toolchain cu cua cocos 3.17.1 vs CMake 4.x: exec_program da bi go (CMP0153) -> execute_process.
TC=cmake/ios.toolchain.cmake
sed -i '' 's|exec_program(uname ARGS -r OUTPUT_VARIABLE CMAKE_HOST_SYSTEM_VERSION)|execute_process(COMMAND uname -r OUTPUT_VARIABLE CMAKE_HOST_SYSTEM_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)|' $TC
sed -n 40,48p $TC
