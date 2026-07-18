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
(→2026-07-18、`*.pcmbank.json`側は`fpe::PcmBank`として実装。D-013参照。
`*.sccwave.json`(`SccWaveBank`)は引き続き未着手。)

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

### D-011: `isSampleBasedVoicePatchType`をAWM限定に修正(ADPCM-A/B・PCM-D8は通常のHwBank経由)

**(→2026-07-18、D-013で一部訂正。「ADPCM-B/A・PCM-D8は通常の`HwBank`
(`patches[]`/`ops[]`)経由」という本エントリの結論は誤りだった。実際には
これら3系統は`HwBank`でも`SampleZoneBank`でもない第3の形状
(`*.pcmbank.json`+参照先`adpcm_json`の`entries[]`、`fpe::PcmBank`として
新設)を持つ。`isSampleBasedVoicePatchType()`をAWM限定にした部分の判断
自体は正しいまま(そのまま維持)。詳細はD-013参照。)**

利用者が`FITOM_staging/config/profiles/emu_opn.profile.json`
(OPN2 + ADPCM-B/ADPCM-A構成)を本エディタで開いたところ、
「サンプルゾーンバンク」欄に`ADPCMB`/`ADPCMA`のバンクが
「(0 patches)」として表示され、参照が事実上機能していないという
報告を受けて調査した。

**原因**: `isSampleBasedVoicePatchType()`(`src/VoicePatchType.cpp`)が
`ADPCMB_Y8950`〜`AWM`の値域全体(D-002時点でdocs記載の「セクション見出し」
だけを見て、ADPCM-B/A・PCM-D8・AWMをひとまとめに"サンプルベース系"と
誤って解釈していた)を`SampleZonePatch`(`*.samplezonebank.json`、
`zones[]`によるキーゾーンマッピング形式)として扱っていたが、実際には
`docs/manuals/hwpatch-reference.md`のセクション14と15は明確に別物だった。

- セクション14(ADPCM-B/A・PCM-D8): 通常の`HwPatch`と同じ`ops[]`形状を
  使い、`ops[0].WS`が「PCM波形バンク内のエントリ番号」を指すだけ
  (FM音源のWaveform Selectと同じ位置づけのフィールド)。
- セクション15(AWM、YMF278+YRW801): `HwPatch.ops[]`を一切使わない、
  専用の`SampleZonePatch`(`zones[]`)形式。

FITOM_X本体の実際のディスパッチ(`core/src/Config.cpp`
`FITOMConfig::buildFromProfile()`のhw_banksループ、
`voicePatchType == VOICE_PATCH_AWM`の場合のみ
`pm.loadSampleZoneBankJson()`、それ以外(ADPCMB/ADPCMA/PCMD8含む)は
FM系チップと同じ`pm.loadHwBankJson()`)でこれを確認した。

**修正**: `isSampleBasedVoicePatchType()`を`AWM`のみ`true`を返すように
限定。`PatchWorkspace::loadBanks()`側のロジック自体(`isSampleBasedVoicePatchType`
の結果でSampleZoneBank/HwBankを振り分ける分岐)は変更不要だった。
`tests/smoke_test.cpp`のアサーションも合わせて更新
(`ADPCMB_Y8950`/`ADPCMB`/`ADPCMA`/`PCMD8`はいずれも`false`になることを
明示的に検証)。

実機で以下を確認済み(`fpe_data`をリンクした一時的な検証用実行ファイル、
検証後削除・リポジトリには残していない):

- `emu_opn.profile.json`(ADPCM構成): 修正後は`ADPCMB`/`ADPCMA`の2バンクが
  「デバイスパッチバンク」(HwBank)側に分類され、「サンプルゾーンバンク」
  は0件になった。
- `emu_opl.profile.json`(AWM構成、OPL4): AWMバンク2件は引き続き
  「サンプルゾーンバンク」側に分類され、`SampleZonePatch`の
  パッチ数(128件・1件)も正しく読み込めることを確認した。

**未解決・別プロジェクト側の問題として報告**: 修正後も`ADPCMB`/`ADPCMA`
バンクの`patches`は0件のままである。これは本エディタのバグではなく、
`emu_opn.profile.json`の`hw_banks[group=ADPCMB/ADPCMA].file`が
`patches[]`を持つ`*.hwbank.json`ではなく、`*.pcmbank.json`
(`{name, codec, sample_rate, boundary, bin_file, adpcm_json}`という
全く別スキーマ、生PCMエントリテーブル)を直接指しているためで、
FITOM_X本体の`loadHwBankJson()`も同じファイルを読めば同様に
`patches`0件になるはずである(`patches`キー自体が存在しないため)。
つまりFITOM_X本体上でもこの構成のままでは同じ問題が起きる可能性が高い。
ドラムキット側(`banks/drums/pss680_opnb.drumkit.json`)は
`patch_bank`/`patch_prog`でこのHwBank登録を直接参照する設計になって
いるため、影響は本エディタの表示だけに留まらない可能性がある。
本エディタ側で独自に「pcmbank.jsonのentries[]を仮想的にpatchesとして
読み込む」ような拡張をするのは、FITOM_X本体が実際にサポートしていない
挙動を先取りして実装することになり、D-003(FITOM_X本体の実装に
忠実に追従する)の方針に反するため見送った。利用者に本件を報告済み。
FITOM_X本体側で対応方針(`banks.pcm_banks[]`経由に変更する/
`loadHwBankJson`側でpcmbank形式を検出してentries[]をpatchesとして
展開する、等)が決まった場合は、本エントリを更新した上で本エディタ側も
追従すること。

### D-012: プロファイルアウトラインをバンク一覧のみに簡略化、個別パッチは選択後の別画面へ

`apps/gui/main.cpp`のOutline画面が、バンク配下の個別パッチ(ネイティブ
パッチ/パフォーマンスパッチ/デバイスボイスパッチ/サンプルゾーン
パッチ、ドラムノート)までツリーで展開表示していたのを、バンク/
キット一覧(名前・インデックス・件数のみ)に簡略化した(利用者からの
UXフィードバック、2026-07-17)。バンク/キットの行を選択すると新設の
`AppState::BankDetail`画面に遷移し、そのバンク/キットの中身(パッチ/
ノート一覧)だけを表示する。「戻る (アウトライン)」でOutlineに戻る。

選択状態は`AppContext::selectedCategory`(`BankCategory` enum:
Native/Performance/Device/SampleZone/Pcm/Drum。`Pcm`はD-013で追加)+
`selectedIndex`(該当vectorへのインデックス)で保持する。本GUIは現時点
では読み取り専用(バンク一覧はロード時に確定し、以後変化しない)なので、
インデックスをそのまま保持する単純な実装で問題ない。将来CRUD機能
(バンクの追加/削除/並べ替え)を実装する際は、インデックスの
unstable 化に注意し、安定なキー(bankIndex+category等)への切り替えを
検討すること。

### D-013: `fpe::PcmBank`を新設し、ADPCM-B/A・PCM-D8の「パッチ一覧」を実装

