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
| `apps/gui/main.cpp` | 🚧 一部実装 | メニュー(プロファイル読み込み/新規作成\*/削除\*、\*は未実装ボタン) → ファイルブラウザ(*.profile.json一覧、ディレクトリ移動) → 読み込み成功時はアウトライン(閲覧専用ツリー)表示、失敗時はエラーポップアップ→ブラウザに戻る、という一連の流れを実装・実機で動作確認済み(2026-07-17)。日本語UI表示のためMeiryo等のCJKフォントを動的ロード。パッチ編集フォーム・CRUD操作・仮想MIDIコントローラは未着手 |

### ビルド・依存関係設定

| ファイル | 状態 |
|---|---|
| `vcpkg.json` | ✅ |
| `CMakeLists.txt` | ✅ |
| `CMakePresets.json` | ✅(`vcpkg` / `vcpkg-windows` / `vcpkg-windows-vs2026`) |

## 既知の未対応・将来課題

- **パッチ編集フォーム・CRUD UI・仮想MIDIコントローラ** — メニュー→
  ファイルブラウザ→読み込み→アウトライン表示(閲覧専用)までは実装
  済み(下記GUI一覧参照)。パッチ編集フォーム(パラメータのつまみ/
  スライダー等)、仮想MIDIコントローラ(ノート番号・ベロシティ・
  モジュレーションデプス等)、バンク/パッチ/プロファイルの新規作成・
  複製・削除UI(メニューの「新規プロファイル作成」「プロファイル削除」
  ボタンは表示のみで無効化してある)は未着手。`fpe::PatchWorkspace`の
  CRUD APIは揃っているので、これを呼ぶUIを`apps/gui/main.cpp`(または
  分割した複数ファイル)に実装していく。
