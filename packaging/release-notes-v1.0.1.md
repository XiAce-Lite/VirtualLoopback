## VirtualLoopback v1.0.1

Windows + Mac の統合リリースです。

### 変更点
- **Mac 対応**（macOS 14.2+）: Core Audio Process Tap（BlackHole / Loopback 不要）
- Mac 配布: VST3 + AU Universal（arm64 / x86_64）
- プラグイン出力レベルメーターを **dB スケール**（DAW トラックメーターに近い見え方）に変更
- Mac VST3 認識まわり: Bundle ID 分離、フラット zip、インストール手順の明確化

### Windows
添付: **`VirtualLoopback-windows-x64.zip`**
1. 展開し、`VirtualLoopback.vst3`（単一ファイル）を `C:\Program Files\Common Files\VST3\` へコピー
2. DAW で再スキャンし、インストゥルメントとして挿す

### Mac
添付: **`VirtualLoopback-macos-universal.zip`**
1. 展開（直下に `VirtualLoopback.vst3` / `VirtualLoopback.component`）
2. `xattr -cr VirtualLoopback.vst3 VirtualLoopback.component`
3. `.vst3` を `~/Library/Audio/Plug-Ins/VST3/` **直下**へ（入れ子にしない）
4. `.component` を `~/Library/Audio/Plug-Ins/Components/` へ
5. 再スキャン。初回は「画面収録とシステムオーディオ」で **DAW** を許可

Mac の公証はしていません。詳細は各 zip 内／README を参照。
