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
| `include/fpe/SampleZone.h` / `src/SampleZone.cpp` | ✅ | SampleZone / SampleZonePatch / SampleZoneBank (AWM専用、D-013で確定) |
| `include/fpe/PcmBank.h` / `src/PcmBank.cpp` | ✅ | PcmBankEntry / PcmBank(ADPCM-B/A・PCM-D8、`*.pcmbank.json`+参照先`adpcm_json`のentries[]、D-013) |
| `include/fpe/Profile.h` / `src/Profile.cpp` | ✅(一部推測、D-002参照) | 最上位の *.profile.json |
| `include/fpe/PatchWorkspace.h` / `src/PatchWorkspace.cpp` | ✅ | 読み込み/保存/CRUD/閲覧ツリーの統合クラス |
| `include/fpe/JsonUtil.h` | ✅ | getOr/getRequiredヘルパー、JsonError |
| `tests/smoke_test.cpp` | ✅ | 117項目のアサーション、クリーンビルドで全通過確認済み |
| `fixtures/*` | ✅ | テスト用サンプルプロファイル一式(PcmBank用フィクスチャ含む) |

### GUI (`fitom_patch_editor_gui`)

| ファイル | 状態 | 内容 |
|---|---|---|
| `apps/gui/main.cpp` | 🚧 一部実装 | メニュー(プロファイル読み込み/新規作成\*/削除\*、\*は未実装ボタン) → ファイルブラウザ(*.profile.json一覧、ディレクトリ移動) → 読み込み成功時はアウトライン(バンク/キット一覧のみ、閲覧専用、ネイティブ/パフォーマンス/デバイス/サンプルゾーン/PCM波形/ドラムキットの6カテゴリ)表示、失敗時はエラーポップアップ→ブラウザに戻る、という一連の流れを実装。バンク/キットを選択すると個別パッチ/ノート一覧を表示するBankDetail画面に遷移(D-012、2026-07-17)。Outlineに「新規バンク作成」ボタン(ネイティブ/ハードウェア/パフォーマンス/ドラムキット、番号自動採番、OK押下で即save()、D-014、2026-07-18)を追加。デバイスパッチバンクの個別パッチを選択すると、モードレスで複数開けるパッチ編集ウィンドウ(FMオペレータパラメータ、リアルタイムエンベロープ波形、試聴鍵盤)を開けるようにした(Deviceパッチのみ、D-015、2026-07-18、実機クリック確認済み)。OPN/OPN2は実チップのレジスタ幅どおりのパラメータ範囲・固定幅ウィンドウ(4オペレータ分、他チップ種別で余白が出るのは許容)・3オクターブ鍵盤+CC#1/CC#7レバー(鍵盤と高さ・Yオフセット一致)・ALG接続図(専用に再生成、スピンボタンでALGの入力そのものと一体化、バンド左端に配置)に対応(D-016/D-017、2026-07-18、実機確認済み。他チップ種別は範囲・接続図とも今後の課題)。日本語UI表示のためMeiryo等のCJKフォントを動的ロード。第1引数に`*.profile.json`パスを渡すとメニュー/ブラウザを飛ばして直接アウトラインから起動可能(動作中のFITOM_Xからの子プロセス起動を想定、D-010参照、実機確認済み)。ネイティブ/パフォーマンス/ドラムノートのパッチ編集フォームは未着手 |
| `apps/gui/MidiPipeClient.h` / `.cpp` | ✅(Windows実機確認、POSIX経路未検証) | FITOM_X内部MIDIパイプ(名前付きパイプ/UNIXドメインソケット)クライアント。試聴鍵盤からのノートオン/オフ・HwPatchパラメータオーバーライドSysEx・CC送信に使用(D-015、D-016) |
| `apps/gui/BmpLoader.h` / `.cpp` | ✅ | 24bit非圧縮BMPの最小限ローダー。ALG接続図表示に使用(D-016) |
| `assets/alg_diagrams/opn_al{0-7}.bmp` | ✅ | OPN系ALG接続図(8種)。実行ファイルの隣にpost-buildでコピーされる(D-016) |

