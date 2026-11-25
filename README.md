# MangaClash iOS shell (CI)
Repo chi chua ma nguon native (C++/ObjC, Cocos2d-x 3.17.1 + Lua) va workflow build. Khong co du lieu game, khong co IP server.
1. `python sync_native.py` roi push len GitHub (repo public = Mac mien phi).
2. Actions -> build-ios-shell -> Run workflow; tai artifact `mangaclash-ios-shell` (MangaClash_shell.ipa) hoac `build-log` neu loi.
3. Tren Windows: `python tools/make_ios_ipa.py <shell.zip|.ipa> <server-dir> [--url http://HOST:PORT/]` -> dist/MangaClash_iOS_arm64.ipa, roi ky bang Sideloadly.
