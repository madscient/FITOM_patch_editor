# 設計・判断ログ

このドキュメントは「今どうなっているか」ではなく「なぜそうしたか」を
記録する。実装状況そのものは `docs/STATUS.md`、利用者向けの構成説明は
`README.md` を参照。判断ログは時系列に追記していく(過去のエントリは
書き換えない。方針を変えた場合は新しいエントリを足して、古いエントリ
に「(→YYYY-MM-DDで撤回、理由は下記参照)」のように注記する)。

---

## 全体アーキテクチャ

```
fpe_data (static library)          fitom_patch_editor_gui (executable)
  include/fpe/*.h  <-------------  apps/gui/main.cpp
  src/*.cpp                          (GLFW + OpenGL3 + Dear ImGui)
  PatchWorkspace: プロファイル+
  全バンクの読み込み/保存、CRUD、
  閲覧ツリーを提供
```

`fpe_data` は「JSONを読んでC++オブジェクトにする/その逆」と「編集
操作(CRUD)」だけを担当し、GUIやMIDI通信には一切依存しない。GUI層は
`fpe_data` の上に構築する(現状は`apps/gui/main.cpp`がウィンドウ/
描画ループを持つだけのシェルで、パッチブラウザ本体は未実装)。

## 判断ログ

### D-001: FITOM_X本体とは別リポジトリ・別プロジェクトにする

FITOM_X本体(音源エンジン)のソースツリーには依存しない、完全に独立
したC++プロジェクトとして構築する。理由:

- パッチエディタはオフラインでも動作する必要があり(FITOM_X起動中の
  試聴は名前付きパイプ/ソケット経由の疎結合)、ビルド依存として
  FITOM_X本体を要求する必然性がない。
- FITOM_X本体には元々 `apps/fitom_gui`(Dear ImGuiプレースホルダ)が
  存在するが、これとは別の独立プロジェクトとして本エディタを開発する
  方針を利用者に確認済み(2026年7月のセッションで確認)。
- 両プロジェクトの接点はファイルフォーマット(profile/bank JSON)と
  MIDIパイプのワイヤープロトコルのみで、コードの共有は行わない。

### D-002: データモデルはFITOM_X公開ドキュメントのみから起こした(ソース未参照)

本プロジェクト立ち上げ時点で、FITOM_X本体のソースツリー(および
`profile.schema.json` 等の実スキーマファイル)にはアクセスできず、
Claude.aiプロジェクトの知識ベースとして同期されていたMarkdown
ドキュメント群(`docs/hwpatch-reference.md` 等、FITOM_X側のドキュメント。
本リポジトリには含まれていない)のみを根拠に実装した。このため、
以下は実例が確認できず推測で埋めた部分がある(詳細は
`include/fpe/Profile.h`・`include/fpe/DrumKit.h` 冒頭コメントおよび
`README.md`「参照元と未確認の推測箇所」を参照)。

- `profile.json` トップレベルの `patch_banks[]` / `sw_banks[]` という
  配列名(確定しているのは `hw_banks[]` / `drum_banks[]` のみ)。
- "routed"形式ドラムキットの `notes[]` 要素(`DrumNote`)のフィールド
  構成(実例があるのは"direct"形式のみ)。

**次にFITOM_X本体の実リポジトリ(スキーマファイル)にアクセスできる
機会があれば、上記2点を最優先で照合・修正すること。**

(→2026-07-17、D-008で実際に照合・修正済み。詳細は下記参照。)

### D-003: JSON I/Oは「読み込みは緩く、書き込みは明示的に」

欠落フィールドはドキュメント記載のデフォルト値にフォールバックし、
パースエラーにしない(FITOM_X本体自体の「ソフトな失敗」という設計
思想に合わせた)。書き込み時は常に正規のフィールド一式を出力する
(元ファイルの簡潔さの再現は狙わない)。理由は往復編集の単純さを
優先したため。

### D-004: GUIはDear ImGui、バックエンドはGLFW + OpenGL3(SDL2/SDL3ではなく)

FITOM_X本体がGUIにDear ImGuiを採用しているため踏襲。ウィンドウ/入力
バックエンドの選定では以下を検討した。

- **SDL2**: タッチパネル操作の要件(明示的なマルチタッチイベント)には
  本来最も適するが、vcpkgのimguiポート(2026年7月時点、v1.92.8)が
  SDL2向けバインディング(`sdl2-binding`)を提供しなくなっており、
  SDL3向け(`sdl3-binding`/`sdl3-renderer-binding`)に統合済みだった。