### ビルド・依存関係設定

| ファイル | 状態 |
|---|---|
| `vcpkg.json` | ✅ |
| `CMakeLists.txt` | ✅ |
| `CMakePresets.json` | ✅(`vcpkg` / `vcpkg-windows` / `vcpkg-windows-vs2026`) |

## 既知の未対応・将来課題

- **パッチ編集フォーム・CRUD UI** — メニュー→ファイルブラウザ→
  読み込み→アウトライン表示(閲覧専用)までは実装済み(下記GUI一覧
  参照)。Outlineへの「新規バンク作成」(ネイティブ/ハードウェア/
  パフォーマンス/ドラムキット)は実装済み(D-014)。デバイスパッチ
  (HwPatch)の編集フォーム(FMオペレータパラメータ、リアルタイム
  エンベロープ波形、試聴鍵盤、モードレス複数ウィンドウ)も実装済み
  (D-015)。パラメータ範囲・ウィンドウ幅・鍵盤3オクターブ+CC#1/CC#7
  レバー・ALG接続図はOPN/OPN2系のみ実チップのレジスタ幅で確認済み
  (D-016)。**OPM/OPZ/OPL系/OPLL系/PSG系等、他のチップ種別のパラメータ
  範囲はまだ未確認**(`genericVoiceRanges()`/`genericOpRanges()`の
  0-99フォールバックのまま)で、`docs/voice-parameter-reference.md`+
  FITOM_X実ソースのレジスタマスクを突き合わせて`getVoiceFieldRanges()`/
  `getOpFieldRanges()`(`apps/gui/main.cpp`)に追加していく必要がある。
  ALG接続図もOPN系の`opn_al{0-7}.bmp`のみで、他チップ用の画像
  (例: `opl_al*.bmp`)は未取り込み。ネイティブパッチ/パフォーマンス
  パッチ/ドラムノートの編集フォーム、バンク/パッチの複製・削除UI、
  プロファイル自体の新規作成・削除UI(メニューの「新規プロファイル
  作成」「プロファイル削除」ボタンは表示のみで無効化してある)は
  未着手。`fpe::PatchWorkspace`のCRUD API(複製・削除含む)は揃って
  いるので、これを呼ぶUIを`apps/gui/main.cpp`(または分割した複数
  ファイル)に実装していく。
- **試聴機能(FITOM_X内部MIDIパイプ)は実装したが実機接続未確認、
  通常MIDI出力へのフォールバックも未実装** — `MidiPipeClient`
  (D-015)でFITOM_X本体の名前付きパイプ/UNIXドメインソケットへの
  接続・送信自体は実装・Windows実機でクラッシュしないことまでは確認
  したが、実際にFITOM_X本体(`fitom_midi_pipe`バックエンド有効ビルド)
  を起動して接続し、音が正しく変化することの確認は未実施(このマシンに
  ビルド済みのFITOM_X実行環境が無いため)。またFITOM_Xが起動して
  いない場合の通常MIDI出力へのフォールバック(利用者の要件に含まれて
  いたが、新規ライブラリ依存の追加を伴うため今回は見送った)も未実装。
  POSIX(Unix domain socket)経路も未検証。次にFITOM_X実行環境が
  用意できたタイミングで実機確認すること。
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
- **`*.sccwave.json`の内容モデル化** — `Profile`に`scc_wave_banks[]`の
  ref(bank+file)は追加したが、参照先ファイル自体のデータモデル
  (`SccWaveBank`クラス)は未着手。`PatchWorkspace`はまだこの内容を
  ロードしない(D-008参照)。`*.pcmbank.json`側は`fpe::PcmBank`として
  実装済み(D-013)。
