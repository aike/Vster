# Vster

VST3プラグインをスタンドアロンアプリのように起動するための小さなホストアプリ(Windows x64)。
DAWを立ち上げずに、VSTインストゥルメントをすぐ鳴らすことが目的です。

## 機能

- VST Instrument ×1 + インサートVST Effect ×3 の固定直列チェーン(Inst → FX1 → FX2 → FX3 → Master)
- ASIO / WASAPI 出力
- ホストBPM設定 — テンポ同期するプラグイン(ディレイ、LFO、アルペジエータ等)はこのBPMに追従します。Play/Stopトグル付き(Playで小節頭から再スタート)
- ソフトキーボード + ハードウェアMIDIキーボード入力
- セッションSave/Load(.vster) — スロット構成・各プラグインのパラメータ状態・BPM・マスター音量を保存
- マスターボリューム / MUTE / ピークメーター
- クラッシュ対策の自動保存(2分毎 + プラグインロード前)と起動時リカバリ
- スキャン中にクラッシュしたプラグインの自動ブラックリスト

シーケンサーはありません。トラックは1本だけです。MIDIはInstrumentスロットにのみ送られます
(FXスロットのボコーダ・アルペジエータ系プラグインは音が出ません)。

## ビルド方法

必要なもの: Visual Studio 2022(C++ワークロード)、CMake 3.22+、git

```
git clone <this repo>
cd vster
git submodule update --init    # external/JUCE
```

### ASIO SDK(任意、ただし推奨)

SteinbergのライセンスによりASIO SDKは**このリポジトリに同梱できません**。各自で取得してください:

1. https://www.steinberg.net/developers/ から ASIO SDK をダウンロード(無償、ライセンス同意が必要)
2. `sdk/asiosdk/common/iasiodrv.h` が存在するように展開する
   (別の場所に置く場合は `-DASIOSDK_DIR=<path>` を指定)

SDKが見つからない場合は警告が出て、WASAPIのみでビルドされます(`-DVSTER_ENABLE_ASIO=OFF` で明示的に無効化も可能)。

### ビルド

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

実行ファイル: `build/Vster_artefacts/Release/Vster.exe`

## 既知の制限・注意点

- **64bit VST3のみ**対応。32bitプラグインとVST2(.dll)はロードできません。
- プラグインはアプリと同一プロセスで動くため、**プラグインがクラッシュするとVsterごと落ちます**。
  自動保存からのリカバリで被害を最小化しています。スキャン中のクラッシュは次回起動時に自動でブラックリストされます。
- プラグインディレイ補償(PDC)はありません。ルックアヘッド系プラグインのレイテンシは
  そのまま加算されます(合計値を上部ステータスに表示)。
- ASIOドライバは排他的なことが多く、DAW等が使用中だとデバイスを開けない場合があります。
- 一部のVST3エディタはHiDPI環境で表示が乱れることがあります。

## ライセンス

Vster本体は **GNU AGPLv3** で公開されています(`LICENSE` 参照)。

- [JUCE](https://juce.com) — AGPLv3(`external/JUCE` サブモジュール)
- VST3ホスティングはJUCEに同梱のVST3インターフェースヘッダ(GPLv3)を使用
- ASIO SDKは再配布禁止のため含まれていません(ビルド時に各自取得)

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.
ASIO is a trademark and software of Steinberg Media Technologies GmbH.