D-011で「ADPCM-B/A・PCM-D8は通常の`HwBank`経由」と結論づけたが、
これは誤りだった。利用者から直接、次の仕様を確認した。

> pcmbankは、参照しているpcmメモリマップファイル(`pss680_opnb_adpcmb.json`
> など)の内容がパッチを表しています。エンドユーザーが直接編集する
> ことはありませんが、ドラムキットから参照することはあるのでパッチ
> 一覧の取得は必要です。(FITOM_X本体でもそのように取得しています)
> pcmメモリマップは`adpcm_packer`のoutputファイルそのものを指定します。

つまりADPCM-B/A・PCM-D8の「パッチ一覧」は、`ops[]`を持つ`HwPatch`でも
`zones[]`を持つ`SampleZonePatch`でもなく、**`*.pcmbank.json`が参照する
`adpcm_json`(別プロジェクト`adpcm_packer`の出力JSON)の`entries[]`**
そのものである。`entries[]`の各要素(`name`/`offset`/`size`/
`padded_size`/`root_note`)は`prog`フィールドを持たず、**配列内の
0始まりインデックスがそのまま`patch_prog`として扱われる**(実データ
`FITOM_staging/banks/drums/pss680_opnb.drumkit.json`の
`patch_prog: 0/16/1/29`が、対応する`adpcm_json`の`entries[0]`/
`entries[16]`/`entries[1]`/`entries[29]`の名前と一致することで確認
済み)。この仕様は`config_schema/pcmbank.schema.json`
(`"adpcm_json を読み込んで entries[] を自動構築する。entry_no は
adpcm_json の entries 順で 0 から割り当てられる"`)とも整合する。

**実装**: `fpe::PcmBank`(`include/fpe/PcmBank.h`/`src/PcmBank.cpp`)を
新設。`PcmBankEntry`(`name`/`offset`/`size`/`padded_size`/`root_note`)
の配列を持つ。`VoicePatchType::isPcmWaveformVoicePatchType()`
(ADPCMB_Y8950/ADPCMB/ADPCMA/PCMD8のみtrue、`isSampleBasedVoicePatchType`
(AWM専用)とは別関数)で、`PatchWorkspace::loadBanks()`のhw_banksループを
3分岐(AWM→`SampleZoneBank`、ADPCM系→`PcmBank`、それ以外→`HwBank`)に
拡張。`banks.pcm_banks[]`(D-008で ref のみ保持していた配列)も同じ
`PcmBank`としてロードするようにした(`PatchWorkspace::pcmBanks()`、
`findPcmBank()`)。GUI(`apps/gui/main.cpp`)にも「PCM波形バンク」
カテゴリを追加(`BankCategory::Pcm`、D-012のOutline/BankDetail構造に
自然に組み込み)。

`*.pcmbank.json`自身のフィールド(`entries[]`が直接埋め込まれている
場合はそちらを優先、無ければ`adpcm_json`を追いかける)は
`PcmBank::from_json`が単独ファイルの内容だけを読み、`adpcm_json`を
追いかける2段階ロードは`PatchWorkspace.cpp`内の`loadPcmBank()`
(ファイルI/Oが必要なため)が担当する。`adpcm_json`の解決基点は
「pcmbank.json自身の親ディレクトリ」(FITOM_X本体の
`PatchManager::loadPcmBankJson()`、`baseDir = path.parent_path()`と
確認済み)。

エンドユーザーはこの内容を直接編集しないとのことだが(CRUD APIは
設けていない)、`PatchWorkspace::saveAs()`が「プロファイルツリー全体を
自己完結コピーする」という既存の約束を保つため、`PcmBank`も
`save()`/`rebaseSourceFiles()`に参加させた。`to_json`は`adpcm_json`が
設定されていれば`entries`を書き出さず参照だけを保持し(参照先ファイル
の内容を無闇に複製しない)、`rebaseSourceFiles()`は`adpcm_json`/
`bin_file`の参照先ファイル自体もコピー先ディレクトリへ物理コピーする
(`copyPcmBankSidecar()`)。これにより「名前を付けて保存」後も
ADPCM系バンクの参照が壊れない。フィクスチャ
(`fixtures/banks/PCM/test.pcmbank.json`+`test_adpcm.json`+ダミー
`test.bin`)とスモークテストを追加し、ロード・ラウンドトリップとも
warning無しで通ることを確認(117項目、全通過)。

**別プロジェクト側で見つかった実データの不整合(要報告・本エディタでは
未修正)**: 上記実装(FITOM_X本体の`loadPcmBankJson()`と同じ「pcmbank.json
自身の親ディレクトリを起点に`adpcm_json`を解決する」ルール)を実際の
`FITOM_staging/config/profiles/emu_opn.profile.json`に適用したところ、
`banks/PCM/pss680/pss680_opna.pcmbank.json`/`pss680_opnb.pcmbank.json`
の`adpcm_json`フィールド値が`"banks/PCM/pss680/pss680_opna_adpcmb.json"`
のように、pcmbank.json自身がすでに`banks/PCM/pss680/`に置かれている
にもかかわらず同じディレクトリ階層を再度含んだパスになっており、
「pcmbank.json自身の親ディレクトリ起点」で解決すると
`banks/PCM/pss680/banks/PCM/pss680/...`という存在しないパスになって
しまうことが判明した(本エディタの警告メッセージで実際に確認、
`hw_banks[group="ADPCMB", bank=0]: adpcm_json not found: ...`)。
`config_schema/pcmbank.schema.json`のサンプル自体も同様に
`"banks/pcm/se_bank1.adpcm.json"`という(bin_fileと同じ階層を含む)
フルパス風の値を例示しており、実データはこのサンプルに倣った可能性が
高い。しかしFITOM_X本体の`PatchManager::loadPcmBankJson()`の実装
(`baseDir = path.parent_path()`)と付き合わせると、この値は
`pss680_opna_adpcmb.json`のようなベアファイル名であるべきで、
現状のままではFITOM_X本体上でも同じ理由でADPCM-B/ADPCM-Aの実発音
(ドラムキット`pss680_opnb.drumkit.json`からの参照)が解決できない
可能性が高い。本エディタ側では警告を出すに留め、データを推測で
「直す」ことはしていない(D-003の方針、および解決基点が2通り
(pcmbank.json自身の親/プロジェクトルート)のうちどちらが正しい
FITOM_X側の意図かを断定できないため)。`FITOM_staging`側のデータ
修正、または`config_schema/pcmbank.schema.json`のサンプル修正が
必要かどうか、利用者側での確認を推奨する。

### D-014: Outlineに「新規バンク作成」ダイアログを追加(ネイティブ/ハードウェア/パフォーマンス/ドラムキット)

利用者の要望(2026-07-18)に基づき、`apps/gui/main.cpp`のOutline画面に
「新規バンク作成」ボタンを追加した。押すと以下を入力するモーダル
ダイアログ(`renderNewBankDialog()`)が開く。

- バンク種別(ネイティブ/ハードウェア/パフォーマンス/ドラムキット の
  4択、`NewBankType` enum)
