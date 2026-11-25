#!/bin/bash
set -euo pipefail
git clone --depth 1 --branch "$COCOS_TAG" https://github.com/cocos2d/cocos2d-x.git engine
cd engine
# download-deps.py: tai ~140 MB thu vien ngoai (prebuilt iOS) vao external/
python3 download-deps.py --remove-download=yes
ls external | head -30
