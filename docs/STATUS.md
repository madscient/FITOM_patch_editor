# 実装状況

現在の完成度と、次にやるべきことをまとめたもの。セッションを終える
ときは、必ず末尾の「進捗ログ」に追記してからコミットすること
(`CLAUDE.md`「複数マシン開発のためのルール」参照)。

## 完成したファイル一覧

### データモデル / JSON I/O層 (`fpe_data`)

| ファイル | 状態 | 内容 |
|---|---|---|
| `include/fpe/VoicePatchType.h` / `src/VoicePatchType.cpp` | ✅ | チップ系統の分類、CC#0直接デバイス選択値との対応 |
| `include/fpe/HwPatch.h` / `src/HwPatch.cpp` | ✅ | HwPatch(デバイスボイスパッチ)、HwBank |
| `include/fpe/SwPatch.h` / `src/SwPatch.cpp` | ✅ | SwPatch(パフォーマンスパッチ)、SwBank |
| `include/fpe/NativePatch.h` / `src/NativePatch.cpp` | ✅ | ToneLayer / Patch(ネイティブパッチ) / PatchBank |
| `include/fpe/DrumKit.h` / `src/DrumKit.cpp` | ✅(一部推測、D-002参照) | DrumNote / DrumKit |
| `include/fpe/SampleZone.h` / `src/SampleZone.cpp` | ✅ | SampleZone / SampleZonePatch / SampleZoneBank (ADPCM/AWM) |
| `include/fpe/Profile.h` / `src/Profile.cpp` | ✅(一部推測、D-002参照) | 最上位の *.profile.json |
| `include/fpe/PatchWorkspace.h` / `src/PatchWorkspace.cpp` | ✅ | 読み込み/保存/CRUD/閲覧ツリーの統合クラス |
| `include/fpe/JsonUtil.h` | ✅ | getOr/getRequiredヘルパー、JsonError |
| `tests/smoke_test.cpp` | ✅ | 85項目のアサーション、クリーンビルドで全通過確認済み |
| `fixtures/*` | ✅ | テスト用サンプルプロファイル一式 |

### GUI (`fitom_patch_editor_gui`)

| ファイル | 状態 | 内容 |
|---|---|---|
| `apps/gui/main.cpp` | 🚧 プレースホルダ | GLFW+OpenGL3+Dear ImGuiのウィンドウ/描画ループのみ。パッチブラウザ/エディタのUI本体は未着手 |

### ビルド・依存関係設定

| ファイル | 状態 |
|---|---|
| `vcpkg.json` | ✅ |
| `CMakeLists.txt` | ✅ |
| `CMakePresets.json` | ✅(`vcpkg` / `vcpkg-windows` / `vcpkg-windows-vs2026`) |

## 既知の未対応・将来課題

- **パッチブラウザ/エディタのUI本体** — プロファイル階層のツリー
  表示、パッチ編集フォーム(パラメータのつまみ/スライダー等)、
  仮想MIDIコントローラ(ノート番号・ベロシティ・モジュレーション
  デプス等)、バンク/パッチ/プロファイルの新規作成・複製・削除UI。
  `fpe::PatchWorkspace` のAPIは揃っているので、これを呼ぶUIを
  `apps/gui/main.cpp` (または分割した複数ファイル) に実装していく。
- **FITOM_X本体との名前付きパイプ/ソケット通信(試聴機能)** —
  未着手。ワイヤーフォーマットはFITOM_X側ドキュメント
  `docs/plugin-midi-pipe.md` に既に仕様がある(生MIDIバイト列、
  Windows: `\\.\pipe\FITOM_X_MIDI`、Linux/macOS:
  `/tmp/fitom_x_midi.sock`)。FITOM_Xが起動していない場合はオフライン
  動作にフォールバックする設計が既に前提。
- **`docs/DESIGN.md` D-002の推測箇所の実スキーマ照合** — FITOM_X本体の
  実リポジトリ(`profile.schema.json` 等)にアクセスできる状況になり
  次第、`patch_banks[]`/`sw_banks[]`の配列名と、"routed"ドラムキット
  の`notes[]`要素のフィールド構成を確認・必要なら修正する。
- **`find_package(imgui CONFIG REQUIRED)` 等、実際のvcpkgでのビルドが
  未検証** — 開発環境の制約上、apt取得のヘッダ/ライブラリと
  vcpkgが実際に使うのと同タグ(v1.92.8)のimguiソースをその場で
  ビルドして代用検証した(コンパイル・リンクは成功、ディスプレイが
  ない環境のため実際のウィンドウ表示は未確認)。実際に
  `cmake --preset vcpkg-windows` 等でvcpkg経由のビルドが通ることは
  未検証。**次にWindows環境等で作業する際、最初にこれを確認すること。**

## 進捗ログ

新しいセッションを終えるたびに、下記フォーマットで追記する
(古いエントリは書き換えない)。

```
### YYYY-MM-DD (メモ、任意)
- やったこと: ...
- 未完了・既知の問題: ...
- 次にやること: ...
```

### 2026-07-17 (Cowork、データモデル層構築)
- やったこと: FITOM_X側の公開ドキュメントを読み込み、`fpe_data`
  (JSONデータモデル/I/O層)を新規C++プロジェクトとして設計・実装。
  CMake + vcpkg以前の段階ではFetchContentでnlohmann/jsonを取得する
  構成でスタートし、スモークテスト(85項目)を作成、サンドボックス内で
  クリーンビルド・全項目通過を確認。ユーザーのローカルリポジトリ
  (`FITOM_patch_editor`)に接続し、成果物一式を配置。
- 未完了・既知の問題: GUI・MIDIパイプ通信は未着手(意図的にスコープ外)。
  `profile.json`のバンク配列名・ドラムキットの一部フィールドは推測。
- 次にやること: GUI実装に着手。

### 2026-07-17 (Cowork、GUIシェル + 依存関係整備)
- やったこと: GUIをDear ImGui(FITOM_X本体と同じ)で実装する方針を
  確認。依存関係(nlohmann-json/imgui/glfw3/glew)をvcpkgマニフェスト
  モードに統一(git submoduleは環境要因で断念、D-006参照)。ImGuiの
  ウィンドウ/入力バックエンドをGLFW + OpenGL3に決定(D-004参照)。
  `apps/gui/main.cpp`(ウィンドウ/描画ループのシェルのみ)を追加し、
  サンドボックス内でapt取得のライブラリ+同タグのimguiソースを使って
  コンパイル・リンクを検証(実行時はディスプレイがなく未確認)。
  その後、Visual Studio 2026対応の`vcpkg-windows-vs2026`プリセットを
  `CMakePresets.json`に追加(D-007参照)。この過程でネットワーク
  マウントされたドライブ特有のファイル破損・git破損を複数回観測し、
  `docs/DESIGN.md`に注意点として記録。
- 未完了・既知の問題: パッチブラウザ/エディタのUI本体は未着手。
  実際のvcpkg経由でのビルドは未検証(上記「既知の未対応・将来課題」
  参照)。
- 次にやること: 実マシン(Windows、vcpkg導入済み)で
  `cmake --preset vcpkg-windows`(または`-vs2026`)が通ることを確認
  してから、パッチブラウザ/エディタのUI実装に着手する。
