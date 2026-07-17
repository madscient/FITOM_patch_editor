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
- **`find_package(imgui CONFIG REQUIRED)` 等、実際のvcpkgでのビルド** —
  2026-07-17、Windows実機(`vcpkg-windows-vs2026`プリセット)で検証
  済み。configure・ビルド・`ctest`(85項目)・GUI実行ファイルの起動
  まで確認(下記進捗ログ参照)。GUI実行中のウィンドウ描画自体(実際に
  画面にImGuiのUIが正しく表示されるか)はスクリーンショット取得に
  失敗したため未確認のまま。次にWindows環境で作業する際は、まず
  ウィンドウが実際に描画されるかを確認すること。

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

### 2026-07-17 (別マシン、vcpkg実ビルド検証)
- やったこと: このマシンで`VCPKG_ROOT`が未設置のパス
  (`D:\Programs\x64\vcpkg`)を指していたため、実体のある`d:\vcpkg`に
  ユーザー環境変数として更新(マシン固有設定のためリポジトリには
  含めていない)。`cmake --preset vcpkg-windows-vs2026`でconfigureし、
  vcpkg経由で依存関係(nlohmann-json/imgui/glfw3/glew)一式の取得・
  ビルドが成功することを確認。続けて`cmake --build`でビルドしたところ、
  `apps/gui/main.cpp`が`<backends/imgui_impl_glfw.h>` /
  `<backends/imgui_impl_opengl3.h>`という`backends/`プレフィックス
  付きパスでインクルードしていたためGUIターゲットのみビルド失敗
  (`fpe_data`・スモークテストは成功)。実際のvcpkg imguiポート
  (v1.92.8)はバックエンドヘッダーを`include/`直下にフラットに配置
  することが判明(以前サンドボックス環境でapt取得のヘッダ構成を代用
  検証した際とはレイアウトが異なっていた)。`main.cpp`のインクルード
  パスを`<imgui_impl_glfw.h>` / `<imgui_impl_opengl3.h>`に修正して
  再ビルド・成功。`ctest`で85項目全通過も確認。
  `fitom_patch_editor_gui.exe`を実行しプロセスが起動・継続すること
  (即クラッシュしないこと)を確認したが、実際にウィンドウが描画
  されているかのスクリーンショット確認には失敗した(意図せず別の
  無関係なウィンドウが写り込んだため、確認前にファイルを削除した)。
- 未完了・既知の問題: GUIウィンドウの実描画確認は未完了。パッチ
  ブラウザ/エディタのUI本体はまだ未着手。
- 次にやること: 次回このマシンまたは別のWindows環境で作業する際は、
  まずGUIウィンドウが実際に画面に描画されるかを確認する。その後、
  `fpe::PatchWorkspace`の上にパッチブラウザ/エディタ本体のUI実装に
  着手する。
