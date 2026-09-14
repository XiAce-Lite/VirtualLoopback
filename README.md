# VirtualLoopback

Windows の PC 再生音（ブラウザ・メディアプレイヤーなど）を、**仮想ケーブルなし**で DAW のトラックに取り込むプラグインです。

- **Windows:** **WASAPI ループバック**（VB-Cable / VoiceMeeter 不要）
- **Mac:** **Core Audio Process Tap**（BlackHole / Rogue Amoeba Loopback 不要、macOS 14.2 以降）

> **対応 OS:** Windows、および macOS 14.2 以降  
> Mac 版のソースは `feature/macos-coreaudio` ブランチです。master の配布物は従来どおり Windows VST3 です。

[English README](README.en.md)

---

## 何ができるか

SYNCROOM でセッションするとき、マイク演奏と同時に「PC で鳴っている音」（例: YouTube、カラオケ音源）も相手に聞かせたい場合があります。

ループバック機能付きオーディオ IF がなくても、このプラグインを DAW に挿せば、その再生音を SYNCROOM へ送れます。

```text
[マイク / 楽器] ──► Track A ──┐
                               ├─► バス / マスター ─► syncroom_vst_bridge2 ─► SYNCROOM
[VirtualLoopback] ► Track B ──┘   （Chrome などの PC 再生音）
```

静止ファイルの再生は、SYNCROOM 本体のファイル再生で行ってください。本プラグインは **ライブの再生音** 向けです。

---

## インストール

### Windows

ビルド不要です。リポジトリの **`Release`** フォルダにある配布物を使ってください。

1. [`Release/VirtualLoopback.vst3`](Release/VirtualLoopback.vst3) を入手する  
   （GitHub の Release から zip を落とす場合は展開し、中の **`VirtualLoopback.vst3` ファイル単体** を使う。フォルダ構成のバンドルではありません）
