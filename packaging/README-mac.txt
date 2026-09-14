VirtualLoopback for Mac (Core Audio Process Tap)
================================================

macOS 14.2 以降 / Apple Silicon と Intel の Universal ビルドです。
BlackHole や Rogue Amoeba Loopback は不要です。仮想オーディオデバイスも入れません。

この zip は GitHub Actions の macOS ランナーでビルドしています。
Apple Developer 証明書での公証はしていません。

インストール
------------
1. zip を展開する

2. ターミナルで、展開したフォルダに対して隔離属性を外す（重要）:

   xattr -cr VirtualLoopback.vst3
   xattr -cr VirtualLoopback.component

   Finder から「開く」を拒否されたときも、このコマンドを先に実行してください。

3. VST3（Cubase / SYNCROOM VST 連携など）:
   VirtualLoopback.vst3 を ~/Library/Audio/Plug-Ins/VST3/ へコピー

4. AU（Logic など）:
   VirtualLoopback.component を ~/Library/Audio/Plug-Ins/Components/ へコピー

5. DAW でプラグインを再スキャンし、インストゥルメント / シンセとして挿す

6. 初回キャプチャ時:
   システム設定 → プライバシーとセキュリティ → 画面収録とシステムオーディオ
   で、使っている DAW（Cubase など）を許可する
   ※プラグイン単体ではなく、ホスト DAW 側です

使い方
------
プラグイン画面の既定「システム再生音」のまま、Chrome や Music で音を出して
出力レベルメーターが振れるか確認してください。

問題が出たら
------------
- キャプチャ中なのに無音: DAW のシステムオーディオ収録許可を確認
- プラグインが出ない: 上記のコピー先と、インストゥルメント一覧を確認
- ハウリング: DAW / SYNCROOM のモニター戻りをタップしていないか確認