- **(別プロジェクト側の課題、要報告済み) `FITOM_staging`の一部
  `*.pcmbank.json`の`adpcm_json`パスが二重になっており解決できない** —
  `banks/PCM/pss680/pss680_opna.pcmbank.json`/`pss680_opnb.pcmbank.json`
  の`adpcm_json`フィールドが、pcmbank.json自身が既に置かれている
  ディレクトリ階層を再度含んだ値になっており(D-013参照)、FITOM_X本体の
  `PatchManager::loadPcmBankJson()`の実装(pcmbank.json自身の親
  ディレクトリを起点に解決)に照らすと解決不能なパスになっている。本
  エディタでは警告として表示するに留めている(実データを推測で書き換え
  ていない)。FITOM_X本体上でも同じ理由でこれらADPCM-B/ADPCM-Aバンクの
  実発音(ドラムキットからの参照)が解決できていない可能性が高い。
  `FITOM_staging`側のデータ修正が必要かどうか、利用者側での確認を推奨。
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

### 2026-07-17 (同マシン、GUI起動引数でプロファイル直接オープン)
- やったこと: `apps/gui/main.cpp`の`main()`を`argv`を受け取るように変更し、
  `argv[1]`(プロファイルパス)が渡された場合は起動直後にそのプロファイルを
  読み込み、メニュー/ファイルブラウザを飛ばして直接アウトライン画面から
  開始するようにした(詳細・動機は`docs/DESIGN.md` D-010参照)。読み込み
  失敗時は既存のエラーポップアップ+メニュー画面へのフォールバックが
  そのまま働く(ファイルブラウザでの選択と同じ`tryLoadProfile()`を通す
  ため、特別な分岐は不要だった)。実機(Windows、`vcpkg-windows-vs2026`)
  でビルドし、スクリーンショット確認で(1)`fixtures/profile.json`を
  引数に渡すとOutline画面(「プロファイル: Test Profile」)から直接
  開始すること、(2)存在しないパスを渡すとメニュー画面+読み込み
  エラーポップアップにフォールバックすること、の両方を確認した。
- 未完了・既知の問題: FITOM_X本体側にこのエディタを実際に子プロセス
  起動する仕組みはまだ無い(本セッションではエディタ側が引数を
  受けられるようにしただけ)。それ以外の未着手項目(パッチ編集
  フォーム・CRUD UI・仮想MIDIコントローラ等)に変更なし。
- 次にやること: パッチブラウザのアウトラインから個々のパッチ/
  パフォーマンスパッチ/ドラムノートを編集するフォームUIを
  `fpe::PatchWorkspace`のCRUD APIの上に実装する。

### 2026-07-18 (同マシン、Outline簡略化 + ADPCM/AWM分類バグ修正)
- やったこと: 利用者が`FITOM_staging/config/profiles/emu_opn.profile.json`
  を実際に読み込んだスクリーンショットからのフィードバックを受けて2件
  対応した。(1) `isSampleBasedVoicePatchType`が`ADPCMB_Y8950`〜`AWM`の
  値域全体を`SampleZonePatch`扱いしていたバグを修正し、AWM限定にした
  (D-011)。FITOM_X本体の`core/src/Config.cpp`の実ディスパッチと
  `docs/manuals/hwpatch-reference.md`のセクション14/15を根拠に確認。
  修正後、`emu_opn.profile.json`のADPCMB/ADPCMAバンクは正しく
  「デバイスパッチバンク」側に分類されるようになった(ただし参照先の
  `*.pcmbank.json`自体が`patches[]`を持たないため中身は0件のまま —
  これは本エディタ側ではなくFITOM_X本体+`FITOM_staging`側の構成の
  問題である可能性が高く、利用者に報告済み。詳細はD-011および上記
  「既知の未対応・将来課題」参照)。(2) `apps/gui/main.cpp`のOutline
  画面が個別パッチ/ノードまでツリー展開していたのを、バンク/キット
  一覧のみの表示に簡略化し、選択すると新設の`BankDetail`画面に遷移して
  そこで初めて個別パッチ/ノート一覧を表示するようにした(D-012)。
  `tests/smoke_test.cpp`のアサーションも(1)に合わせて更新し、
  `ctest`(98項目)全通過を確認。データモデル層の修正は
  `FITOM_staging/config/profiles/emu_opn.profile.json`(ADPCM構成)と
  `emu_opl.profile.json`(AWM構成)の両方を実際に読み込ませる一時的な
  検証用実行ファイル(検証後削除)で、意図した分類・パッチ数になる
  ことを確認した。GUIのOutline画面自体は実機スクリーンショットで
  「バンク一覧のみ(個別パッチなし)」になったことを確認したが、
  そこからバンクをクリックして`BankDetail`画面に遷移する経路は、
  スクリーンショット自動化(マウスクリックのシミュレート)がこの環境で
  安定せず、実機確認できずに終わった(下記参照)。
