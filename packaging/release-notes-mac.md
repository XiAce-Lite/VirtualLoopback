## VirtualLoopback v1.1.0-mac

macOS 向け Core Audio Process Tap の配布 zip です。`master` / Windows の v1.0.0 とは別ブランチ `feature/macos-coreaudio` から出しています。

添付: **`VirtualLoopback-macos-universal.zip`**（VST3 + AU、arm64/x86_64 Universal）

BlackHole および Rogue Amoeba Loopback は使いません。仮想 HAL ドライバもインストールしません。

### 対応
- macOS 14.2 以降
- VST3 / AU（インストゥルメント）
- Apple Silicon と Intel

### インストール（VST3 が出ない報告への注意）
1. zip を展開する（直下に `VirtualLoopback.vst3` と `VirtualLoopback.component` があること）
2. 隔離属性を外す:
   ```
   xattr -cr VirtualLoopback.vst3
   xattr -cr VirtualLoopback.component
   ```
3. **`.vst3` パッケージそのもの**を `~/Library/Audio/Plug-Ins/VST3/` の直下へコピー  
   （外側フォルダごと入れない。最終パスは  
   `.../VST3/VirtualLoopback.vst3/Contents/MacOS/VirtualLoopback`）
4. `.component` を `~/Library/Audio/Plug-Ins/Components/` へコピー
5. DAW を再スキャンし、**インストゥルメント**として挿す
6. **システム設定 → プライバシーとセキュリティ → 画面収録とシステムオーディオ** で DAW を許可

Mac の VST3 は Windows のような単一ファイルではなく **フォルダ（バンドル）** です。中身だけコピーすると認識されません。

公証はしていません。Gatekeeper / 隔離属性が残っていると、AU は見えて VST3 だけ落ちることがあります。詳細は zip 内 `README-mac.txt`。

### 注意
- プラグイン画面の既定は「システム再生音」（DAW / SYNCROOM は除外）
- 許可が無いと API は成功しても無音だけが来ることがあります
- Windows 版は従来どおり v1.0.0 を使ってください