2. その `.vst3` ファイルを **`C:\Program Files\Common Files\VST3\`** にコピーする  
   （VST3 の標準インストール先。システムドライブが C: でない場合は `%CommonProgramFiles%\VST3`）
3. **フォルダが無い場合**は、上記パスで `VST3` フォルダを自分で作成してからコピーする  
   （サードパーティの VST3 を一度も入れていない PC では、最初から存在しないことがあります。管理者権限が必要な場合があります）
4. DAW でプラグインを再スキャンする（Cubase の場合は再起動でも可）
5. メーカー **XiAceLite** / プラグイン名 **VirtualLoopback** が出ることを確認する

### Mac（VST3 / AU）

Mac 版は **バンドル**です（Windows のような単一 `.vst3` ファイルではありません）。GitHub Release の Mac 用 zip を展開して使います。

1. `VirtualLoopback.vst3` を **`~/Library/Audio/Plug-Ins/VST3/`** にコピーする  
   （Logic 以外、Cubase / SYNCROOM VST 連携など）
2. `VirtualLoopback.component` を **`~/Library/Audio/Plug-Ins/Components/`** にコピーする  
   （Logic / GarageBand など AU ホスト）
3. フォルダが無ければ作成する
4. DAW でプラグインを再スキャンする
5. 初回キャプチャ時、macOS が許可を求めたら **画面収録とシステムオーディオ** で **使っている DAW** を許可する  
   （プラグイン単体ではなく Cubase などのホスト側です。プロンプトが出ないときはシステム設定から手動で追加）

BlackHole や Loopback のインストールは不要です。仮想デバイスも追加しません。

---

## DAW への挿し方

VirtualLoopback は **音源（Instrument / プラグインシンセ）** として作っています。  
エフェクト（FX / Insert）一覧には出ないことがあります。**シンセ / インストゥルメント** としてトラックに入れてください。

### Cakewalk（Sonar）の場合

1. 空のトラックを用意する（またはインストゥルメントトラックを追加）
2. トラックの **プラグインシンセ**（Instrument）から **VirtualLoopback** を選択
3. 入力モニタが必要なら、そのトラックの再生・モニタを有効にする
4. プラグイン画面でキャプチャ対象デバイスを選び、レベルメーターが振れることを確認

※ FX ラック（インサートエフェクト）から探すと見つからないことがあります。

### Cubase の場合

#### インストール先（Cubase ユーザー向け）

Cubase では、次の場所を **VirtualLoopback の置き場所として使わないでください**。

- `C:\Program Files\Steinberg\VSTPlugins\` … **VST2 用**（Plugin Manager の VST2 パス設定に出てくるフォルダ）
- `C:\Program Files\Steinberg\Cubase 14\VST3\` … **Cubase 同梱プラグイン用**（ここに置いても動く報告はありますが、サードパーティ VST3 の正式な置き場所ではありません）

VirtualLoopback は **`C:\Program Files\Common Files\VST3\`** に置いてください。  
このフォルダが無ければ **手動で作成** します。Cubase は VST3 の検索パスをユーザーが追加できないため、ここが実質唯一の正解です。

うまく認識されないときは、**Studio → VST Plug-in Manager → Plug-in Report** で VirtualLoopback の **Path** が `Common Files\VST3` になっているか確認してください。

#### トラックへの挿し方

1. **インストゥルメントトラックを追加**（プロジェクト → トラックを追加 → インストゥルメント）
2. プラグイン選択で **VirtualLoopback** を選ぶ  
   （カテゴリは Instrument / Synth 付近。FX 一覧には出ません）
3. トラックのモニタ（または入力の聞き取り）をオンにし、音が出る／メーターが振れることを確認
4. 必要なら出力をグループチャンネルやマスターへ送り、後段で SYNCROOM 用 VST と混ぜる

### 他の DAW でも共通の考え方

- 「波形を加工するエフェクト」ではなく、「音を出す音源」として扱う
- マイク用トラックとは **別トラック** に挿し、バスやマスターで混ぜる

---

## SYNCROOM とのつなぎ方（VST 連携）

1. DAW でマイク／楽器用トラックと、VirtualLoopback 用トラックを用意する
2. 両方をバスやマスターに送る
3. マスター（または送りバス）に **syncroom_vst_bridge2** を挿し、SYNCROOM を VST 連携で起動する
4. SYNCROOM 側は通常の ASIO 設定ではなく、VST 連携状態になる（音声は DAW 経由）

DAW と SYNCROOM で同じ ASIO を同時に掴むと衝突しやすいので、この用途では **VST 連携が筋がよい**です。

---

## プラグイン画面の「再生デバイス」の選び方

ここが一番わかりにくいところです。

### Windows

VirtualLoopback は、「Windows 上で **その再生デバイスに流れている音**」を取り込みます。

### 選ぶべきもの（典型例）

Chrome や MP3 プレイヤーが普段スピーカーから鳴るとき、多くの Windows PC では次のようなデバイスです。

- **スピーカー (Realtek(R) Audio)**
- **ヘッドホン (Realtek(R) Audio)**
- ノート PC 内蔵スピーカー／イヤホン用の Realtek など

プラグインの一覧で、名前に **Realtek** と付いている再生デバイスを選べば、だいたい正解です。  
（イヤホンを挿しているときは「ヘッドホン」、外付けスピーカーなら「スピーカー」側、など状況に合わせて選ぶ）

### 選んではいけないもの

- SYNCROOM や DAW の **モニター音が戻ってくるデバイス**
- 「Yamaha SYNCROOM Driver」など、配信・モニター用の仮想ライン（用途が逆方向）

これらを選ぶと、相手の音や自分のモニターがループしてハウリング／エコーの原因になります。

### 確認方法

1. Realtek などの普段の再生デバイスを選ぶ
2. Chrome やプレイヤーで音を出す
3. プラグインの **出力レベル** メーターが振れれば OK

### Mac

既定の **「システム再生音」** を選んでください。Chrome / Music など、DAW 以外のアプリが出している再生音をまとめて取り込みます（ホスト DAW と SYNCROOM は除外します）。

特定の出力デバイス名を選ぶこともできますが、まずは「システム再生音」でメーターが振るかを確認してください。

キャプチャ中なのにメーターが動かないときは、**システム設定 → プライバシーとセキュリティ → 画面収録とシステムオーディオ** で DAW が許可されているか確認してください。許可が無いと API は成功したように見えて無音だけが来ます。

---

## ビルド（開発者向け）

- JUCE: `F:/JUCE`
- Projucer で再生成する場合:

```powershell
& F:\JUCE\Projucer.exe --resave D:\Documents\GitHub\VirtualLoopback\VirtualLoopback.jucer
```

- Visual Studio で `Builds\VisualStudio2022\VirtualLoopback.sln` を x64 ビルド（VST3 ターゲット）

MSBuild を使う場合は **amd64 版** を推奨します。

### Mac

- macOS 14.2 以降、Xcode、JUCE
- Projucer で Xcode exporter の JUCE モジュールパスを、Mac 上の JUCE に合わせてから `--resave`
- `Builds/MacOSX/VirtualLoopback.xcodeproj` を Universal（arm64 + x86_64）で VST3 / AU ビルド
- 配布前は codesign と公証（notarize）を推奨（ad-hoc 署名だとシステムオーディオ許可が不安定になりやすい）

---

## 注意事項

- Mac は **macOS 14.2 以降** が必要です（それ以前には公開の再生ミックス API がありません）
- Mac では BlackHole / Loopback / 仮想 HAL ドライバは使いません
- ファイル再生（あらかじめ用意した WAV 等）は、本プラグインの対象外です
- 初回キャプチャ開始時、内部バッファが溜まるまでごく短く無音になることがあります