- **SDL3**: vcpkgのimguiポートが公式にバインディングを提供しており
  最有力候補だったが、
- **GLFW + OpenGL3**: vcpkgで完全対応(`glfw-binding`/
  `opengl3-binding`)。GLFW自体はネイティブのマルチタッチAPIを持たず
  OSのマウスエミュレーションに依存する点で、タッチパネル要件に対して
  は本来SDL系より劣る。

利用者にSDL3/SDL2(バックエンドファイルを手動管理)/GLFWの3択を提示し、
**GLFW + OpenGL3を選択**(2026年7月)。タッチ操作を本格的に作り込む
段階で、この選定が問題になる場合は本エントリを見直すこと(D-005参照)。

### D-005: (将来の検討事項)タッチ操作の作り込みでGLFWの限界に当たった場合

D-004でGLFWを選んだ結果、マルチタッチジェスチャー(ピンチ等)が必要に
なった場合は、SDL3への切り替え(vcpkgの`sdl3-binding`は既に対応
済みなので移行コストは主に`apps/gui`側のバックエンド初期化コードの
書き換えのみ)を検討すること。`fpe_data`側には一切影響しない。

### D-006: サードパーティ依存は vcpkg マニフェストモードのみ(git submoduleは不採用)

`nlohmann-json` / `imgui` / `glfw3` / `glew` はすべて `vcpkg.json` で
宣言し、`find_package(... CONFIG REQUIRED)` で解決する。リポジトリに
ソースをベンダリング(git submodule等)しない方針にした。

**経緯**: 当初 `nlohmann-json` はCMake `FetchContent` で取得していたが、
GUI追加にあたりimgui等も含めて「submoduleでもvcpkgでも良い」と利用者
から提案があった。実際に `git submodule add` を試したところ、当時の
作業環境(クラウド同期/ネットワークマウントされたドライブ上に
リポジトリが置かれていた)で `.git/modules/...` 以下の書き込みが
不安定になり、`fatal: bad config line 1` 等のエラーで失敗した
(同種の環境では、大きめのファイル書き込みが不完全な状態で保存され
たり、`.git/index` が壊れたりする事象も複数回観測している。詳細は
下記「環境固有の注意点」参照)。この問題を構造的に避けるため、
リポジトリ内にサードパーティのgit履歴を持ち込まない vcpkg
マニフェストモードに統一した。

### D-007: Visual Studio 2026 対応プリセットの追加

CMakeの `Visual Studio 18 2026` ジェネレータ(CMake 4.2で追加)を使う
`vcpkg-windows-vs2026` プリセットを `CMakePresets.json` に追加
(2026年7月)。既存の `vcpkg-windows`(Visual Studio 2022、
`Visual Studio 17 2022`)はそのまま残し、マシンによって使えるVisual
Studioのバージョンが異なることを想定して両方を用意している。
Visual Studio 2026に同梱のCMake(4.1.1)はこのジェネレータに対応して
いないため、該当マシンでは新しいCMakeを別途用意する必要がある
(`README.md`「ビルド方法」参照)。

### D-008: D-002の推測箇所をFITOM_X実リポジトリ・実プロファイルと照合・修正

このマシンから、FITOM_X本体の実リポジトリ(`config_schema/*.schema.json`)
と、製品バンドル用の実プリセットプロファイル管理リポジトリ
(`FITOM_staging`、実際の`*.profile.json`一式)の両方に初めてアクセス
できたため、D-002で保留していた推測箇所を照合した。結果は以下の通り。

**発見1(重大・要修正): `profile.json`のバンクレジストリ配列は
トップレベルではなく`"banks": {...}`オブジェクトの下にネストされている。**
D-002では「配列名(`patch_banks[]`/`sw_banks[]`)が合っているか」を懸念
していたが、実際は配列名自体は推測通り正しく、問題は階層だった。
`hw_banks[]`/`patch_banks[]`/`sw_banks[]`/`drum_banks[]`はすべて
`profile.json`の`banks`オブジェクトの子として存在する。旧実装は
これらをトップレベルキーとして読んでいたため、実際の製品プロファイル
(`FITOM_staging/config/profiles/unified_preset.profile.json`等)を
読み込むと、4配列すべてが「読み込みは緩く」の設計により静かに空になり、
しかも`Profile::extra`が`banks`キーを丸ごと不透明に保持してしまうため
(未知のトップレベルキーとして扱われる)、保存時にデータは失われないが
GUIからは一切編集できない状態になっていた。`Profile.h`/`.cpp`を修正し、
`banks`オブジェクトの中から6配列
(`hw_banks`/`patch_banks`/`sw_banks`/`drum_banks`/`scc_wave_banks`/
`pcm_banks`)を読み書きするようにした。実際に`unified_preset.profile.json`
を読み込ませ、`hw_banks=63 patch_banks=5 sw_banks=7 drum_banks=15`が
ファイル内容と一致することを確認済み(修正前は全て0になっていたはず)。

