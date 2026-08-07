# VirtualLoopback

Windows の PC 再生音（ブラウザ・メディアプレイヤーなど）を、**仮想ケーブルなし**で DAW のトラックに取り込む VST3 です。

内部では Windows の **WASAPI ループバック**を使っています。VB-Cable / VoiceMeeter は不要です。

> **対応 OS:** Windows のみ（Mac 非対応）

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

ビルド不要です。リポジトリの **`Release`** フォルダにある配布物を使ってください。

1. [`Release/VirtualLoopback.vst3`](Release/VirtualLoopback.vst3) を入手する  
   （GitHub の Release から zip を落とす場合は展開する）
2. 次のどちらかにコピーする
   - `C:\Program Files\Common Files\VST3\`
   - または DAW が参照している VST3 フォルダ
3. DAW でプラグインを再スキャンする
4. メーカー **XiAceLite** / プラグイン名 **VirtualLoopback** が出ることを確認する

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

1. **インストゥルメントトラックを追加**（プロジェクト → トラックを追加 → インストゥルメント）
2. プラグイン選択で **VirtualLoopback** を選ぶ  
   （カテゴリは Instrument / Synth 付近）
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

---

## ビルド（開発者向け）

- JUCE: `F:/JUCE`
- Projucer で再生成する場合:

```powershell
& F:\JUCE\Projucer.exe --resave D:\Documents\GitHub\VirtualLoopback\VirtualLoopback.jucer
```

- Visual Studio で `Builds\VisualStudio2022\VirtualLoopback.sln` を x64 ビルド（VST3 ターゲット）

MSBuild を使う場合は **amd64 版** を推奨します。

---

## 注意事項

- **Windows 専用**です。Mac では動作しません
- ファイル再生（あらかじめ用意した WAV 等）は、本プラグインの対象外です
- 初回キャプチャ開始時、内部バッファが溜まるまでごく短く無音になることがあります