- バンク名(自由テキスト)
- ファイル名(拡張子・接尾辞なしの語幹のみ入力させ、種別選択に応じて
  ディレクトリ+接尾辞を自動生成 - `buildRelativeBankFile()`。例:
  ネイティブなら`patches/<stem>.patchbank.json`、ハードウェアなら
  `banks/<チップ系統>/<stem>.hwbank.json`)
- (ハードウェアのみ)チップ系統選択。`kCreatableDeviceGroups`という
  固定リストから選ぶ形にし、**AWM・ADPCM-B(Y8950)/ADPCM-B/ADPCM-A/
  PCM-D8(サンプルベース系、D-011/D-013)とSD1/MA3/MA5/MA7(未実装チップ、
  `stringToVoicePatchType`は認識するがschemaのenumには含まれない
  `VoicePatchType.h`参照)は選択肢から除外**した。理由: これらを
  `createDeviceBank()`(通常のHwBank専用)で作ってしまうと、次回
  ロード時に`PatchWorkspace::loadBanks()`のhw_banks分類ロジック
  (`isSampleBasedVoicePatchType`/`isPcmWaveformVoicePatchType`)に
  よって`SampleZoneBank`/`PcmBank`として再解釈され、`{"patches":[]}`
  という空のHwBank形状データが期待される`{"patches":[...]}`
  (SampleZoneBank)や`{"entries":[...]}`(PcmBank)のどちらの形状にも
  一致しない不整合なファイルになってしまう。
- (ドラムキットのみ)routed/direct選択(ラジオボタン、`DrumKitType`)。

バンク番号(ネイティブ/パフォーマンス/ハードウェアの`bank`、
ドラムキットの`prog`)は利用者に入力させず、既存バンクの最大値+1を
自動採番する(`nextBankIndex()`/`nextDeviceBankIndex()`/
`nextDrumProg()`)。利用者の依頼文面が「バンク種別選択・バンク名・
ファイル名」の3項目のみを明示していたため、それ以外(番号・チップ系統・
routed/direct)は「入力させず妥当な扱いにする」(番号は自動採番)か
「種別選択に連動して追加フィールドを出す」(チップ系統・kit種別)かの
いずれかで対応し、ユーザーに追加確認は取らなかった(ファイル名接尾辞が
種別に連動して自動生成されるという要望の書きぶり自体が「種別選択で
ダイアログの中身が変わる」という設計を既に示唆していたため)。

OK押下時点で、既存のCRUD API
(`createNativePatchBank`/`createDeviceBank`/`createPerformanceBank`/
`createDrumKit`)でメモリ上にバンクを追加した直後に
`PatchWorkspace::save()`を呼び、実際にスケルトンファイルをディスクに
書き出す(「バンクファイルを作成」という依頼文言に合わせ、将来实装予定の
明示的な保存ボタンを待たずに即座に永続化する設計とした)。作成後は
Outlineの一覧に(次フレームから)自動的に反映される(`ws.xxxBanks()`を
毎フレーム参照して描画しているため、追加の通知処理は不要)。

実機で以下を確認済み: (1) GUIをビルドし、Outline画面に「新規バンク
作成」ボタンが表示されることをスクリーンショットで確認。(2)
ダイアログ自体のクリック操作(種別選択・テキスト入力・OK押下)の
実機確認は、本セッションでもウィンドウのフォーカス/最前面化の不安定さ
(前回セッション参照)を理由に見送り、代わりに`tryCreateBank()`と
同じ`PatchWorkspace`呼び出し列(各`createXxxBank()`+`save()`)を
`fixtures/profile.json`に対して実行する一時的な検証用実行ファイル
(検証後削除)で、(a)4種類とも期待通りのパスにスケルトンファイルが
実際に作成されること、(b)作成後に`saveAs()`していた別ディレクトリから
再読み込みしてもwarning無しで新規バンクが見つかること、の両方を確認した。

### D-015: モードレスなパッチ編集ウィンドウ + FITOM_X内部MIDIパイプ経由の試聴を実装(Deviceパッチのみ)

利用者から、参考にしている既存のFMシンセ用パッチエディタ(DX7系ハードウェア
やWeb FMシンセのエディタツール、スクリーンショット3枚で提示)の
UXを踏まえた要件を受けた。

> パッチ一覧からパッチを選択すると、パッチ編集画面を開く。
> パッチ編集画面はモードレスで複数開くことができる
> ボイスパッチタイプごとに適切な入力項目を持つ
> ADSRパラメータを変更するとエンベロープ波形をリアルタイムで表示
> 下部に試聴用の鍵盤があり、クリックにより指定されたノートを送信する
> (FITOM_Xのインスタンスがある場合は内部パイプ、無い場合はMIDI)

**スコープを絞った点(この回はDeviceパッチ、つまりHwPatchのみ)**:
提示された3枚のスクリーンショットはいずれもDX7系のFMオペレータ
編集画面(OP1-OP6、ADSR、鍵盤)で、これは`fpe::HwPatch`
(デバイスボイスパッチ、`ops[]`= `FmHwOp`)にちょうど対応する。
ネイティブパッチ(ToneLayerの参照束ね)・パフォーマンスパッチ
(SwPatch)・ドラムノートの編集画面は構造が全く異なる(ToneLayerは
値そのものではなくHwBank/HwProgへの参照)ため、今回はDeviceパッチの
編集のみを実装し、他の3種別は将来対応とした。BankDetailの
デバイスパッチバンクの行だけがクリック可能(`ImGui::Selectable`)に
なっている。

**モードレス複数ウィンドウ**: `AppContext::openEditors`
(`std::vector<PatchEditorWindow>`)で開いているエディタを保持し、
`renderPatchEditors()`が毎フレーム全件を独立した`ImGui::Begin()`
ウィンドウとして描画する(`AppState`とは無関係に常時描画されるため、
Outline/BankDetailのどちらを見ていても開いたままになる)。
`PatchEditorWindow`は`{bankIndex, prog}`のペアだけを保持し、
実体(`fpe::HwPatch&`)は毎フレーム`ws.deviceBanks()[bankIndex]
.findByProg(prog)`で引き直す(D-012の`BankDetail`と同じ設計判断)。
同じパッチに対して重複してウィンドウを開こうとした場合は、既存の
ウィンドウを再度アクティブにするだけ(`openPatchEditor()`)。

**エンベロープ波形のリアルタイム表示**: `renderEnvelopeCurve()`が
`AR`/`DR`/`SL`/`SR`/`RR`/`TL`から台形状の波形を都度再描画する
(ImGuiは毎フレーム全体を再描画するイミディエイトモードGUIなので、
スライダーを動かした次のフレームには自動的に反映される -
明示的な「リアルタイム更新」処理は不要)。**この波形は編集時の
視覚的な補助に過ぎず、特定チップの正確なエンベロープジェネレータを
再現するものではない**(それはFITOM_X本体の役割)。`FmHwOp`の
各フィールド自体に方向性(値が大きいほど速いのか遅いのか等)の
明記がドキュメント上に無いため、以下を仮定して実装した。