- **GUIの日本語フォント読み込みがWindows専用パスに依存** —
  `apps/gui/main.cpp`の`loadFonts()`はWindowsでは`meiryo.ttc`等を
  `C:\Windows\Fonts\`から直接パス指定で探す。Linux/macOSでは別の
  候補パス(Noto Sans CJK等)を試すが未検証(開発機がWindowsのため)。
  該当環境でビルド・実行する際、フォントが見つからない場合は
  警告をstderrに出しつつ「?」表示にフォールバックする設計。
- **FITOM_X本体との名前付きパイプ/ソケット通信(試聴機能)** —
  未着手。ワイヤーフォーマットはFITOM_X側ドキュメント
  `docs/plugin-midi-pipe.md` に既に仕様がある(生MIDIバイト列、
  Windows: `\\.\pipe\FITOM_X_MIDI`、Linux/macOS:
  `/tmp/fitom_x_midi.sock`)。FITOM_Xが起動していない場合はオフライン
  動作にフォールバックする設計が既に前提。
- **`docs/DESIGN.md` D-002の推測箇所の実スキーマ照合** — 2026-07-17、
  FITOM_X本体の実リポジトリと`FITOM_staging`(製品バンドルpreset
  プロファイル)にアクセスできる状況になり、照合・修正済み(詳細は
  D-008参照)。`banks`ネストの見落とし(重大)と`DrumNote`/`DrumKit`の
  フィールド不足を修正、実プロファイルでの動作も確認済み。
- **`VoicePatchType.cpp`の`group`文字列テーブルが実スキーマのenumと
  一部不一致** — 実スキーマ(`hw_banks[].group`)には`OPNA`/`OPNB`/
  `SCCP`/`PSG`/`PCM`が含まれるが、`VoicePatchType.cpp`のテーブルには
  未登録(D-008参照)。次に着手する際に追加・確認する。
- **`*.sccwave.json`/`*.pcmbank.json`の内容モデル化** — `Profile`に
  `scc_wave_banks[]`/`pcm_banks[]`のref(bank+file)は追加したが、
  参照先ファイル自体のデータモデル(`SccWaveBank`/`PcmBank`クラス)は
  未着手。`PatchWorkspace`はまだこれらの内容をロードしない(D-008参照)。
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

### 2026-07-17 (同マシン、D-002の実スキーマ照合・修正)
- やったこと: ユーザーからFITOM_X本体の実リポジトリ
  (`source/repos/FITOM_X`)と、製品バンドル用presetプロファイル管理
  リポジトリ(`source/repos/FITOM_staging`)への参照許可を得て、
  `docs/DESIGN.md` D-002で保留していた推測箇所を実スキーマ
  (`config_schema/profile.schema.json`、`drumkit.schema.json`)と実
  プロファイル(`FITOM_staging/config/profiles/unified_preset.profile.json`
  等)で照合(詳細な経緯・判断はD-008参照)。重大な見落としを発見:
  `hw_banks[]`/`patch_banks[]`/`sw_banks[]`/`drum_banks[]`は
  トップレベルではなく`"banks": {...}`オブジェクトの下にネストされて
  おり、旧`Profile.cpp`はこれをトップレベルキーとして読んでいたため、
  実際の製品プロファイルを読み込むと4配列とも常に空になり、しかも
  `banks`キー全体が`Profile::extra`に不透明に保持されて編集不能になる
  という実害のあるバグだった。`Profile.h`/`.cpp`を修正して`banks`
  オブジェクト経由で読み書きするようにし、新たに存在が判明した
  `scc_wave_banks[]`/`pcm_banks[]`のref(`SccWaveBankRef`/`PcmBankRef`、
  bank+file)も追加(参照先ファイル内容のモデル化は未着手、D-008/
  既知の未対応参照)。あわせて`DrumNote`/`DrumKit`("routed"の
  notes[]要素、"direct"キット全体)に欠落していた`fine_tune`/`pan`/
  `gate_time`と、"direct"側の`voice_patch_type`/`sw_bank`/`sw_prog`を
  追加。`fixtures/profile.json`・`fixtures/drums/*.drumkit.json`を
  実データ形状に更新し、`tests/smoke_test.cpp`にこれらの新フィールド
  の検証を追加(85→98項目、全通過)。さらに一時的な検証用プログラムを
  fpe_data.libにリンクしてビルドし、実際の
  `unified_preset.profile.json`を読み込ませたところ
  `hw_banks=63 patch_banks=5 sw_banks=7 drum_banks=15`とファイル内容
  通りに解決されることを確認(修正前なら全て0になっていたはずの値)。
  検証用プログラムはテスト後に削除済み(リポジトリには含まれない)。
- 未完了・既知の問題: `VoicePatchType.cpp`の`group`文字列テーブルが
  実スキーマのenum(`OPNA`/`OPNB`/`SCCP`/`PSG`/`PCM`)と一部不一致
  (D-008参照、未修正)。`*.sccwave.json`/`*.pcmbank.json`の内容
  モデル化は未着手。GUIウィンドウの実描画確認・パッチブラウザ/
  エディタのUI本体も引き続き未着手。
- 次にやること: `VoicePatchType.cpp`のテーブルに`OPNA`/`OPNB`/`SCCP`/
  `PSG`/`PCM`を追加するかどうか検討(FITOM_X側で最近拡張された可能性
  があるため、追加前に実際の用途を確認)。その後、GUIウィンドウの
  実描画確認とパッチブラウザ/エディタのUI実装に着手する。

### 2026-07-17 (同マシン、GUI: メニュー→ファイルブラウザ→読み込み→アウトライン実装)
- やったこと: `apps/gui/main.cpp`にステートマシン(MainMenu →
  FileBrowser → Outline、エラー時はポップアップ→FileBrowserに留まる)
  を実装。MainMenuは「プロファイル読み込み」「新規プロファイル作成」
  「プロファイル削除」の3ボタン(後者2つは今回未実装のため無効化
  表示のみ)。FileBrowserは`*.profile.json`(および接頭辞なしの
  `profile.json`)一覧・ディレクトリ移動(ダブルクリック/パス直接
  入力)を実装。読み込み成功時は`fpe::PatchWorkspace`の全バンク種別
  (ネイティブパッチ/パフォーマンス/デバイス/サンプルゾーン/
  ドラムキット)をツリー表示するOutline画面に遷移。失敗時は
  `読み込みエラー`モーダルにファイルパスと例外メッセージを表示し、
  OKでFileBrowserに戻る。
  実装の過程で2件のバグを発見・修正した。(1)
  `CMakeLists.txt`がMSVCの`/utf-8`フラグを指定しておらず、日本語
  コメント・文字列リテラルを含むソースがシステムのコードページ
  (932=Shift-JIS)で誤解釈され、コンパイルエラー(文字列リテラル内の
  改行)になっていた問題 → プロジェクト全体に`add_compile_options(/utf-8)`
  を追加して解決。(2) ファイルブラウザの`*.profile.json`フィルタが
  接頭辞なしの`profile.json`(本プロジェクトのfixture自体がこの名前)
  にマッチしないバグ → 修正。
  実機(Windows、`vcpkg-windows-vs2026`)でGUIを実際に起動し、
  スクリーンショット自動化(PowerShell + user32/gdi32 P/Invoke)で
  メニュー表示・ファイルブラウザでのディレクトリ移動・
  `fixtures/profile.json`の読み込み成功→アウトライン表示・
  ツリー展開・「閉じる」→メニューへの復帰・不正なJSON読み込み時の
  エラーポップアップ表示→OKでブラウザに復帰、の全経路を目視確認済み。
  Dear ImGuiの組み込みフォントはCJKグリフを含まないため、全ての
  日本語UIが「?」化する問題も発見し、`meiryo.ttc`等を動的ロードする
  `loadFonts()`を追加して解決(Windows専用パス、Linux/macOSは未検証)。
  検証中、`taskkill /F`で強制終了した直後は数秒間Windowsが古い
  フレームの「ゴーストウィンドウ」を残すことがあり、スクリーンショット
  に古い状態が写り込む場合があると判明(アプリ自体のバグではないので
  注意。プロセスが実際に生きているかは`tasklist`で確認すること)。
- 未完了・既知の問題: パッチ編集フォーム・CRUD UI・仮想MIDI
  コントローラは未着手。「新規プロファイル作成」「プロファイル削除」
  は無効化ボタンのまま。フォントパスがWindows専用(D-008の
  `VoicePatchType`拡張・`*.sccwave.json`/`*.pcmbank.json`モデル化も
  引き続き未着手)。
- 次にやること: パッチブラウザのアウトラインから個々のパッチ/
  パフォーマンスパッチ/ドラムノートを編集するフォームUIを
  `fpe::PatchWorkspace`のCRUD APIの上に実装する。その後、
  「新規プロファイル作成」「プロファイル削除」を実装する。

### 2026-07-17 (同マシン、FITOM_X側の相対パス解決ルール変更への追従確認)
- やったこと: FITOM_X本体側で`banks.*[].file`の相対パス解決基点が
  CWD基点からプロファイル自身のディレクトリ基点に変更されたコミット
  (`eed0b4a`)を受け、本エディタ側の対応状況を確認(詳細は
  `docs/DESIGN.md` D-009参照)。`PatchWorkspace::resolve()`は元々
  プロファイル自身のディレクトリを基点にしていたため、コード変更は
  不要だった。念のため一時的な検証用実行ファイル(`fpe_data`リンク、
  検証後削除・リポジトリには含まれない)を作り、
  `../FITOM_staging/config/profiles/unified_preset.profile.json`
  (新仕様の`"../../banks/..."`形式)を実際に読み込ませ、
  `hw_banks=63`/`patch_banks=5`/`sw_banks=7`/`drum_banks=15`が
  warning無しで全件解決されることを確認。`../FITOM_X/config/profiles/`
  配下の複数プロファイルでも確認し、出たwarningは全て参照先ファイル
  自体の不在(空文字列プレースホルダや`FITOM_X`リポジトリ側の
  ファイル欠落)によるもので、パス解決ロジック自体の問題ではないと
  切り分けた。`ctest`(既存98項目)も引き続き全通過を再確認。
- 未完了・既知の問題: 上記の通りコード変更は発生していない
  (ドキュメント更新のみ)。パッチ編集フォーム・CRUD
  UI・仮想MIDIコントローラは引き続き未着手。`VoicePatchType.cpp`の
  `group`テーブル不一致(D-008発見4)、`scc_wave_banks`/`pcm_banks`の
  内容モデル化も未着手のまま。
- 次にやること: 変更なし。パッチブラウザのアウトラインから個々の
  パッチ/パフォーマンスパッチ/ドラムノートを編集するフォームUIの
  実装に進む。