- 未完了・既知の問題: **重要 - 作業中の事故**: `BankDetail`画面への
  遷移をクリック操作で確認しようとした際、ウィンドウのフォーカスが
  意図通りにならない問題への対処として`taskkill /IM chrome.exe /F`と
  `taskkill /IM msedge.exe /F`を実行してしまい、利用者が開いていた
  Chrome/Edgeのプロセスを全て強制終了させてしまった(利用者に直接
  謝罪・報告済み、2026-07-18)。**今後、動作確認目的であっても
  `taskkill`等でユーザーの無関係なプロセスを終了させる操作は行わない
  こと。** `BankDetail`画面のクリック遷移自体はコード上は
  `renderOutline()`の`ImGui::Selectable(...) -> selectBank(...)`と
  `renderBankDetail()`(ファイルブラウザの既存Selectableパターンを
  踏襲)で実装済みだが、実機でのクリック確認は次回セッションの持ち越し
  課題とする。ADPCM PCM waveform bank(`*.pcmbank.json`)の内容モデル化
  (D-008発見2、`scc_wave_banks`/`pcm_banks`)も引き続き未着手。
- 次にやること: 次回このマシンで作業する際は、まず`BankDetail`画面への
  遷移(バンク/キット選択のクリック動作)を実機で確認する
  (スクリーンショット自動化はウィンドウのフォーカス/最前面化が
  不安定なため、別の確認手段 — 例えば手動確認を利用者に依頼する、
  または`SendMessage`でウィンドウハンドルに直接メッセージを送るなど —
  を検討すること)。その後、パッチ編集フォームの実装に進む。

### 2026-07-18 (同マシン、fpe::PcmBank新設 - ADPCM-B/A・PCM-D8のパッチ一覧取得)
- やったこと: 前セッションのD-011(「ADPCM-B/A・PCM-D8は通常のHwBank
  経由」)が誤りだったことが利用者からの直接の仕様確認で判明したため
  修正した(詳細・全文引用はD-013参照)。これら3系統の「パッチ一覧」は
  `*.pcmbank.json`が参照する`adpcm_json`(別プロジェクト`adpcm_packer`の
  出力JSON)の`entries[]`そのもので、配列インデックスがそのまま
  `patch_prog`になる。新規に`fpe::PcmBank`/`PcmBankEntry`
  (`include/fpe/PcmBank.h`/`src/PcmBank.cpp`)を実装し、
  `VoicePatchType::isPcmWaveformVoicePatchType()`で
  `PatchWorkspace::loadBanks()`のhw_banksループを3分岐
  (AWM→SampleZoneBank/ADPCM系→PcmBank/それ以外→HwBank)に拡張、
  `banks.pcm_banks[]`(D-008でref保持のみだった配列)も同じ`PcmBank`で
  ロードするようにした。GUIに「PCM波形バンク」カテゴリ
  (`BankCategory::Pcm`)を追加し、D-012のOutline/BankDetail構造に
  組み込んだ。`PatchWorkspace::saveAs()`の「プロファイルツリー全体を
  自己完結コピーする」という既存の約束を保つため、`PcmBank`も
  `save()`/`rebaseSourceFiles()`に参加させ、`adpcm_json`/`bin_file`の
  参照先ファイル自体も新しい場所へ物理コピーするようにした
  (`copyPcmBankSidecar()`)。フィクスチャ(`fixtures/banks/PCM/
  test.pcmbank.json`+`test_adpcm.json`+ダミー`test.bin`)と
  スモークテストを追加し、117項目全通過を確認。実データ
  (`FITOM_staging/config/profiles/emu_opn.profile.json`)に対しても、
  一時的な検証用実行ファイル(検証後削除)で`pcmBanks().size()==2`
  になることを確認したが、この過程で`FITOM_staging`側の実データに
  `adpcm_json`パスの二重化バグがあることも発見した(D-013の
  「別プロジェクト側で見つかった実データの不整合」参照、利用者に報告済み
  — 本エディタでは推測で直さず警告表示のみに留めた)。
