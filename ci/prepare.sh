#!/bin/bash
set -euo pipefail
git clone --depth 1 --branch "$COCOS_TAG" https://github.com/cocos2d/cocos2d-x.git engine
# download-deps.py cua cocos doi python 2 -> tai thang goi thu vien ngoai (v3-deps-153, ~140 MB)
curl -sSL -o /tmp/deps.zip https://github.com/cocos2d/cocos2d-x-3rd-party-libs-bin/archive/v3-deps-153.zip
unzip -q /tmp/deps.zip -d /tmp/deps
D=$(ls -d /tmp/deps/*/ | head -1)
cp -R "$D". engine/external/
ls engine/external | head -40
ls engine/external/png/prebuilt/ios || true
