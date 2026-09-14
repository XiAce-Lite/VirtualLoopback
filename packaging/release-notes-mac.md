## VirtualLoopback v1.1.0-mac

macOS 向け Core Audio Process Tap の配布 zip です。`master` / Windows の v1.0.0 とは別ブランチ `feature/macos-coreaudio` から出しています。

添付: **`VirtualLoopback-macos-universal.zip`**（VST3 + AU、arm64/x86_64 Universal）

BlackHole および Rogue Amoeba Loopback は使いません。仮想 HAL ドライバもインストールしません。

### 対応
- macOS 14.2 以降
- VST3 / AU（インストゥルメント）
- Apple Silicon と Intel

### インストール
1. `VirtualLoopback-macos-universal.zip` を展開する
2. ターミナルで隔離属性を外す:
   `xattr -cr VirtualLoopback.vst3`
   `xattr -cr VirtualLoopback.component`
3. `VirtualLoopback.vst3` を `~/Library/Audio/Plug-Ins/VST3/` へコピー
4. `VirtualLoopback.component` を `~/Library/Audio/Plug-Ins/Components/` へコピー
5. DAW を再スキャンし、インストゥルメントとして挿す
6. **システム設定 → プライバシーとセキュリティ → 画面収録とシステムオーディオ** で、使っている DAW を許可する

公証はしていません。Gatekeeper に止められたら、上記の `xattr -cr` を先に実行してください。詳細は zip 内の `README-mac.txt` を参照。

### 注意
- プラグイン画面の既定は「システム再生音」（DAW / SYNCROOM は除外）
- 許可が無いと API は成功しても無音だけが来ることがあります
- Windows 版は従来どおり v1.0.0 を使ってください