- 未完了・既知の問題: 上記の`adpcm_json`パス二重化バグにより、
  `emu_opn.profile.json`のADPCM-B/ADPCM-Aバンクは実際には
  `entries=0`のまま(警告は正しく表示される)。`BankDetail`画面での
  「PCM波形バンク」カテゴリの実機クリック確認も、前セッションから引き続き
  未完了。`*.sccwave.json`(`SccWaveBank`)の内容モデル化も引き続き未着手。
- 次にやること: `BankDetail`画面への遷移(全カテゴリ、特に新設した
  「PCM波形バンク」を含む)を実機で確認する。`FITOM_staging`の
  `adpcm_json`パス二重化バグの修正方針について利用者と相談する。
  (→2026-07-18、利用者により`FITOM_staging`側で修正済みとの報告あり。
  次回このマシンで作業する際、実機で`emu_opn.profile.json`を再読み込みし
  `entries`がwarning無しで埋まることを再確認すること。)

### 2026-07-18 (同マシン、Outlineに「新規バンク作成」ダイアログを追加)
- やったこと: 利用者の要望に基づき、Outline画面に「新規バンク作成」
  ボタンとモーダルダイアログ(`renderNewBankDialog()`)を追加した
  (詳細はD-014参照)。ネイティブ/ハードウェア(チップ系統選択付き、
  AWM/ADPCM系・未実装チップは選択肢から除外)/パフォーマンス/
  ドラムキット(routed/direct選択付き)の4種別に対応。バンク番号/prog
  は既存最大値+1で自動採番し、ファイル名は語幹入力+種別連動の
  ディレクトリ・接尾辞自動生成(例: `patches/<stem>.patchbank.json`)。
  OK押下時点で既存のCRUD API経由でメモリ上にバンクを追加し、直後に
  `PatchWorkspace::save()`を呼んで実際にスケルトンファイルを
  ディスクへ書き出す。実機でビルドし、スクリーンショットでボタンの
  表示を確認。ダイアログ自体のクリック操作(前セッションで発生した
  ウィンドウフォーカスの不安定さを理由に見送り)の代わりに、
  `tryCreateBank()`と同じ`PatchWorkspace`呼び出し列を`fixtures/
  profile.json`に対して実行する一時的な検証用実行ファイル(検証後削除)
  で、4種別ともスケルトンファイルが期待通りのパスに作成され、
  `saveAs()`で別ディレクトリにコピーした上での再読み込みもwarning
  無しで新規バンクが見つかることを確認した。`ctest`(117項目)も
  引き続き全通過を確認。
- 未完了・既知の問題: ダイアログ自体のクリック操作(種別選択・
  テキスト入力・OK押下)の実機確認は未完了(`BankDetail`画面の
  クリック確認も同様に持ち越し中)。バンクの複製・削除UI、パッチ単位の
  CRUD UI、パッチ編集フォーム、仮想MIDIコントローラは引き続き未着手。
- 次にやること: 次回このマシンで作業する際は、まず`BankDetail`画面
  および「新規バンク作成」ダイアログのクリック操作を実機で確認する
  (スクリーンショット自動化のウィンドウフォーカス問題への対処法を
  検討すること)。その後、バンク/パッチの複製・削除UIやパッチ編集
  フォームの実装に進む。
  (→2026-07-18、下記セッションでクリック自動化の問題を解決し、
  `BankDetail`のクリック確認・パッチ編集ウィンドウの実装まで完了。)

