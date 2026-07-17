# fitom_patch_editor

FITOM_X用のオフラインパッチ/プロファイルエディタです。FITOM_X本体(音源
エンジン)の一部ではなく、FITOM_Xが使うのと同じ `*.profile.json` /
`*.patchbank.json` / `*.hwbank.json` / `*.swbank.json` /
`*.drumkit.json` / `*.samplezonebank.json` を読み書きするスタンド
アロンのC++アプリケーションで、FITOM_Xを起動していなくてもパッチの
閲覧・編集・保存ができます。GUIはFITOM_X本体と同じくDear ImGuiで
実装しています。

## 構成

大きく2つの部分から成ります。

- **`fpe_data`(JSONデータモデル/I/O層)**: プロファイルと全バンクを
  ひとまとめの閲覧・編集可能なオブジェクトグラフに束ねる
  `PatchWorkspace` クラスを中心とした静的ライブラリ。GUIを一切介さず
  単体でも使えます。
- **`fitom_patch_editor_gui`(GUIアプリケーション)**: Dear ImGui +
  GLFW + OpenGL3によるウィンドウ/描画シェル(`apps/gui/main.cpp`)。
  現時点ではウィンドウとメインループのみで、パッチブラウザ/エディタ
  本体のUIはこれから`fpe::PatchWorkspace`の上に構築します。

以下は今後の拡張として未実装です。

- パッチブラウザ/エディタのUI本体(プロファイル階層のツリー表示、
  パッチ編集フォーム、仮想MIDIコントローラなど)。
- 起動中のFITOM_X本体との名前付きパイプ/ソケット接続によるリアルタイム
  試聴機能。FITOM_X側プロジェクトの `docs/plugin-midi-pipe.md` に
  仕様があり、ワイヤーフォーマットは単純な生MIDIバイト列で、Windowsは
  `\\.\pipe\FITOM_X_MIDI`、Linux/macOSは `/tmp/fitom_x_midi.sock` に
  送信するだけなので、後から追加しやすい設計になっています。

## 依存関係