**発見2: `scc_wave_banks[]`/`pcm_banks[]`という、これまで全く
モデル化されていなかった配列が`banks`オブジェクト内に存在する。**
`SccWaveBankRef`/`PcmBankRef`(`bank`+`file`)として`Profile`に追加した。
ただし参照先ファイル形式(`*.sccwave.json`/`*.pcmbank.json`)自体の
データモデル化(専用クラスの新設)は今回のスコープ外。refは
ラウンドトリップのために保持されるが、`PatchWorkspace`はまだその内容を
ロードしない(閲覧・編集ツリーには出てこない)。将来`SccWaveBank`/
`PcmBank`の本格対応をする際は本エントリを更新すること。

**発見3(軽微・修正済み): "routed"ドラムキットの`notes[]`要素、および
"direct"キットのフィールド構成。** 推測していたフィールド名
(`note`/`name`/`voice_patch_type`/`patch_bank`/`patch_prog`/`play_note`/
`sw_bank`/`sw_prog`)はすべて正しかったが、以下が欠落していた。

- "routed"の`notes[]`要素: `fine_tune`/`pan`/`gate_time`
- "direct"キット全体: `voice_patch_type`/`sw_bank`/`sw_prog`/
  `fine_tune`/`pan`/`gate_time`(キット全体で1つの値、
  `effectiveNotes()`で全ノートに展開される)

`DrumKit.h`/`.cpp`に追加し、フィクスチャ・スモークテストを実データの
形状に合わせて更新(98/98項目通過)。

**発見4(未修正・将来課題): `HwBankRef.group`(`VoicePatchType`)の
文字列テーブルが実スキーマのenumと一部食い違う。** 実スキーマの
`hw_banks[].group` enumには`OPNA`/`OPNB`/`SCCP`/`PSG`/`PCM`が含まれるが、
`VoicePatchType.cpp`のテーブルには未登録。スキーマのコメントに
「2026年7月に細かい分類文字列の不整合をFITOM_X側で修正した」旨の記載が
あり、本ライブラリの実装後にFITOM_X側で拡張された可能性がある。次回
着手する際は`docs/STATUS.md`の既知の未対応一覧を参照。

### D-009: `banks.*[].file` の相対パス解決基点をFITOM_X側の仕様変更に追従・確認

FITOM_X本体側で、`profile.json`の`banks.*[].file`(hw_banks/sw_banks/
patch_banks/drum_banks/scc_wave_banks/pcm_banks)の相対パス解決基点が、
「起動時のカレントワーキングディレクトリ」から「プロファイルファイル
自身が置かれているディレクトリ」に変更された(FITOM_X側コミット
`eed0b4a "Fix. banks.*[].file resolve relative to profile's own
directory, not CWD"`、2026-07-17。詳細は同リポジトリの
`docs/patch-structure-design.md`「相対パスの解決基点」節を参照)。
これに伴い、`FITOM_staging`(製品バンドル用presetプロファイル管理
リポジトリ)側の実プロファイルも`banks/`への参照が
`"banks/OPN/gm/xxx.hwbank.json"`から`"../../banks/OPN/gm/xxx.hwbank.json"`
のような形に更新されている(`config/profiles/`が`banks/`の2階層下に
あるため)。

本エディタ側 (`PatchWorkspace::load()` / `PatchWorkspace::resolve()`)
は、そもそも設計時点から`rootDir_ = profileJsonPath.parent_path()`を
基点に相対パスを解決する実装になっており、**追従のためのコード変更は
不要だった**(元々「プロファイル自身のディレクトリ」を基点にしていた
ため、たまたま新仕様と一致していた)。念のため以下の2点を実機で確認
した。

- `FITOM_staging/config/profiles/unified_preset.profile.json`
  (`"../../banks/..."`形式の参照を含む実プロファイル)を
  `PatchWorkspace::load()`で読み込み、`hw_banks=63`/`patch_banks=5`/
  `sw_banks=7`/`drum_banks=15`が全件warningsなしで解決されることを
  確認(`fpe_data`をリンクした一時的な検証用実行ファイルで確認、
  検証後に削除済み・リポジトリには残していない)。
