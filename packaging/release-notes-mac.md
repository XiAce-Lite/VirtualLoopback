## VirtualLoopback v1.0.1 (Mac assets)

統合リリース **v1.0.1** の Mac 配布です。Windows 用 zip も同じ Release にあります。

添付: **`VirtualLoopback-macos-universal.zip`**（VST3 + AU、arm64/x86_64）

BlackHole / Rogue Amoeba Loopback は不要です。

### インストール
1. zip を展開（直下に `VirtualLoopback.vst3` と `VirtualLoopback.component`）
2. `xattr -cr VirtualLoopback.vst3` / `xattr -cr VirtualLoopback.component`
3. `.vst3` を `~/Library/Audio/Plug-Ins/VST3/` 直下へ
4. `.component` を `~/Library/Audio/Plug-Ins/Components/` へ
5. DAW 再スキャン（インストゥルメント）
6. システム設定 → 画面収録とシステムオーディオ で DAW を許可

詳細は zip 内 `README-mac.txt` とリポジトリ README を参照。