### 2026-07-18 (同マシン、モードレスパッチ編集ウィンドウ + FITOM_X内部MIDIパイプ試聴を実装)
- やったこと: 利用者からDX7系FMシンセエディタのスクリーンショット3枚
  付きで要件を受け、Deviceパッチ(HwPatch)向けのモードレスパッチ編集
  ウィンドウを実装した(詳細・仮定・スコープの絞り方はD-015参照)。
  BankDetailのデバイスパッチバンクの行をクリック可能にし、選択すると
  `AppContext::openEditors`に追加された独立windowが開く(複数同時に
  開ける)。各エディタは名前/sw_bank/sw_prog/チャンネルパラメータ
  (FB/ALG/AMS/PMS/NFQ/FB2)/オペレータごとの`AR/DR/SL/SR/RR/TL`
  スライダー+詳細項目(KSR/KSL/MUL/DT1/DT2/FXV/AM/VIB/EGT/WS/REV/
  EGS/DT3)を持ち、`AR/DR/SL/RR/TL`の現在値からエンベロープ波形を
  毎フレーム再描画(視覚補助であって特定チップの正確な再現ではない
  ことをコード上明記)。下部に試聴用の鍵盤(2オクターブ、白鍵/黒鍵の
  クリック判定込み)を実装し、新規`apps/gui/MidiPipeClient.h`/`.cpp`
  でFITOM_X本体の内部MIDIパイプ(名前付きパイプ/UNIXドメイン
  ソケット、`docs/plugin-midi-pipe.md`仕様)経由でCC#0/CC#32/
  プログラムチェンジによるデバイス選択→HwPatchパラメータオーバーライド
  SysEx→ノートオン/オフ、の順で送信するようにした。FITOM_Xが起動して
  いない場合の通常MIDI出力へのフォールバックは、新規ライブラリ依存の
  判断を伴うため今回は意図的に見送った(D-015参照)。
  実機確認では、前回セッションから持ち越しだった「スクリーンショット
  自動化がクリックを正しい位置に届けられない」問題も解決した。原因は
  DPIスケーリングによる座標系のずれで、`GetClientRect`/`ClientToScreen`
  で得た物理ピクセル座標と、実際にクリックが命中する座標との間に
  約1.33倍(≒4/3)のスケール差があった。`SetCursorPos`で実カーソルを
  目標位置に動かした上で、`PostMessage`で同じ座標を含む
  `WM_MOUSEMOVE`/`WM_LBUTTONDOWN`/`WM_LBUTTONUP`を対象ウィンドウへ
  直接送る(ウィンドウの最前面化やフォーカスの成否に依存しない)方式に
  切り替え、比率のずれを較正した座標を使うことで、Outline→
  デバイスパッチバンク展開→BankDetail→パッチ選択→パッチ編集
  ウィンドウ表示、までの一連のクリック操作を実機で確認できた。
  この過程でコードの実装バグを2件発見・修正した。(1) 試聴鍵盤
  ウィジェットが`SetCursorScreenPos()`のみでカーソルを進めていたため
  Dear ImGui自身のデバッグ警告が出ていた問題(`ImGui::Dummy()`追加で
  解決)。(2) `FB`/`ALG`等を`SameLine()`で横並びにする際、幅指定
  なしで2つ目のスライダーがウィンドウ外にはみ出し不可視になっていた
  問題(`SetNextItemWidth(150)`追加で解決)。鍵盤クリック時にクラッシュ
  しないことは確認したが、FITOM_X本体を実際に起動しての音の確認は
  未実施(このマシンにビルド済みFITOM_X実行環境が無いため)。`ctest`
  (117項目、GUI変更のみでデータモデル層に変更なし)も全通過を確認。
- 未完了・既知の問題: FITOM_X本体との実接続確認、通常MIDI出力への
  フォールバック、ネイティブ/パフォーマンス/ドラムノートの編集
  フォーム、バンク/パッチの複製・削除UIは引き続き未着手(詳細は上記
  「既知の未対応・将来課題」参照)。