- `FITOM_X/config/profiles/`配下の`preset_opl`/`preset_opm`/
  `preset_opn`/`emulator_opn_family`の各`.profile.json`も同様に確認。
  一部`patch_banks[bank=0].file`が空文字列("プレースホルダ、まだ
  ネイティブパッチバンク未割り当ての意図と思われる)だったり、
  `emulator_opn_family.profile.json`が参照する
  `../../banks/sw/necopn_gm.swbank.json`/
  `../../banks/patches/necopn_gm.patchbank.json`が
  `FITOM_X`リポジトリ側に実ファイルとして存在しない(`banks/sw/`には
  `default_gm.swbank.json`はあるが`necopn_gm.swbank.json`は無い)ために
  warningが出るケースはあったが、いずれも**パス解決自体は意図通り**で
  (`rootDir_`+相対パスの結合結果は正しい)、参照先ファイルが実際に
  存在するかどうかの問題であり、`read loosely / skip and warn`という
  既存方針(D-003)通りに動作している。本エディタ側の実装や
  `docs/patch-structure-design.md`側の仕様に不一致は無い。

`scc_wave_banks[]`/`pcm_banks[]`(参照先ファイル自体の内容モデル化は
D-008の時点で未着手のまま)についても、`ref.file`の相対パス自体は
同じ`resolve()`経由で解決されるため、今回の仕様変更の影響は同様に
「元から一致していた」。将来`SccWaveBank`/`PcmBank`を実装する際も、
この`resolve()`をそのまま使えばよい。

### D-010: GUI起動時の第1引数でプロファイルを直接開けるようにした

`apps/gui/main.cpp`の`main()`が`argv[1]`を受け取り、値があれば起動直後に
`tryLoadProfile()`でそのプロファイルを読み込んでアウトライン画面から
開始するようにした(成功時はOutline、失敗時は従来のエラーポップアップ
+MainMenuに自然にフォールバック。ファイルブラウザでの選択と全く同じ
関数を通すため、成功/失敗の扱いに特別分岐は不要だった)。

**動機**: 動作中のFITOM_X本体からこのエディタを子プロセスとして
起動し、FITOM_X側が現在読み込んでいるプロファイルをそのまま編集対象に
したいというユースケース(利用者からの要望、2026-07-17)。FITOM_X側に
このような起動の仕組み自体はまだ実装されていない(本エントリ時点では
本エディタ側が引数を受けられるようにしただけ)。将来FITOM_X側から
実際に子プロセス起動する実装をする際は、本エントリと`README.md`
「GUIの起動引数」節を参照すること。パイプ/ソケット越しの試聴機能
(`docs/plugin-midi-pipe.md`)とは独立した機構であり、起動引数は
あくまで「どのプロファイルを開くか」を伝えるだけで、双方向通信は行わない。

実機(Windows、`vcpkg-windows-vs2026`)でスクリーンショット確認済み:
`fitom_patch_editor_gui.exe "fixtures\profile.json"`でOutline画面
(「プロファイル: Test Profile」)から直接開始すること、存在しない
パスを渡した場合はメニュー画面+読み込みエラーポップアップに
フォールバックすることの両方を確認した。

## 環境固有の注意点(繰り返し観測した問題)

このリポジトリがクラウド同期/ネットワークマウントされたドライブ上に
ある場合(今回の開発環境がそうだった)、以下の事象を複数回観測した。
原因は特定できていないが、**大きめの内容を一度に書き込む操作
(エディタツールでのファイル書き換え、`git submodule add`、
`git add`後の内部処理等)が、書き込み完了前に打ち切られたように
途中で終わる**という共通点がある。

- テキストファイルの書き込みが、UTF-8のマルチバイト文字の途中で
  唐突に切れて保存される(該当ファイルが構文エラーやencodeエラーに
  なる)。
- `git submodule add` が `.git/modules/.../config` 書き込み中に失敗する。
- `.git/index` が破損し、`git status` が全ファイルを削除扱いで表示
  するようになる(`git read-tree HEAD` で復旧可能。作業ツリーの
  ファイル自体は無事なことが多い)。

**対策**: ファイルを書き換えたら、`wc -l`・`tail`・UTF-8デコード確認
(`python3 -c "open(path, encoding='utf-8').read()"`)・`git diff`のいずれか
で必ず内容を検証してから次の作業に進む。`git`操作が失敗した場合は
即座に諦めず、まず `git status`/`git log` で実際の被害範囲(多くは
インデックスのみで作業ツリーは無事)を確認してから対処する。
