## VirtualLoopback v1.1.0

Windows + Mac の統合リリースです。`master` にアプリ単位キャプチャを入れました。

### 変更点
- **アプリ単位キャプチャ**を追加。既定は従来どおり **システム再生音**（全体ループバック）
- 一覧の **アプリ: …** で個別アプリをキャプチャ
- **Windows:** Application Loopback（Windows 10 version 2004 / ビルド 19041 以降、Windows 11）
- **Mac:** Core Audio Process Tap のプロセス指定（macOS 14.2+）
- 保存したアプリ指定は、対象が起動していなくても既定に戻さない（`アプリ: … （未起動）` として保持）
- **追加プロセス…（Windows）:** オーディオセッションが無いアプリも、プロセス名を登録すれば候補に出せる
- 同じトラックにこのプラグインを複数挿すと、**最後のインスタンスだけ**が有効（前段の入力は捨てる仕様）。個別アプリはトラックを分ける

### Windows
添付: **`VirtualLoopback.vst3`** または **`VirtualLoopback-windows-x64.zip`**

迷ったら **`VirtualLoopback.vst3` を直接ダウンロード** し、`C:\Program Files\Common Files\VST3\` にコピーしてください。zip は展開して中の `VirtualLoopback.vst3`（単一ファイル）を同じ場所へ置きます。

### Mac
添付: **`VirtualLoopback-macos-universal.zip`**（Actions のビルド完了後に付きます）
1. 展開（直下に `VirtualLoopback.vst3` / `VirtualLoopback.component`）
2. `xattr -cr VirtualLoopback.vst3 VirtualLoopback.component`
3. `.vst3` を `~/Library/Audio/Plug-Ins/VST3/` **直下**へ（入れ子にしない）
4. `.component` を `~/Library/Audio/Plug-Ins/Components/` へ
5. 再スキャン。初回は「画面収録とシステムオーディオ」で **DAW** を許可

Mac の公証はしていません。詳細は README を参照。