- 次にやること: FITOM_X実行環境(`fitom_midi_pipe`バックエンド有効
  ビルド)が用意できたら、実際に接続して試聴音が正しく変化することを
  確認する。その後、ネイティブ/パフォーマンス/ドラムノートの編集
  フォーム、またはバンク/パッチの複製・削除UIに進む。
  (→2026-07-18、下記セッションで利用者からOPN系の実機評価
  フィードバックを受け、範囲・ウィンドウ幅・鍵盤・ALG接続図を対応。)

### 2026-07-18 (同マシン、OPN系パッチ編集フォームの4点改善)
- やったこと: 利用者がD-015のOPN系パッチ編集フォームを実機評価し、
  (1)パラメータ設定範囲が不適切、(2)ウィンドウ初期幅が4OP分無い、
  (3)鍵盤3オクターブ化+CC#1/CC#7レバー追加、(4)ALG値に応じた接続図
  表示、の4点フィードバックを受け、すべて対応した(詳細はD-016参照)。
  (1)FITOM_X本体の実レジスタマスク(`core/src/OPN_new.cpp`)と
  `docs/voice-parameter-reference.md`を突き合わせ、OPN/OPN2の実際の
  レジスタ幅(AR/DR/SR=0-31、SL/RR=0-15、TL=0-127、KSR=0-3、MUL=0-15、
  DT1=0-7、EGT=0-15、他は未使用)を確認し、`FieldRange`/
  `HwVoiceFieldRanges`/`HwOpFieldRanges`+`getVoiceFieldRanges()`/
  `getOpFieldRanges()`で反映(未確認の他チップは0-99フォールバック)。
  (2)`renderPatchEditors()`が`ops.size()`を覗き見てウィンドウ初期幅を
  動的計算するようにした。(3)`renderPreviewKeyboard()`を3オクターブ
  (C3-C6)に拡張し、`ImGui::VSliderInt`によるMod/Volレバー(新規
  `MidiPipeClient::sendControlChange()`でCC#1/CC#7を送信)を鍵盤の左に
  追加。(4)利用者が指定した`E:\マイドライブ\FITOM\dev\FITOMApp\
  FITOMApp\res\opn_al0-7.bmp`(8種、24bit非圧縮BMP)を本リポジトリの
  `assets/alg_diagrams/`にコピーして取り込み(別プロジェクトの
  Google Drive同期フォルダへの絶対パス依存を避けるため)、新規の
  最小限BMPローダー(`apps/gui/BmpLoader.h`/`.cpp`)+GLテクスチャ
  キャッシュ(`getOpnAlgTexture()`)で`ImGui::Image()`表示。アセットは
  `fixturesDir()`と同じ「CWDから上方向探索」方式(`assetsDir()`)で
  実行時に見つけ、`CMakeLists.txt`のpost-buildステップで実行ファイル
  の隣へコピーするようにした。
  実機確認では、実データ(`FITOM_staging/config/profiles/
  emu_opn.profile.json`の`necopn GM Bank`の`Acoustic Grand Piano`)を
  開き、(1)スライダーが実チップのレジスタ幅どおりの範囲で動くこと、
  (2)4オペレータ全てが横スクロール無しでウィンドウに収まること、
  (3)ALG=4に対応する接続図(2系統FMペア)が正しく表示されること、
  (4)3オクターブ鍵盤とMod/Volレバーが表示されること、をスクリーン
  ショットで確認した。この過程で、前回セッションで確立した
  クリック自動化較正(実カーソル移動+`PostMessage`、DPIスケール
  補正係数)は、ウィンドウサイズ/起動方法(`-WorkingDirectory`指定)が
  変わると再較正が必要になることも分かった(較正係数自体は同じ値
  0.751が再度有効だった)。`ctest`(117項目、GUI変更のみ)も全通過を
  確認。
- 未完了・既知の問題: OPM/OPZ/OPL系/OPLL系/PSG系等、他チップ種別の
  パラメータ範囲は未確認のまま(上記「既知の未対応・将来課題」参照)。
  ALG接続図もOPN系のみ(他チップ用画像は未取り込み)。FITOM_X本体との
  実接続確認、通常MIDI出力へのフォールバックも引き続き未着手。
- 次にやること: 利用者から他のチップ種別(OPM/OPL系等)についての
  評価フィードバックが来たら、同様にレジスタ幅を確認して対応する。
  その後、ネイティブ/パフォーマンス/ドラムノートの編集フォーム、
  またはバンク/パッチの複製・削除UIに進む。
  (→2026-07-18、下記セッションで同日中に追加フィードバックを受け、
  ウィンドウ幅・レバー整列・ALG接続図をさらに改善。)

### 2026-07-18 (同マシン、OPN系フォームさらに3点改善: 固定幅・レバー整列・ALG入力一体化)
- やったこと: D-016の実機評価直後、利用者から追加で3点フィードバックを
  受け、すべて対応した(詳細はD-017参照)。(1)`renderPatchEditors()`の
  動的ウィンドウ幅計算(オペレータ数に応じて変化)を削除し、常に
  4オペレータ分の固定幅(`kPatchEditorInitialSize`、1100x900)を使う
  実装に戻した(利用者の指示通り、オペレータ数の少ないチップで右側が
  余るのは許容)。(2)Mod/Volレバーが鍵盤とYオフセット・高さで揃って
  いなかったバグを修正 — 原因はレバー側が「ラベル→スライダー」の順で
  積んでいたため、`ImGui::SameLine()`で続けた鍵盤(ラベル無し)との間で
  実スライダー位置がラベル1行ぶんずれていたこと。レバー側を
  「スライダー→ラベル」の順に入れ替え、`renderPreviewKeyboard()`の
  白鍵高さを引数化して両者に同じ`kLeverHeight`(70.0f)を渡すように
  修正した。(3)利用者指定の元画像`opn_al0-7.bmp`を単純縮小すると
  判読できない問題に対応するため、元画像8枚から実際のオペレータ接続
  トポロジー(ALG0〜7それぞれ)を目視で確認した上で、UIでの実表示
  サイズ(168x100)に直接最適化した新しい図を再生成し
  `assets/alg_diagrams/opn_al{0-7}.bmp`を差し替えた。あわせて、独立
  していたALGスライダーを廃止し、接続図画像+`ImGui::ArrowButton`の
  スピンボタン(押しっぱなし連続変化対応)+数値表示を1つのグループに
  まとめて「チャンネルパラメータ」バンドの左端に配置し、接続図自体を
  ALGの入力コントロールとして統合した。実機確認では、`emu_opn.
  profile.json`の`Acoustic Grand Piano`を開き、固定幅ウィンドウ・
  レバーと鍵盤の完全な整列・バンド左端のALG接続図(新しい簡潔な図)・
  スピンボタンでのALG変更とそれに連動した接続図の再描画、を
  スクリーンショットで確認した。`ctest`(117項目、GUI変更のみ)も
  全通過を確認。
- 未完了・既知の問題: OPM/OPZ/OPL系/OPLL系/PSG系等、他チップ種別の
  パラメータ範囲・ALG接続図は引き続き未確認/未取り込みのまま。
  FITOM_X本体との実接続確認、通常MIDI出力へのフォールバックも
  引き続き未着手。
- 次にやること: 利用者から他のチップ種別についての評価フィードバックが
  来たら、同様に対応する。その後、ネイティブ/パフォーマンス/
  ドラムノートの編集フォーム、またはバンク/パッチの複製・削除UIに
  進む。
  (→2026-07-18、同日中にさらにALG表示の要望を受け対応。下記参照。)
- 追記(同日): ALG接続図の左上に`"ALG n"`を焼き込み(再生成スクリプトに
  `Draw-AlgLabel`追加)、画像自体が設定値を表すようにした上で、
  スピンボタンを画像の左右にフランキング配置(縦中央揃え)する形に
  変更した(D-017追記参照)。独立した「ALG %d」テキスト表示は
  (画像が使えるOPN/OPN2では)廃止。実機スクリーンショットで確認済み。
  `ctest`(117項目)も全通過。