- `AR`/`DR`/`RR`:値が大きいほど速い(区間の見た目の幅が狭くなる)。
- `TL`:0-99のアッテネーション(値が大きいほど静か、一般的なヤマハFM
  チップの慣習)。ピークの高さ = 99-TL。
- `SL`:0-99の絶対的なサステインの高さ(ピークに対する減衰率では
  なく、そのまま高さとして表示)。
- `SR`(`FmHwOp`自身のコメント「0=sustain/ADSR mode, >0=percussive
  mode」)が0でない場合、水平なサステイン区間の代わりに`SR`由来の
  速度で0に向かって減衰し続ける表示にした(パーカッシブ音色の実際の
  挙動に近い)。

**FITOM_X内部MIDIパイプ経由の試聴**: 新規`apps/gui/MidiPipeClient.h`/
`.cpp`で、FITOM_X本体側`docs/plugin-midi-pipe.md`の仕様
(Windows名前付きパイプ`\\.\pipe\FITOM_X_MIDI`、Linux/macOS UNIX
ドメインソケット`/tmp/fitom_x_midi.sock`、生MIDIバイト列を書き込むだけ)
を実装した。送信専用・単一クライアントの仕様通り、`PatchEditorWindow`
ではなく`AppContext`が1つの`MidiPipeClient`を共有する。鍵盤クリック時、
(1) CC#0(直接デバイス選択値=`VoicePatchType`)+CC#32(HwBank)+
プログラムチェンジで試聴対象を選択、(2) プライベートSysEx
(`F0 00 48 01 <sub-cmd=0x01> 00 <ch> 00 <JSON> F7`)でHwPatchの
現在値を丸ごとオーバーライドとして送信、(3) ノートオンを送信、という
順で行う。**このSysExのJSON形状は、本プロジェクトの`*.hwbank.json`
オンディスク形式(`fpe::to_json(HwPatch)`、`FB`/`ALG`等を`"hw"`
オブジェクトにネスト)とは異なる**ことに注意。
`docs/manuals/midi-message-reference.md`「8.1」の例
(`{"FB":5,"ALG":3,"ops":[...]}`)は`FB`/`ALG`等をトップレベルの
キーとして直接持つため、`buildHwPatchOverrideJson()`で`hw`の
中身をフラットにトップレベルへ展開してから送信する
(`ext`は最小限のドキュメント例には出てこないため、他のフィールド
同様「バンクファイルと同じキー名」という規約に従うだろうという推測で
ネストしたまま送っている。実機未確認、下記参照)。ノートオフは
鍵盤を離した(`ImGui::IsItemDeactivated()`)タイミングで送信する。

**未実装・意図的に見送った点**:

- **FITOM_Xが起動していない場合の通常MIDI出力へのフォールバック**
  (→2026-07-18 実装、D-018参照。RtMidiを新規依存として追加した)。
  利用者の要件には明記されていたが、実装には新規ライブラリ依存
  (例:RtMidi)の追加という、この方針変更の是非を利用者と相談すべき
  意思決定を伴う。今回はFITOM_X内部パイプ経路のみを実装し、
  接続できない場合は鍵盤クリックを無音の無害なno-opとして扱う
  (エディタ画面に「試聴: FITOM_X未接続(オフライン)」と表示し、
  利用者に状況が分かるようにした)。本格的な通常MIDI出力を追加する
  場合は、本エントリを更新の上でライブラリ選定から着手すること。
- **ネイティブパッチ・パフォーマンスパッチ・ドラムノートの編集画面**。
  上記の通りスコープ外(将来対応)。
- **SysExのJSON形状(`ext`のネスト有無)の実機未確認**。FITOM_X本体
  (`fitom_midi_pipe`バックエンド、`-DFITOM_BUILD_BACKEND_MIDI_PIPE=ON`
  でビルドしたもの)を実際に起動してこのエディタと接続し、音が
  正しく変化することを確認するテストは今回未実施(このマシンに
  ビルド済みのFITOM_X実行環境が無いため)。次にFITOM_X実行環境が
  用意できたタイミングで実機確認すること。
- **POSIX(Unix domain socket)経路は未検証**(開発機がWindowsのため)。

**実機確認したこと**(Windows、`vcpkg-windows-vs2026`、スクリーン
ショット):Outline→(デバイスパッチバンクを展開)→BankDetail→
パッチ選択、という一連のクリック操作でモードレスのパッチ編集
ウィンドウが実際に開くこと、名前/sw_bank/sw_prog/チャンネル
パラメータ/OP1・OP2それぞれのエンベロープ波形(スライダーの現在値を
反映した台形)/試聴鍵盤、が期待通り描画されること、鍵盤をクリックしても
(FITOM_X未接続の状態で)クラッシュしないことを確認した。この過程で
実装のバグを2件発見・修正した。(1) 鍵盤ウィジェットが
`ImGui::SetCursorScreenPos()`のみでカーソル位置を進めていたため
Dear ImGui自身のデバッグ警告(「SetCursorPosでwindow境界を広げる際は
その後に何かitemを置くこと」)が出ていた問題 → `ImGui::Dummy()`を
追加して解決。(2) `FB`/`ALG`等を`ImGui::SameLine()`で横並びに
表示しようとした際、各スライダーが幅指定なしでウィンドウ全幅を
確保しようとして2つ目のスライダーがウィンドウ外にはみ出し
不可視になっていた問題 → 各スライダーに`ImGui::SetNextItemWidth(150)`
を追加して解決。鍵盤クリック時に実際に音が鳴る(FITOM_X接続時の)
確認は上記の通り未実施。

### D-016: OPN系パッチ編集フォームの4点改善(範囲・ウィンドウ幅・鍵盤3オクターブ+CC/ALG接続図)

**(→同日、D-017でさらに3点改善。「2.ウィンドウ幅」は動的計算から
固定幅へ変更、「3.鍵盤/レバー」はYオフセットのずれを修正、
「4.ALG接続図」は元画像を単純縮小する方式から専用に再生成する方式へ
変更し、ALGスライダーを廃止して接続図+スピンボタンをALGの入力
そのものとして統合した。「1.パラメータ範囲」の内容はD-017でも変更
なし。詳細はD-017参照。)**

利用者がOPN系パッチ編集フォーム(D-015)を実機評価し、以下4点の
フィードバックを受けた。

1. 各パラメータの設定範囲が適切でない
2. ウィンドウの初期サイズ(X方向)は4OPが収まる大きさに
3. 鍵盤は3オクターブ、左側にCC#1(モジュレーション)/CC#7(ボリューム)
   のレバーを追加
4. ALGは値に対応した接続イメージを表示(`opn_al0-7.bmp`参照)

**1. パラメータ範囲**: D-015時点では全フィールド一律0-99だった。
FITOM_X本体の実際のレジスタ書き込みマスク(`core/src/OPN_new.cpp`、
`FB&7`/`ALG&7`/`DT1&7`/`MUL&0xF`/`TL&0x7F`/`AR・DR・SR&0x1F`/
`KSR&3`/`SL・RR&0xF`/`EGT&0xF`)と`docs/voice-parameter-reference.md`の
OPNセクションを突き合わせ、OPN(YM2203)/OPN2系の実際のレジスタ幅を
確認した。

