## VirtualLoopback v1.1.0-pre

アプリ単位キャプチャのプレビューです。`master` / **v1.0.1** にはマージしていません（ブランチ `feature/per-app-capture`）。

### 今回の追加（Windows）
- **追加プロセス…**: オーディオセッションが無いアプリも、プロセス名の allowlist でキャプチャ候補に出せます
- 候補はウィンドウのあるアプリ寄りに絞り込み（SYNCROOM / SYNCROOM2 は非表示）
- 名前が取れないセッションは一覧に出さない
- 保存先: `%AppData%\VirtualLoopback\app_process_allowlist.txt`

### 変更点（共通・既存）
- **既定:** システム再生音（従来どおりの全体ループバック）
- **追加:** 一覧の `アプリ: …` で個別アプリをキャプチャ
- **Windows:** Application Loopback（Win10 2004 / ビルド 19041 以降、Win11）
- **Mac:** Core Audio Process Tap のプロセス指定（macOS 14.2+）
- 保存したアプリ指定は、対象が居なくても **既定に戻さない**（`アプリ: … （未起動）` として保持）

### Mac について
今回の機能追加は **Windows のみ**です。Mac の挙動変更はありませんが、タグ整合のため Universal zip は再ビルドして添付します。

### ダウンロードの選び方

#### Windows
| ファイル | 向いている人 |
|---|---|
| **`VirtualLoopback.vst3`** | 手早く入れたい人。**これ1つ**を `C:\Program Files\Common Files\VST3\` にコピー |
| `VirtualLoopback-windows-x64.zip` | zip で保管したい人。展開して中の `VirtualLoopback.vst3` を同じ場所へ |

> 迷ったら **`VirtualLoopback.vst3` を直接ダウンロード** で OK です。

#### Mac
- **`VirtualLoopback-macos-universal.zip`** を展開し、`VirtualLoopback.vst3` を `~/Library/Audio/Plug-Ins/VST3/` へ（AU は `.component` を Components へ）
- 展開後: `xattr -cr VirtualLoopback.vst3`（AU も使う場合は `.component` も）

### 注意
- pre-release です。本番利用は v1.0.1 を推奨
- 複数アプリを1トラックで混ぜる機能はありません（トラックを分けてください）