すべて [vcpkg](https://github.com/microsoft/vcpkg) のマニフェストモード
(`vcpkg.json`)で管理しています。リポジトリにサードパーティのソースを
バンドル(submodule等)する形は取っていません。

| 依存 | 用途 |
|---|---|
| `nlohmann-json` | プロファイル/バンクJSONの読み書き(`fpe_data`) |
| `imgui`(features: `glfw-binding`, `opengl3-binding`) | GUI本体 |
| `glfw3` | ウィンドウ/入力(キーボード・マウス) |
| `glew` | OpenGL関数のロード |

## ディレクトリ構成

```
vcpkg.json           依存関係マニフェスト
CMakePresets.json     vcpkgツールチェーンを使う構成プリセット
include/fpe/         公開ヘッダー(namespace fpe、データモデル)
  VoicePatchType.h     チップ系統の分類 (CC#0直接デバイス選択値 / HwBankタグ)
  HwPatch.h            HwPatch("デバイスボイスパッチ"): FM/PSGのレジスタレベル合成パラメータ
  SwPatch.h            SwPatch("パフォーマンスパッチ"): ビブラート/トレモロ/ベロシティ感度
  NativePatch.h        ToneLayer / Patch / PatchBank ("ネイティブパッチ"、通常モードCC#0=0)
  DrumKit.h            DrumNote / DrumKit (リズムチャンネル用プログラム)
  SampleZone.h         SampleZone / SampleZonePatch / SampleZoneBank (ADPCM/AWMサンプル音源)
  Profile.h            トップレベルの *.profile.json (バンクレジストリ + 未対応フィールドの保持)
  PatchWorkspace.h     プロファイル+全バンクの読み込み/保存、CRUD、閲覧ツリー
  JsonUtil.h           getOr/getRequiredヘルパー、JsonError
src/                 fpe_dataの実装 (to_json/from_json、PatchWorkspace)
apps/gui/main.cpp    GUIアプリケーションのエントリポイント(ウィンドウ/描画シェル)
tests/smoke_test.cpp fpe_dataの読み込み/CRUD/保存/再読み込みラウンドトリップ確認テスト
fixtures/            テスト用の手書きサンプルプロファイル+バンク
```

## 閲覧・編集階層

`PatchWorkspace` は、エディタUI向けに想定されている以下の階層構造を
そのまま公開します。

```
パッチプロファイル (Profile)
  + ネイティブパッチバンク (PatchBank)      -> nativePatchBanks()
  |   + ネイティブパッチ (Patch)
  |       + トーンレイヤー0..3 (ToneLayer)
  + パフォーマンスバンク (SwBank)             -> performanceBanks()
  |   + パフォーマンスパッチ (SwPatch)
  + デバイスパッチバンク (HwBank)             -> deviceBanks()
  |   + デバイスボイスパッチ (HwPatch)
  |       + [sw_bank/sw_prog 参照 -> resolvePerformancePatch()]
  + ドラムキットマップ (Profile::drum_banks)   -> drumKits()
      + ドラムキット (DrumKit)
          + ドラムノート (DrumNote)
```

「デバイスパッチバンク → デバイスボイスパッチ → パフォーマンスバンク →
パフォーマンスパッチ」は、別ストレージではなく**参照**です。`HwPatch`
の `sw_bank`/`sw_prog` が、同じ `performanceBanks()` リスト内を指し
示します。`PatchWorkspace::resolvePerformancePatch()` がUI向けにこの
参照をたどります。

サンプルベース音源(ADPCM-B/A、PCM-D8、AWM)はHwPatchの形状に一切
当てはまらないため、別系統の `sampleZoneBanks()` を持ちます。
hw_banks[]エントリの `group` がサンプルベース系の`VoicePatchType`に
解決される場合、`deviceBanks()`ではなくこちらが自動的に選ばれます。

## ビルド方法

CMake 3.20以上、C++17対応コンパイラ、[vcpkg](https://github.com/microsoft/vcpkg)
が必要です。`VCPKG_ROOT` 環境変数がvcpkgのチェックアウト先を指している
前提で、マニフェストモード(`vcpkg.json`)が依存関係を自動的に解決します。

```bash
cmake --preset vcpkg          # Linux/macOS。Windowsは --preset vcpkg-windows
cmake --build build/vcpkg
ctest --test-dir build/vcpkg --output-on-failure   # fpe_dataのテストのみ実行
./build/vcpkg/fitom_patch_editor_gui               # GUIアプリケーションを起動
```

プリセットを使わない場合は、通常のCMake手順に
`-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`
を追加してください。GUIをビルドしたくない場合は `-DFPE_BUILD_GUI=OFF`
を指定すると、`fpe_data` とテストのみがビルドされ、vcpkgに
`glfw3`/`glew`/`imgui`は要求されません。

`tests/smoke_test.cpp` は読み込み・フィールドデフォルト・全階層での
CRUD・save-as+再読み込みのラウンドトリップをチェックする、85項目の
アサーションから成ります。

## 参照元と未確認の推測箇所

すべての構造体には、FITOM_Xのどのドキュメント節から起こしたかを
コメントで注記しています(`docs/hwpatch-reference.md`、
`docs/swpatch-reference.md`、`docs/native-patch-reference.md`、
`docs/patch-structure-design.md`、`docs/config-design.md`、
`docs/voice-parameter-reference.md`、`docs/terminology.md`)。
本ライブラリはFITOM_Xの公開ドキュメントのみから実装されており、FITOM_X
本体のソースツリー自体、および実際の `profile.schema.json` /
`hwbank.schema.json` / `swbank.schema.json` / `patchbank.schema.json` /
`config_schema/drumkit.schema.json` とは未照合です。本番投入前に、
以下の2点は実際のスキーマと突き合わせて確認することをおすすめします。

1. **`profile.json` トップレベルの、ネイティブパッチバンク/パフォーマンス
   バンク用配列名。** ドキュメント内の実例(worked example)では
   `profile_name`、`hw_plugins[]`、`midi_inputs[]`のみが示されており、
   確定しているのは `hw_banks[]` と `drum_banks[]` のみです。本ライブラリ
   では、これらと同じ`{bank, file}`形状であるとの類推から
   `patch_banks[]` と `sw_banks[]` という名前を仮定しています。詳細は
   `include/fpe/Profile.h` 冒頭のコメントを参照してください。
2. **"routed"形式のドラムキットにおける、1ノートあたりのフィールド構成。**
   ドキュメントが完全なJSON例を示しているのは"direct"形式のみで、
   `include/fpe/DrumKit.h` の `DrumNote`("routed"形式の`notes[]`要素)は
   `DrumNote.voicePatchType`/`.playNote`/`.sw_bank`/`.sw_prog`への文章上の
   言及から再構成したものであり、実例からの確認ではありません。

いずれも該当箇所に `NOTE ON CONFIDENCE` というコメントを付けています。
実際の `profile.schema.json` / `drumkit.schema.json` と食い違っていた
場合も、修正対象は1ファイルずつ(`Profile.h`/`.cpp`、
`DrumKit.h`/`.cpp`)で済み、ライブラリの他の部分はこれらの厳密な
配列名・フィールド名には依存していません。

それ以外(HwPatch、SwPatch、ToneLayer/Patch、SampleZone、
VoicePatchType)は、ドキュメント内の具体的なJSON例と突き合わせて
確認済みです。

## 設計上のポイント

- **読み込みは緩く、書き込みは明示的に。** 各フィールドはドキュメントに
  記載のデフォルト値を持ち(`sw_bank`/`sw_prog` = -1、`enabled` =
  true、`note_range` = 0-127 など)、JSON中でキーが欠落していても
  エラーにせずデフォルトにフォールバックします。これはFITOM_X自体の
  「ソフトな失敗」という設計方針に合わせたものです。書き込み時は常に
  正規のフィールド一式をすべて出力します(元のファイルがどれだけ簡潔に
  手書きされていたかを再現しようとはしていません)。
- **profile.jsonの未対応フィールドは保持されます。** `Profile::extra`
  が、本ライブラリがモデル化していないトップレベルキー
  (`hw_plugins`、`midi_inputs`、`midi_backend`、`psg_fallback_chip`、
  `devices` など)をすべて保持し、保存時にマージし直すため、パッチを
  編集しても実行時設定が黙って失われることはありません。
- **`PatchWorkspace::saveAs()` は `profile.json` だけでなくツリー全体を
  再配置します。** 読み込み済みの全バンク/キットのファイルパスを、
  元のプロファイルディレクトリからの相対パスを保ったまま新しい
  ディレクトリ配下に再配置するため、「名前を付けて保存」で
  自己完結したコピーが作られます。
- **GUIバックエンドはGLFW + OpenGL3。** タッチパネル操作の要件に対して
  はSDL系(明示的なマルチタッチイベントを持つ)の方が本来向いています
  が、vcpkgのimguiポートは現在SDL2向けバインディングを提供しておらず
  (SDL3向けの`sdl3-binding`/`sdl3-renderer-binding`に統合済み)、vcpkg
  だけで完結させる方針からGLFW + OpenGL3を採用しています。GLFW自体は
  ネイティブのマルチタッチAPIを持たず、OSのマウスエミュレーションに
  依存する点に注意してください。