| フィールド | 範囲 | 備考 |
|---|---|---|
| FB, ALG | 0-7 | |
| AR, DR, SR | 0-31 (5bit) | |
| SL, RR | 0-15 (4bit) | |
| TL | 0-127 (7bit) | |
| KSR | 0-3 (2bit) | |
| MUL, DT1 | 0-15 / 0-7 | |
| EGT | 0-15 (SSG-EG、OPN/OPNA系のみ有効) | |
| AMS/PMS/NFQ/FB2/KSL/DT2/FXV/AM/VIB/WS/REV/EGS/DT3 | (未使用) | OPNは参照しない |

`FieldRange{minV,maxV,used}` + `HwVoiceFieldRanges`/`HwOpFieldRanges`
(チップ種別ごとの一覧表)を新設し、`getVoiceFieldRanges()`/
`getOpFieldRanges()`でVoicePatchTypeから引く。未確認のチップは
`genericVoiceRanges()`/`genericOpRanges()`(全項目0-99、D-015時点の
挙動のまま)にフォールバックする。OPN以外の各チップ(OPM/OPZ/OPL系/
PSG系等)も同様に`docs/voice-parameter-reference.md`+実ソースの
レジスタマスクを突き合わせて追加していく必要がある(次回以降の
継続課題)。`used=false`のフィールドはレイアウトを安定させるため
非表示にはせず、`ImGui::BeginDisabled()`でグレーアウトする方式にした。

**2. ウィンドウ幅**: `renderPatchEditors()`が`ImGui::Begin()`より前に
対象パッチの`ops.size()`を覗き見て(`PatchWorkspace`はconst参照で
読むだけなので副作用なし)、初期幅を`60 + オペレータ数*260`で計算する
ようにした。チップによってオペレータ数が1(PSG系)〜4(OPN/OPM等)と
変わるため、固定幅ではなく動的に決める設計とした。

**3. 鍵盤3オクターブ+CC#1/CC#7レバー**: `renderPreviewKeyboard()`の
呼び出しを`(48, 15)`(2オクターブ+1)から`(48, 22)`(3オクターブ+1、
C3-C6)に変更。参考にした既存エディタのUIに倣い、鍵盤の左に
`ImGui::VSliderInt`によるMod/Volの縦レバーを追加し、動かすたびに
CC#1/CC#7を送信する(`MidiPipeClient::sendControlChange()`を新設)。
レバーの現在値は`PatchEditorWindow::ccMod`/`ccVolume`としてエディタ
ウィンドウごとに保持する(複数エディタを同時に開いた場合、それぞれ
独立したレバー位置を持つ)。

**4. ALG接続図**: 利用者の指定した`E:\マイドライブ\FITOM\dev\
FITOMApp\FITOMApp\res\opn_al0.bmp`〜`opn_al7.bmp`(8種、24bit非圧縮
BMP、327x62〜269x105とサイズは様々)を本リポジトリの
`assets/alg_diagrams/`にコピーして取り込んだ(元の場所はこの
プロジェクトとは無関係の別プロジェクトのGoogle Drive同期フォルダ
であり、他マシンに存在する保証が無い絶対パスをそのままコードに
埋め込むのは避けた - CLAUDE.md「マシンごとに異なる設定はコミット
しない」の精神に合わせ、アセット自体をリポジトリにコピーして
バージョン管理下に置く方針にした)。

画像ローダーは新規実装した(`apps/gui/BmpLoader.h`/`.cpp`)。24bit
非圧縮BITMAPINFOHEADER形式のみ対応する最小限のパーサで、一般的な
BMPデコーダではない(今回同梱した8ファイルがいずれもこの形式である
ことを`file`コマンドで確認済み)。ロードした画像は`GLuint`テクスチャに
変換してALG値(0-7)ごとにキャッシュする(`getOpnAlgTexture()`)。
`ImGui::Image()`のシグネチャがこのプロジェクトが使っているDear ImGui
バージョン(1.92.8)で`ImTextureRef`(`ImTextureID`からの暗黙変換あり)
に変わっている点に注意して実装した。

アセットの実行時パス解決は、`tests/smoke_test.cpp`の`fixturesDir()`と
同じ「カレントディレクトリから上方向にマーカーファイルを探す」方式
(`assetsDir()`)を採用し、コンパイル時の絶対パス埋め込みは避けた。
`CMakeLists.txt`に`assets/`を実行ファイルの隣へコピーするpost-build
ステップを追加したので、エクスプローラーからのダブルクリック起動
(CWDが実行ファイル自身のディレクトリになる)でも上方向探索が
即座に見つけられる。

実機確認: Windows実機で、実データ
(`FITOM_staging/config/profiles/emu_opn.profile.json`の
`[OPN2 bank 0] necopn GM Bank`)に対して`[prog 0] Acoustic Grand
Piano`を開き、(1)AR/DR/SR/SL/RR/TLのスライダーが実チップの
レジスタ幅どおりの範囲で動くこと、(2)4オペレータ全てが横スクロール
無しでウィンドウに収まること、(3)ALG=4に対応する接続図
(OP1(M1)→OP2(C1)、OP3(M2)→OP4(C2)の2系統FMペア)が正しく
表示されること、(4)3オクターブの鍵盤とMod/Volレバーが表示される
こと、をスクリーンショットで確認した。

### D-017: D-016のOPN系フォームをさらに3点改善(固定幅・レバー整列・ALG入力一体化+図の再生成)

D-016の実機評価直後、利用者から追加で3点フィードバックを受けた。

1. ウィンドウ幅は一律で決めてよい(2オペレータで右側が余るのは許容)
2. Mod/Volレバーの Y オフセットを揃え、鍵盤の高さとレバーの高さを
   一致させる
3. ALG接続図は単純に縮小すると潰れて見えないので、適切なイメージを
   再生成してリポジトリに追加する。またALGの設定と一体化して
   バンドの左端に配置する(スピンボタン等で接続図自体を入力項目にする)

**1. ウィンドウ幅の固定化**: `renderPatchEditors()`が`ops.size()`を
覗き見て動的に幅を計算していたロジックを削除し、`kPatchEditorInitialSize`
(1100x900、4オペレータ分)を`ImGuiCond_FirstUseEver`で常に使う単純な
実装に戻した。オペレータ数が少ないチップ(PSG系=1、OPL2/OPLL系=2)では
右側が空くが、利用者の指示通り許容する。

**2. Mod/Volレバーと鍵盤の整列**: 原因は、レバー側が「ラベル
テキスト→スライダー」の順で積んでいたのに対し、鍵盤
(`renderPreviewKeyboard()`)にはラベルが無く、`ImGui::SameLine()`は
直前の行の先頭Yを基準にするため、レバーの実スライダー部分だけが
ラベル行ぶん下にずれていたこと。レバー側を「スライダー→ラベル」の
順に入れ替え(ラベルを下に移動)、鍵盤呼び出しの直前に何も挟まらない
ようにして解決した。さらに、`renderPreviewKeyboard()`にこれまで
関数内部定数だった白鍵高さを引数化し(`whiteHeight`)、レバー側の
`kLeverHeight`(同一の70.0f)をそのまま呼び出し側で共有させることで、
「たまたま同じ値の2つのリテラル」ではなく「同じ変数を渡す」形にし、
今後どちらかだけ変更されてズレが再発することを防いだ。

**3. ALG接続図の再生成+入力項目としての一体化**:

- *再生成*: 元々コピーした`opn_al0-7.bmp`(利用者指定の
  `E:\マイドライブ\...\FITOMApp\res\`由来、327x62〜187x164とサイズが
  不揃い)を単純に70px高へ縮小すると、特に正方形に近いもの(例:
  187x164)は文字が判読できないほど小さくなっていた。元画像8枚を
  一度PNGへ変換して実際のオペレータ接続関係を目視で確認した上で
  (ALG0: 1→2→3→4の直列、ALG1: (1+2)→3→4、ALG2: 1→4直結+(2→3)→4、
  ALG3: (1→2)+3→4、ALG4: (1→2)+(3→4)の2系統、ALG5: 1→(2,3,4)の
  3分岐、ALG6: (1→2)+3+4、ALG7: 1-4独立4系統+1にフィードバック)、
  最終表示サイズ(168x100、UIでの実表示幅150に近い解像度)で直接
  新規描画するスクリプト(`.NET System.Drawing`経由のPowerShell、
  リポジトリには含めていない一時スクリプト)で8枚とも再生成した。
  数字のみの簡潔なラベル("1"〜"4")+矢印+出力を示す下向き矢印という
  最小限の見た目にすることで、この解像度でも判読できるようにした。
  `assets/alg_diagrams/opn_al{0-7}.bmp`を差し替えた(ファイル名・
  24bit非圧縮BMP形式は変更なし、`BmpLoader`側の変更は不要)。
- *入力項目としての一体化*: これまで独立していた`ALG`の
  `sliderU8Ranged`を削除し、代わりに接続図の画像+
  `ImGui::ArrowButton`(左右のスピンボタン、`ImGui::PushButtonRepeat(true)`
  で押しっぱなし連続変化にも対応)+現在値テキスト("ALG %d")を
  ひとつのグループにまとめ、「チャンネルパラメータ」バンドの左端に
  配置した。FB/AMS/PMS/NFQ/FB2の既存スライダー群はその右側に
  `ImGui::SameLine()`で続ける。OPN/OPN2以外(接続図画像が無いチップ)
  では画像を省略し、スピンボタン+数値表示のみになる(`used=false`の
  場合は他のフィールド同様グレーアウト)。

実機確認: Windows実機で、`emu_opn.profile.json`の`Acoustic Grand
Piano`(ALG=4)を開き、(1)4オペレータ全てが収まる固定幅ウィンドウで
あること、(2)Mod/Volレバーと鍵盤の上端・高さが完全に一致すること、
(3)ALG接続図がバンド左端に配置され、新しい図(1→2, 3→4の2系統)が
判読できるサイズで表示されること、(4)スピンボタンの"▶"をクリックすると
ALG値が実際に変化し(この操作では`PushButtonRepeat`と検証スクリプトの
クリック保持時間の組み合わせにより1回のクリックでALG4→7まで複数段
進んでしまったが、実際の人間の素早いクリックでは通常1段ずつ進むはずで、
機能的なバグではなく検証手法側の副作用と判断した)、接続図もその値に
連動して再描画されること(ALG7=4系統独立+OP1へのフィードバック
ループが正しく表示された)、をスクリーンショットで確認した。

**追記(同日、さらに追加フィードバック)**: 「ALGの画像左上に設定値
(必要ならラベルも)を埋め込んで、画像自体が設定値を表すようにできるか。
画像自体の左右に◀▶のスピンボタンを配置するイメージ」という要望を
受け、(a)ALG接続図の再生成スクリプトに、キャンバス左上へ`"ALG n"`
(nは実際のALG値)を焼き込む処理(`Draw-AlgLabel`、黄色系の文字色で
オペレータボックスの白文字と区別)を追加して8枚を再生成し、(b)
`renderPatchEditor()`側の独立した`ImGui::Text("ALG %d", ...)`表示を
廃止して、スピンボタンを画像の左右に直接フランキングする配置
(`ImGui::SetCursorPosY()`でスピンボタンを画像の縦中央に揃える)に
変更した。画像が無いチップ種別(OPN/OPN2以外)ではこれまで通り
「◀ ALG n ▶」というテキストベースの表示にフォールバックする。実機で
`ALG 4`が画像左上に焼き込まれ、スピンボタンが画像左右に縦中央揃えで
配置されることをスクリーンショットで確認した。

### D-018: RtMidiによるMIDI出力フォールバック + プリファレンス機能を実装

利用者から以下の要件を受けた。

> MIDI出力機能をrtmidiで追加
> FITOM_Xのインスタンス(内部パイプ)が見つからない時にフォールバック
> トップメニューに「プリファレンス」ボタンを設置
> プリファレンス設定は、優先プロファイルフォルダ、プロファイル自動
> 読み込みON/OFFおよび読み込むプロファイルパス、出力MIDIポート、
> 出力MIDI CHをダイアログで設定し、設定内容はjsonファイルに保存し、
> 次回起動時に自動的に読み込む。コマンドライン引数でプロファイルが
> 指定される場合はプリファレンス設定をオーバーライドするが
> プリファレンス設定内容には影響しない。

これはD-015で意図的に見送っていた「FITOM_X未接続時の通常MIDI出力への
フォールバック」の実装であり、当時保留にしていたライブラリ選定を
今回RtMidi(vcpkgポート`rtmidi`、バージョン6.0.0)に決定した。

**ライブラリ選定(RtMidi)**: クロスプラットフォーム(Windows
MME/WinMM、macOS CoreMIDI、Linux ALSA/JACK)でC++から直接扱える
定番のMIDI I/Oライブラリで、vcpkgマニフェストモードで
`find_package(rtmidi CONFIG REQUIRED)` / `target_link_libraries(...
RtMidi::rtmidi)`として素直に解決できることを確認した
(D-006の「サードパーティ依存はvcpkgマニフェストモードのみ」方針に
そのまま従う)。

**トランスポート層の分離**: 既存の`MidiPipeClient`(FITOM_X内部パイプ
専用)にRtMidiの分岐を直接埋め込むと、送信するMIDIバイト列の組み立て
ロジック(ノートオン/オフ、CC、SysExオーバーライド等)がパイプ経路と
MIDI経路の両方に重複してしまう。これを避けるため、以下の3層に分離した。

- `apps/gui/MidiMessages.h`: 実際のMIDIバイト列を組み立てる純粋な
  ヘッダオンリー関数群(`noteOn`/`noteOff`/`controlChange`/
  `selectDevice`/`paramOverrideSysEx`等)。どちらのトランスポートにも
  依存しない。`paramOverrideSysEx`が組み立てるプライベートSysEx
  (`F0 00 48 01 <sub-cmd> 00 <ch> 00 <JSON> F7`)は、FITOM_X以外の
  一般的なMIDI受信機に送っても、未知のマニュファクチャラーIDの
  SysExとしてMIDI仕様上単に無視されるだけなので、経路を問わず
  そのまま送ってよい。
- `apps/gui/MidiPipeClient.h`/`.cpp`: FITOM_X内部パイプの
  接続/送信のみを行う純粋なトランスポートに簡略化(D-015時点にあった
  セマンティックな各メソッドは全て削除し、`ensureConnected()`/
  `sendRaw()`のみに縮小)。
- `apps/gui/RtMidiOutput.h`/`.cpp`: RtMidiの薄いラッパー
  (`listPorts()`/`openPort()`/`sendRaw()`)。コンストラクタが
  `RtMidiError`を投げうる(利用可能なMIDI APIが無い環境など)ため、
  try/catchで吸収して`isAvailable()==false`に倒す(起動時に例外で
  落ちないようにするため)。
- `apps/gui/PreviewOutput.h`/`.cpp`: 上記2つのトランスポートを
  束ね、`MidiMessages.h`のビルダーを使ってセマンティックな
  メソッド(`selectDevice`/`sendHwPatchOverride`/`noteOn`等、
  D-015時点で`MidiPipeClient`にあったものと同じシグネチャ)を
  提供する。`ensureReady()`がまずFITOM_X内部パイプへの接続を試み、
  失敗した場合のみRtMidi出力ポートにフォールバックする
  (`ActiveBackend::{FitomXPipe, RtMidi, None}`)。`AppContext`は
  `MidiPipeClient`ではなく`PreviewOutput`を1つ持つ形に変更した。
  試聴鍵盤側のUI(`renderPatchEditor()`)は現在どちらの経路で
  接続しているかを`ActiveBackend`に応じたテキスト
  (「FITOM_Xに接続済み」/「MIDI出力(フォールバック)で試聴中」/
  「未接続」)で表示する。

**プリファレンスの永続化先**: `apps/gui/Preferences.h`/`.cpp`。
このリポジトリ自体の複数マシン運用ルール(CLAUDE.mdの「マシンごとに
異なる設定はコミットしない」)と同じ理由で、プリファレンスJSONは
リポジトリ/ビルドツリーではなくOSのユーザー設定ディレクトリに保存する
(Windows: `%APPDATA%\FITOM_patch_editor\preferences.json`、
POSIX: `$XDG_CONFIG_HOME/fitom_patch_editor/preferences.json`、
無ければ`~/.config/fitom_patch_editor/preferences.json`)。
読み込み失敗(未保存/壊れたファイル)はエラーではなく「まだ保存されて
いない」として既定値にフォールバックする一方、保存失敗はダイアログに
エラー表示する(利用者に伝える価値があるため非対称に扱った)。

**ADLの罠(MSVC固有のビルドエラー)**: `to_json`/`from_json`を
`resolveConfigDir()`等の内部ヘルパーと同じ無名namespace内にまとめて
書いたところ、MSVCが`nlohmann::json`の`get<Preferences>()`/暗黙変換
呼び出し時にADL(引数依存の名前探索)でこれらを発見できず、
C2672/C2665相当のオーバーロード解決エラーになった。`Preferences`
自体はグローバル名前空間で宣言されているため、ADLの対象になる
`to_json`/`from_json`もグローバル名前空間に置く必要がある
(無名namespaceは技術的には別の一意な名前空間であり、using-directiveで
可視にはなるがADLの対象集合には必ずしも入らない)。`to_json`/
`from_json`のみを無名namespaceの外(グローバルスコープ)に出し、
`resolveConfigDir()`等の純粋な内部ヘルパーだけを無名namespaceに残す
ことで解決した。同種のnlohmann ADLパターンを今後追加する際は
この点に注意すること。

**コマンドライン引数によるオーバーライド(非破壊)**: `main()`の
起動シーケンスを、(1)`loadPreferences()`でプリファレンスを読み込み、
(2)`argc>1`ならその引数のプロファイルを読み込み(D-010からの既存
動作)、(3)そうでなく`autoLoadEnabled`かつ`autoLoadProfilePath`が
空でなければそちらを自動読み込み、という順に変更した。`argv[1]`が
`ctx.preferences`自体を書き換えることは無い(メモリ上の一時的な
読み込み対象を差し替えるだけ)ため、保存されているプリファレンス
ファイルの内容には一切影響しない、という利用者の要件をそのまま
満たしている。

**プリファレンスダイアログ**: `renderPreferencesDialog()`
(`renderNewBankDialog()`と同じ`BeginPopupModal`+OK/キャンセルの
形式)。ポート一覧はダイアログを開くたび(`openPreferencesDialog()`)に
`PreviewOutput::listRtMidiPorts()`で再列挙する(USB MIDIインター
フェースやloopMIDI等の仮想ポートは実行中に増減しうるため、アプリ
起動時に一度だけキャッシュするのではなく毎回スキャンし直す)。OKを
押すと`ctx.preferences`に書き戻して`savePreferences()`で保存し、
即座に`ctx.previewOutput.configureRtMidiPort()`で反映する(再起動不要)。

**実機確認**: Windows実機で、ビルド(`cmake --build build/vs2026
--config Release --target fitom_patch_editor_gui`)・
`ctest`(既存のfpe_smoke_test 85項目)が通ることを確認した。GUI起動後、
メインメニューの「プリファレンス」ボタンからダイアログを開き、出力
MIDIポートのコンボボックスに実機上の実在ポート(loopMIDIの仮想ポート)
が正しく列挙されることをスクリーンショットで確認した。OK確定後の
`preferences.json`保存・次回起動時の自動読み込み・
FITOM_X未接続時に実際にRtMidi経路で音が出ることの3点は、この
セッションでは(利用者が並行してダイアログを操作していたため
自動クリック検証を控えたことも含め)未検証。次回セッションで
改めて確認すること。

### D-019: フォルダ/ファイルパス入力欄は「テキストボックス末尾に参照ボタン」をUI全体のルールとする

D-018のプリファレンスダイアログ実装直後、利用者から以下の指摘を受けた。

> フォルダ、ファイルパスの入力には、テキストボックスの末尾にボタンを
> 配置してブラウジングによって入力する手段を用意してください
> (UI全体のルール)

**「UI全体のルール」の解釈**: 個別のフィールド対応ではなく、今後
このアプリにフォルダ/ファイルパスを入力する欄が増えるたびに同じ
パターンを適用すべき規約として扱う。現時点で該当するのは
プリファレンスダイアログの「優先プロファイルフォルダ」(フォルダ)と
「自動読み込みプロファイルパス」(`*.profile.json`ファイル)の2箇所
のみだが、将来同種の欄(例: 将来のプロファイル新規作成先フォルダ等)
にも同じ`openPathPicker()`/`PathPickerState`を再利用できるよう、
特定のダイアログに縛られない汎用コンポーネントとして実装した。

**ネイティブOSファイルダイアログではなく、自前のブラウザを流用**:
D-006(サードパーティ依存はvcpkgマニフェストモードのみ)の精神に
照らすと、`nativefiledialog-extended`等の新規ライブラリ依存を追加する
選択肢もあったが、このアプリには既に`FileBrowserState`/
`renderFileBrowser()`という「*.profile.json一覧+ディレクトリ移動」の
自前実装がある(メニューの「プロファイル読み込み」で使用)。同じ
考え方をモーダルポップアップとして切り出した`PathPickerState`/
`openPathPicker()`/`renderPathPicker()`で十分要件を満たせるため、
新規ライブラリ依存を増やさずに実装した。

**実装**: `PathPickerState`(`pickFolder`でフォルダ確定モード/
`*.profile.json`選択モードを切り替え、`target`/`targetSize`で
呼び出し元の`char[]`バッファへのポインタを保持)を`AppContext`に
1つだけ持たせ、どのテキスト欄から呼ばれてもこれを使い回す(同時に
開けるのは1つだけだが、常にモーダルとして開くため問題にならない)。
`FileBrowserState::refresh()`内にあった「`*.profile.json`名か」の
判定ロジックを`isProfileFileName()`として切り出し、両方の
ブラウザ実装で共有した。各テキスト入力は、これまでの
`ImGui::InputText("ラベル", ...)`(ラベルが右側に自動表示される形)
から、「ラベルを上の行に表示→次の行で`##隠しID`のInputText+
`ImGui::SameLine()`で直後に「参照...」ボタン」という2行構成に変更
した(ラベルとブラウズボタンをInputTextの右側に両方置こうとすると
ImGuiの自動レイアウトと衝突するため)。「自動読み込みプロファイル
パス」側は、既存の`autoLoadEnabled`チェックボックスによる
`BeginDisabled()/EndDisabled()`の対象に参照ボタンも含め、
チェックが外れている間はテキスト欄と一緒にグレーアウトする。

**実機確認**: ビルド(`cmake --build build/vs2026 --config Release
--target fitom_patch_editor_gui`)・`ctest`(既存117項目)の全通過を
確認した。プリファレンスダイアログを実際に開いてのクリック確認は、
実機テストで**「参照...」ボタンを押してもピッカーが表示されず、
プリファレンスダイアログ自体が消えて、モーダル状態のままメイン
フレームが無反応になる」という不具合が実際に発見された**(利用者
からの報告、2026-07-18)。原因は、ピッカーの描画
(`renderPathPicker(ctx)`)を`renderPreferencesDialog()`の
`EndPopup()`より後、`main()`側の別呼び出しとして実行していたこと。
Dear ImGuiのモーダルは「入れ子(stacked modals)」として使う場合、
子モーダルの`OpenPopup()`/`BeginPopupModal()`は親モーダルの
`BeginPopupModal`〜`EndPopup()`ブロックの**内側**から呼ぶ必要があり、
ブロックの外(既に`EndPopup()`済みの兄弟呼び出し)から呼ぶと子の
ポップアップIDが誤ったID階層で解決されて`BeginPopupModal`が
静かに失敗し、既に開いている親モーダルだけが「入力をブロックする
が何も描画しない」状態のまま取り残される。`renderPathPicker(ctx)`
の呼び出しを`renderPreferencesDialog()`内の`EndPopup()`直前
(`BeginPopupModal("プリファレンス", ...)`ブロックの内側)に移動し、
`main()`側の重複呼び出しを削除して解決した。`PathPickerState`の
コメントに「呼び出し元は必ず自身のモーダルブロック内で
`renderPathPicker()`を呼ぶこと」という制約を明記した(将来、他の
ダイアログがこの共有ピッカーを再利用する際に同じ問題を再発させない
ため)。修正後、利用者が実機で「参照...」ボタン→ピッカー表示→
選択、が正常に動作することを確認した。

### D-020: プリファレンスの保存先を実行ファイルと同じディレクトリ・固定ファイル名へ変更

D-019のピッカー動作確認直後、利用者から以下の指示を受けた。

> プリファレンスの保存を実装してください。場所は実行ファイルと
> 同じディレクトリ、ファイル名はFITOM_patch_editor.preference.json
> 固定で良いです。json内部の構造設計は任せます。

D-018時点では、他の多くのデスクトップアプリの慣習に倣いOSの
ユーザー設定ディレクトリ(Windows: `%APPDATA%\FITOM_patch_editor\
preferences.json`)に保存する設計にしていたが、これを明示的に変更する
指示のため、以下のように実装し直した。

- **保存先**: `<実行ファイルのディレクトリ>\FITOM_patch_editor.preference.json`
  (固定ファイル名)。`CLAUDE.md`の「`build/`ディレクトリはコミット
  しない(gitignore済み)」というルールにより、実行ファイルもこの
  設定ファイルもリポジトリには入らないため、複数マシン開発のルール
  (マシン固有設定はコミットしない)には引き続き合致する。
- **実行ファイルのディレクトリの特定方法**: これまで`assets/`の
  検索に使っていた「CWDから上方向探索」方式(`assetsDir()`、
  ビルド成果物のpost-buildコピー先を探す用途に最適化された方式)
  ではなく、Windows APIの`GetModuleFileNameW(nullptr, ...)`で
  実際に実行中のexe自身のパスを取得し、その親ディレクトリを使う
  方式にした。これは、CWDが必ずしもexeのディレクトリと一致するとは
  限らない(ショートカットの「作業フォルダ」指定やコマンドラインから
  別ディレクトリで起動した場合等)ため、利用者の要求「実行ファイルと
  同じディレクトリ」をより正確に満たすための判断。POSIX側は今回
  未実装(実行環境がWindowsのみのため)で、`exeDir()`が空を返した
  場合は`fs::current_path()`にフォールバックする。
- **JSON内部構造**: 利用者から一任されたため、D-018で既に決めていた
  フラットなキー構造(`profile_folder`/`auto_load_enabled`/
  `auto_load_profile_path`/`midi_port_index`/`midi_channel`)を
  そのまま維持し、保存先のみ変更した(`to_json`/`from_json`の実装
  自体に変更は無い)。

**実機確認**: ビルド・`ctest`(117項目)全通過を確認した後、実機で
プリファレンスダイアログの値を変更してOKを押し、
`build/vs2026/Release/FITOM_patch_editor.preference.json`が
実際に期待通りの内容
(`{"auto_load_enabled":false,"auto_load_profile_path":"",
"midi_channel":0,"midi_port_index":0,"profile_folder":"..."}`)
で作成されることを、ファイル内容の直接確認で検証した(利用者からも
「こちらでも確認しました」との報告あり)。次回起動時にこの内容が
実際に復元されること、および`autoLoadEnabled`時の自動読み込み・
RtMidi経路での実音出力の3点は、D-018から引き続き未検証のまま
(下記STATUS.md参照)。

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
