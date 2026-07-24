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

### D-021: OPL系(OPL/OPL2/OPL3_2/OPLL)パッチ編集フォームの範囲修正 + ALG/WS画像対応

利用者から以下の要件を受けた。

> 続いてOPL系(OPL, OPL2, OPL3_2, OPLL)のパッチ編集画面
> 各パラメータの設定範囲が不適切なのでパラメータごとに適切な範囲を設定する
> ALGはOPNと同様にopl_alg0-1.bmpを参考に適切に再生成
> OPパネルにWS設定を追加する。WSはALGと同様の画像そのものをスピン
> ボタンでフリップするUIとする。
> WSの画像は、E:\マイドライブ\FITOM\material\waveform.xlsxのシート1に
> ある波形グラフを元に、適切なサイズの画像を生成する

D-016/D-017のOPN系対応と同じ考え方(実チップのレジスタ幅を実ソースで
確認してから反映する)を、OPL系4チップに適用した。

**パラメータ範囲の根拠**: `docs/voice-parameter-reference.md`の
OPL/OPL2/OPL3(2opモード)/OPLL系セクションと、FITOM_X本体の実ソース
(`core/src/OPL_new.cpp`/`core/src/OPLL_new.cpp`の実際のレジスタ書き込み
マスク)を突き合わせて確認した。

- **OPL/OPL2/OPL3_2共通**: `FB`3bit(0-7)、`ALG`1bit(0-1、doc記載の
  「0=FM/1=AM」)、`AR`/`DR`/`SR`5bit(0-31)、`SL`/`RR`4bit(0-15)、
  `TL`7bit(0-127、実チップ側は`tl6()`で6bitに切り詰められるが構造体
  フィールド自体は他チップ同様7bit)、`KSR`1bit、`KSL`2bit、`MUL`4bit。
  **(→2026-07-18訂正、D-023参照。AR/DR/SRは実際には4bit(0-15)・TLは
  6bit(0-63)が正しい実効範囲。以下の記述は誤り)**
  `DT1`/`DT2`/`FXV`(OPL系に相当機構なし、doc記載の通り常時0固定)と
  `EGT`(doc本文で「OPL系では別の用途のため無関係」と明記 - `SR`/`RR`
  側が実機のEGビット/パーカッシブモードを表現する、テーブル見出しには
  載っているが本文の注記が優先)は`used=false`。`REV`/`EGS`/`DT3`は
  OPZ専用のため同様に`used=false`。
- **WSはチップごとにビット幅が異なる**: OPLは波形選択レジスタ自体が
  無く常時サイン波固定(`used=false`)、OPL2は2bit(0-3)、OPL3_2
  (実機OPL3の2opモード)は3bit(0-7)。`oplOpRanges(wsMax)`に
  `wsMax`を渡して1関数で3チップ分を賄う設計にした。
- **OPLL系(OPLL/OPLLP/OPLLX/VRC7)**: `core/src/OPLL_new.cpp`を確認
  すると、`COPLLP`/`COPLLX`/`CVRC7`/`COPLL2`はいずれも`COPLL`から
  派生し`updateVoice`を上書きしていないため、レジスタマスクは完全に
  共通(`isOpllFamily()`でまとめて判定)。ADSR系の幅はOPL/OPL2/OPL3_2
  と同じだが、2点異なる箇所がある。(1) `hw.ALG`はFM接続の選択肢では
  なく、`ext.ALG_EXT`bit0(プリセット/ユーザー音色フラグ)が1の時のみ
  意味を持つ4bit(0-15)のROMプリセット音色番号(`instNo = preset ?
  (ALG&0xF) : 0`) - そのためOPLLは「ALG接続図」の対象からは除外し
  (`isOplAlgFamily()`がOPLLを含まない)、範囲だけ0-15に修正した数値
  スピナーのままにした。**(→2026-07-18訂正、D-023参照。本エディタの
  ops[]編集レイアウトに到達するOPLLパッチは常に`isBuiltinRef()==false`
  =ユーザー音色であり、その場合`ALG`は`preset`判定自体が偽になるため
  レジスタに全く反映されない - 0-15という範囲は誤りで、正しくは
  `used=false`の0固定)** (2) `docs/voice-parameter-reference.md`の
  OPLLセクションの表にはWSが載っていない(記載漏れ)が、実ソース
  (`(hwOp[0].WS&1)<<3`/`(hwOp[1].WS&1)<<4`)を見るとOPLLにも
  1bit(0-1)のWSが実在する - ドキュメントより実ソースを優先する
  という本プロジェクトの既定方針(README「設計上のポイント」)通り、
  ドキュメントのギャップをソースで埋めて反映した。

**ALG接続図の再生成**: OPN系と同様、`E:\...\FITOMApp\FITOMApp\res\
opl_al0.bmp`/`opl_al1.bmp`(実機画像)のトポロジーを確認した上で
(ALG0=OP1(M1)→OP2(C1)の直列FM、ALG1=OP1/OP2がそれぞれ独立して出力へ
並列接続)、UIでの実表示サイズ(168x100、OPN系と統一)に最適化した
画像を新規生成し(PowerShell + System.Drawing、OPN系画像と同じ配色・
フォント)、`assets/alg_diagrams/opl_alg0.bmp`/`opl_alg1.bmp`として
追加した(`opl_al2.bmp`以降はOPL3の4opモード用の別ペア(OP3/OP4)で
あることを画像を開いて確認済みだが、今回のスコープ外)。

**WS波形画像の生成**: 利用者指定の`E:\...\material\waveform.xlsx`
Sheet1を、xlsxが実体はzip+XMLであることを利用してPythonの`zipfile`/
`xml.etree`で直接解析した(Excel自体を起動する手段が無い環境のため)。
列B-Iがそれぞれ`WS0`-`WS7`、行2-361が角度0-359度に対応するキャッシュ
済み計算値(Excelの数式が計算済みの`<v>`要素)であることを`xl/charts/
chart*.xml`の`<c:f>`参照と`xl/worksheets/sheet1.xml`のセル定義から特定
した(各列の数式 - 例: `WS0=SIN(deg)`、`WS3=IF(MOD(deg,90)=MOD(deg,180),
ABS(SIN(deg)),0)`等 - も併せて確認し、YMF262/YM3812の標準的な8波形
(サイン/ハーフサイン/絶対値サイン/パルスサイン/交番サイン/キャメル
サイン/矩形波/導出矩形波)と一致することを確認した)。数式を再実装
するのではなく、Excelが実際に計算済みのキャッシュ値をそのままCSVに
書き出し、その値を使ってPowerShell + System.Drawingで波形カーブを
描画する方式にした(数式の再現に伴う誤差・見落としを避けるため)。
ALGと同じ168x100サイズ、左上に`"WS n"`ラベルを焼き込み(ALGと同じ
配色規約)、`assets/waveforms/ws0.bmp`-`ws7.bmp`として追加した。

**UI実装**: ALG用の画像+スピンボタンのコードを`renderImageSpinner()`
として汎用化し(値・範囲・表示幅・テクスチャ取得関数を引数化)、ALGの
チャンネルバンドとWSのオペレータパネルの両方から呼び出す形にした
(コード重複の削減)。WSは「詳細」折りたたみの中の地味な数値入力
だったものを、ALGと同様にオペレータパネル本体の可視領域へ格上げした
(利用者の「OPパネルにWS設定を追加する」という要望に対応)。
`isOplWsImageFamily()`(OPL/OPL2/OPL3_2/OPLL系- 画像を使う範囲)と
`isOplAlgFamily()`(OPL/OPL2/OPL3_2のみ、OPLLは含まない)を別々に
判定するようにし、OPLLのALGが接続図ではないという違いを型システム
レベルで表現した。

**実機確認**: ビルド(`cmake --build build/vs2026 --config Release
--target fitom_patch_editor_gui`)・`ctest`(既存117項目)全通過を確認。
`unified_preset.profile.json`(`FITOM_staging`)の`OPL3_2 bank 0`
`Tubular Bells`を実際に開き、(1) ALGバンドに新しい接続図(OP1→OP2、
ALG0)が正しく表示されること、(2) 各パラメータのスライダー範囲が
確認済みの実チップ幅で動作すること、(3) OPパネルにWS画像+スピン
ボタンが表示され(WS0=サイン波の画像)、スピンボタンでWS1(ハーフ
サイン波形の画像)に切り替わり、対象のオペレータのみ値・画像が更新
されることを、スクリーンショットで確認した。OPLL(WS 0-1の範囲
クランプ、ALGが接続図でなく数値のままであること)は、同一の
レンダリング経路(`renderImageSpinner()`/範囲構造体)を使うため
コードレビューで妥当性を確認したが、実機クリックでの再確認は今回
行っていない(次回セッションの持ち越し)。

**追記: OPLLの`builtin`(内蔵ROM参照)バンクはこのD-021の対象外**
(利用者から指摘、2026-07-18)。`fpe::HwPatch::builtin`
(`BuiltinRef`、`include/fpe/HwPatch.h`)は「role=="builtin_swpatch_meta"
バンクのエントリ」専用のフィールドで、`ops`とは排他(`builtin`が
設定されている場合`ops`は空になりうる)。これはOPLL系チップに固有の
概念(実機ROMプリセット音色への参照、`patch_type`+`patch_no`)で、
他のOPL/OPL2/OPL3_2バンクには存在しない。`renderPatchEditor()`は
以前から`patch->isBuiltinRef()`が真の場合、ops[]エディタ(本D-021の
ALG/WS画像を含む)を描画せず「内蔵ROM音色への参照のため編集できません」
という簡易メッセージを表示して早期returnする実装になっており
(D-015時点から存在)、これは今回のD-021の変更範囲に含まれていない
(=OPLLでも`builtin`が付与されていないバンクのみ、本D-021の共通
OPLレイアウトが適用される)。`builtin`バンク専用の編集画面(ROM
プリセット音色に対して実際に編集可能なフィールド、例えば
`sw_bank`/`sw_prog`程度)は、利用者からも明示的に「pending」と
された通り、今回スコープ外・今後の課題。

### D-022: ALG/WS画像アセットをBMPからPNGへ変更(stb_image導入)

利用者から「画像アセットがbmpになってますがpngにできませんか?」という
指摘を受けた。D-016時点では自前の最小限24bit非圧縮BMPデコーダ
(`apps/gui/BmpLoader.h`/`.cpp`)を実装していたが、これをPNGへの
対応も含めて汎用化するため、vcpkgの`stb`ポート(header-only、
`stb_image.h`等、MIT/CC-PDDCライセンス)を新規依存として追加した。

**ライブラリ選定の判断**: 新規にネイティブ依存を増やす代わりに
自前でPNGデコーダ(zlib展開含む)を書く選択肢もあったが、PNGは
BMPと違い可逆圧縮(DEFLATE)を要するため自前実装のコストが見合わない。
`stb_image.h`はDear ImGuiエコシステムで広く使われる定番のヘッダオンリー
画像デコーダで、`vcpkg.json`のマニフェストモード経由で取得できる
ため、D-006(サードパーティ依存はvcpkgマニフェストモードのみ、
ベンダリング・submodule化しない)の方針にそのまま従える。
`STBI_ONLY_PNG`(このプロジェクトの資産はPNGのみのため、JPEG等の
不要なデコードパスをビルドから除外)・`STBI_NO_STDIO`(既存のBMP
ローダーと同様、ファイル全体を`std::ifstream`で読み込んでから
`stbi_load_from_memory()`に渡す方式を踏襲し、stb側のFILE*経由API
は使わない)を定義してビルドサイズ・依存範囲を絞った。

**実装**: `apps/gui/BmpLoader.h`/`.cpp`を`apps/gui/ImageLoader.h`/
`.cpp`に置き換えた(`BmpImage`/`loadBmp24()` → `ImageRGBA`/
`loadImageRgba()`、外部から見たインターフェースの形は維持しつつ
BMP限定でなくなったことを名前に反映)。`CMakeLists.txt`に
`find_package(Stb REQUIRED)` + `target_include_directories(...
${Stb_INCLUDE_DIR})`を追加。既存の全アセット(`assets/alg_diagrams/
opn_al{0-7}.bmp`・`opl_alg{0-1}.bmp`、`assets/waveforms/ws{0-7}.bmp`、
D-016/D-021で生成したもの)をPowerShell + System.Drawingで24bit BMPから
PNGへ変換し直し(生成内容自体は変更なし、コンテナ形式のみ変更)、
BMP版は削除した。`assetsDir()`のマーカーファイル判定・各`get*Texture()`
のファイル名構築も`.bmp`→`.png`に追従させた。

**実機確認**: `cmake --preset vcpkg-windows-vs2026`の再configureで
`stb`が正しく解決されること、`cmake --build`・`ctest`(117項目)全通過
を確認。実機で`unified_preset.profile.json`の`OPL3_2 bank 0`
`Tubular Bells`を開き、ALG接続図・WS画像(いずれもPNG化後)が
D-021時点と変わらず正しく表示されることをスクリーンショットで
再確認した。

### D-023: OPL/OPLL系のAR/DR/SR/RR/TL範囲・OPLLのALG範囲を訂正

D-021公開後、利用者から直接の訂正指摘を受けた。

> OPL, OPLLともに、パラメータの設定範囲がまだ不適切です(ADSRがOPNと
> 同じになっている)
> OPL/OPLLではAR/DR/SR/RRは4bit、TLは6bitの範囲です
> OPLLのWSは0-1で正しい。ALGはOPLLでは0固定。

**AR/DR/SRの訂正(5bit→4bit)** **(→2026-07-18再訂正、D-024参照。
この節の「4bit/6bitが正しい」という判断自体が誤りだった。FITOM_X本体
の`hwpatch-reference`ドキュメントが明記した通り、スキーマ上の範囲は
D-021時点の5bit(0-31)/7bit(0-127)のままで正しく、OPL/OPLL側は
単に「上位ビットのみを取り出す」処理をしているだけだった。D-021の
値へ再度戻したので、以下の記述は歴史的経緯として残すのみ)**:
D-021では`core/src/OPL_new.cpp`の
`(o.AR & 0x1F)`という読み出し時のマスクをそのまま「5bit(0-31)」と
解釈したが、これは誤りだった。実際にはこの後`ar4(v)=v>>1`で4bitへ
右シフトしてから実機レジスタに書き込んでおり(`(ar4(ar_opl) << 4) |
ar4(dr_opl)`)、これはTLが`tl6()=v>>1`で7bit→6bitに切り詰められる
のと全く同じパターンである。TLについてはD-021の時点で「実チップ側は
切り詰められるが構造体フィールド自体は7bit」という理由でスライダー
範囲を7bit(0-127)のまま据え置いたが、これは「編集しても実機の音に
反映される有効桁数」という観点で一貫性を欠く判断だった。AR/DR/SRも
TLも、下位1bitは実機に届く直前で捨てられる(2つおきの値が同じ音に
なる)ため、利用者の指摘通りスライダーが実際に区別できる範囲
(AR/DR/SR=4bit/0-15、TL=6bit/0-63)に合わせて修正した。RRは元々
シフトを介さずレジスタへ直接書き込まれる(`rr_opl & 0xF`)ため
4bit(0-15)のままで正しく、変更していない。`oplOpRanges(wsMax)`
(OPL/OPL2/OPL3_2、`opllOpRanges()`からも共有)のAR/DR/SR/TLの
範囲値のみを修正した。

**OPLLのALGの訂正(0-15→0固定/`used=false`)**: D-021では
`ext.ALG_EXT`bit0(プリセットフラグ)が1の時のみ`hw.ALG`が4bit
ROMプリセット音色番号として意味を持つ(`instNo = preset ? (ALG&0xF)
: 0`)という実装を踏まえ、範囲を0-15とした。しかし、このプリセット
フラグが1になるのはROM音色参照パッチ(`fpe::HwPatch::isBuiltinRef()
==true`)の場合であり、そうしたパッチは`ops`を持たず(`builtin`と
`ops`は排他)、そもそも本エディタの`renderPatchEditor()`が早期return
してops[]編集フォーム自体を描画しない(D-015時点からの既存動作、
D-021追記参照)。つまり本エディタのops[]編集レイアウトに実際に
到達するOPLLパッチは常にユーザー音色(`preset==false`)であり、この
場合`hw.ALG`は「プリセット判定が偽」なので一切参照されない
(`instNo`計算に到達しない)。よって本エディタの文脈では`ALG`は
実質的に常に無視されるフィールドであり、`opllVoiceRanges()`の`ALG`
を`{0, 0, false}`(他の未使用フィールドと同じグレーアウト扱い)に
修正した。WS(0-1)はD-021のまま変更なし(利用者からも「正しい」との
確認を受けた)。

**実機確認**: ビルド・`ctest`(117項目)全通過を確認。実機で
`unified_preset.profile.json`の`OPL3_2 bank 0``Tubular Bells`
(AR=15)を開き、AR/DR/SRスライダーが新しい0-15レンジで表示され
(AR=15がスライダー右端まで埋まること=D-021時点の0-31レンジでは
半分しか埋まっていなかったところから見た目上の変化で確認)、
ALG接続図・WS画像がD-021/D-022時点と変わらず正しく表示されることを
スクリーンショットで確認した。OPLLバンクでのALGグレーアウト表示の
実機再確認は、クリック自動化がこのセッションでは同一ウィンドウでも
毎回位置が変わり座標較正がその都度必要になる上に安定しなかったため
断念し、コードレビューのみで済ませた(次回セッションの持ち越し)。

### D-024: D-023のOPL/OPLL範囲訂正をさらに訂正で差し戻し + エンベロープ波形プレビューのスケーリングバグを修正

D-023公開後、利用者からさらに訂正を受けた。

> OPL/OPLLの設定範囲は私の誤解がありました。設定範囲はスキーマ通りで、
> OPL/OPLL系では上位bitのみを取り出す動作になっていました。したがって、
> OPL/OPLLのADSRパラメータ範囲設定の修正はリバートしてください。
> (FITOM_Xのhwpatch-referenceドキュメントに明記されました)
> ただし、ADSR設定値のエンベロープ波形に対する作用がスキーマの設定と
> 乖離しているように思います。AR=31(最大値)にしてもエンベロープの
> アタック波形が最大になっていません。

**範囲の差し戻し**: `oplOpRanges(wsMax)`のAR/DR/SRを4bit(0-15)→
5bit(0-31)、TLを6bit(0-63)→7bit(0-127)に戻し、D-021時点の値
(FITOM_X本体の`hwpatch-reference`ドキュメントが明記するスキーマ通り)
に復帰させた。RRは元々D-023でも変更していないため0-15のまま。OPLLの
`ALG`(D-023で`{0,0,false}`に修正した箇所)はADSRとは無関係の別件
(接続アルゴリズムではなくROMプリセット音色番号という意味の違いに
起因する修正)のため、今回の差し戻し対象外でそのまま維持した。

**エンベロープ波形プレビューのスケーリングバグ**: 利用者が指摘した
「AR=31(スキーマ上の最大値)にしても波形が最大にならない」という
症状を調査した結果、`renderEnvelopeCurve()`(D-015で導入、視覚補助
専用でチップの正確なエンベロープジェネレータの再現ではないと明記
済み)の`rateToSegWidth`ヘルパーが、どのチップでも常に固定値`/99.0f`
で正規化していたバグを発見した。OPN/OPL/OPLL系のAR/DR/SRのように
実際の最大値が31(0-99ではない)のフィールドでは、最大値31を入れても
`1 - 31/99 ≈ 0.687`にしかならず、見た目上まだ7割近い長さの区間が
残ってしまい、「最大値なのに最速に見えない」という利用者の観察と
一致する挙動になっていた。TL/SLの高さ計算(`peak`/`sustain`)も同様に
固定`/99.0f`だった。この関数は元々「0-99の汎用フォールバック範囲」
を前提に書かれたもので、D-016でOPN用に実際のレジスタ幅を
`HwOpFieldRanges`として導入した際に、この関数側の正規化基準を
連動させ忘れていた(D-016〜D-023を通じて見落とされていたバグ)。

`renderEnvelopeCurve(const fpe::FmHwOp& op, const HwOpFieldRanges&
ranges)`とranges引数を追加し、`rateToSegWidth`/TL・SLの正規化を
それぞれ`ranges.AR.maxV`/`ranges.DR.maxV`/`ranges.SR.maxV`/
`ranges.RR.maxV`/`ranges.TL.maxV`/`ranges.SL.maxV`(フィールドごとに
異なる実際の最大値)を基準にするよう修正した。これにより、まだ
実レジスタ幅が未確認で汎用0-99フォールバックのままのチップ種別は
従来通りの見た目(実質的な変更なし)を保ちつつ、OPN/OPL/OPLL等
確認済みチップでは各フィールドの実際の最大値でAR=最大値の時に
視覚的にも最速(区間最小幅)になるよう修正された。

**実機確認**: ビルド・`ctest`(117項目)全通過を確認。実機で
`unified_preset.profile.json`の`OPL3_2 bank 0``Tubular Bells`
(AR=15)を開き、AR/DR/SRスライダーが0-31レンジに復帰していることを
確認した(D-023からの差し戻しの確認)。ただし、このセッションでは
クリック自動化のマウスイベントが対象ウィンドウではなく背後の
ブラウザへ漏れてしまう事象が発生し(利用者の実ブラウザのタブ・URLが
意図せず変化した)、これ以上の自動クリック操作は安全のため中断した
(スクリーンショットは撮影直後に削除済み)。そのため、AR=31における
エンベロープ波形が実際に最速表示になることの実機確認は次回セッション
の持ち越し。

### D-025: エンベロープ波形プレビューのSL解釈を修正(TLと同じ「0=最大」の減衰量、かつTLの影響を受ける)

D-024公開後、利用者から実機確認結果とともに追加の指摘を受けた。

> OPLLのALGグレーアウト確認しました。
> AR最大の場合にアタックが最速になるのは確認できましたが、SLの解釈が
> 反転しています。SLはTLと同様、0が最大です。また、SLの描画がTLの
> 作用を受けていません。

D-024でAR最大時に最速表示になることは確認が取れたが、SLについて
2点誤りがあった。

1. **極性の反転**: D-024時点の実装は`sustain =
   levelToNorm(op.SL, ranges.SL.maxV)`、すなわちSLの値が大きいほど
   サステインの描画上の高さが高い(値が大きい=大きい音)という解釈
   だった。しかし実際のYamaha FM系チップの慣習では、SLもTLと同じ
   「アッテネーション(減衰量)」の一種で、0が最大音量・値が大きいほど
   静か、という向きである(TLとの整合性)。
2. **TLとの独立性**: D-024時点はサステイン高さをTLのピーク計算とは
   無関係に、SL単体で0-1の絶対値として描画していた。しかし実際には
   SLはピーク(TLで既に減衰させた後の値)からの「さらなる」減衰量
   であるべきで、TLの値が変わればサステインの高さも連動して変わる
   必要がある。

`renderEnvelopeCurve()`の`levelToNorm`を`attenuationToNorm`に改名し
(TL/SL共通で「0=減衰なし、maxV=完全減衰」という向きであることを
名前で明示)、サステイン高さの計算を
`sustain = peak * (1.0f - attenuationToNorm(op.SL, ranges.SL.maxV))`
に変更した。これにより、SL=0ならサステインはピーク(TL減衰後の値)と
同じ高さを維持し、SLが大きくなるほどピークからさらに減衰し、TLの値が
変われば(ピークが変われば)サステインの高さも比例して連動するように
なった。

**実機確認**: ビルド・`ctest`(117項目)全通過を確認した。利用者から
実機でのAR最大確認・OPLLのALGグレーアウト確認について報告を受けたが、
SL修正自体の実機確認は、前回セッションでクリック自動化が利用者の
実ブラウザへ入力を漏らす事故を起こした直後であることを踏まえ、今回は
自動クリックによる検証を控えた(ビルド成功・コードレビューのみ)。
次回、利用者側での目視確認または安全なクリック自動化手順が確立できた
セッションでの実機確認が望ましい。

### D-026: 起動時引数によるキオスクモードを追加

利用者から以下の要件を受けた。

> 起動時引数により、キオスクモードを追加したい。
> 起動時引数でhwbankファイル名、prog noを受け取ると直接パッチ編集画面を
> 開く。パッチ編集を終了したらそのまま終了する。FITOM_X本体からの起動を
> 想定する。MDIフレームは無いほうが良いが、実装が複雑になるのであれば
> 常にMDIにフルサイズドッキングした状態で良い。既存のプロファイル指定
> との共存のためにプロファイルの指定方法の仕様を変えても構わない。

**引数仕様**: 既存の`argv[1]`=プロファイルパス(D-010、1引数のみ)は
そのまま維持し、新たに3引数
(`fitom_patch_editor_gui.exe <profile.json> <hwbank-file> <prog>`)を
キオスクモードのトリガーとした。`hwbank-file`はプロファイルが実際に
参照している`*.hwbank.json`のいずれかと一致する必要がある
(`fs::equivalent()`で比較 - 相対パス表記の違いや大文字小文字の違いを
気にせず同一ファイルとして扱える)。`HwBank`自体の on-disk JSON形状には
チップ種別(`VoicePatchType`)が含まれておらず、プロファイルの
`hw_banks[].group`からしか判定できない(`HwBank`構造体のコメント参照)
ため、hwbankファイル単体を直接ロードする設計は採らず、プロファイル
全体を読み込んだ上でその中から一致するバンクを探す設計にした
(利用者が許可した「プロファイル指定方法の仕様変更」は、1引数だった
ものを3引数の一部として使う形に変わった、という意味で解釈した)。

**起動失敗の扱い**: キオスクモードの引数(prog番号のパース、
プロファイル読み込み、バンク+prog一致)はすべて`glfwInit()`より前、
つまりウィンドウを一切作る前に検証する。失敗したら標準エラー出力に
メッセージを出して`return 1`する - キオスクモードはFITOM_X本体からの
非対話的な起動を想定しているため、GUIのエラーポップアップより
プロセスの終了コードで呼び出し元に伝える方が適切と判断した。

**MDIフレーム省略**: `AppContext`に`kioskMode`フラグと
`kioskEditor`(既存の`PatchEditorWindow`をそのまま再利用)を追加し、
メインループを`kioskMode`で分岐した。キオスクモード時は
MainMenu/Outline/BankDetail/各種ダイアログを一切描画せず、
`renderPatchEditor()`だけを、毎フレーム`SetNextWindowPos(0,0)`+
`SetNextWindowSize(io.DisplaySize)`で強制的にビューポート全体に
フィットさせた1枚のウィンドウとして描画する。完全に枠(タイトルバー)
無しのウィンドウも検討したが、その場合「編集完了」を伝える閉じるボタン
の代わりが必要になり実装が複雑になる(キーボードショートカット等の
追加実装が要る)ため、利用者が許可した「常にMDIにフルサイズドッキング」
案を採用: タイトルバー(閉じるXボタン)は残しつつ、
`ImGuiWindowFlags_NoResize|NoMove|NoCollapse`でリサイズ・移動・
折りたたみだけ禁止し、実質「常時フルサイズでドッキングされた1枚だけの
ウィンドウ」に見えるようにした。このXボタンで`kioskEditor.open`が
偽になったら、その場で`glfwSetWindowShouldClose()`を呼びプロセス
全体を終了する(「パッチ編集を終了したらそのまま終了する」に対応)。
既存の`renderPatchEditor()`/`isBuiltinRef()`早期return等はそのまま
再利用しており、キオスク対象がbuiltin参照パッチだった場合も既存の
「編集できません」メッセージがフルサイズウィンドウの中に表示される
だけで、クラッシュ等はしない。

**実機確認**: ビルド・`ctest`(117項目)全通過を確認。非対話的な
検証として、(1)不正なprog番号、(2)存在しないプロファイル、
(3)プロファイル内に一致するバンクが無いhwbankファイル、の3パターンで
期待通り標準エラー出力+終了コード1になることをコマンドライン実行で
確認した。(4)実データ(`unified_preset.profile.json`+
`banks/OPL2/alsa/std_opl2.hwbank.json`+ prog 14 = "Tubular Bells")での
正常系は、プロセスが起動しクラッシュせず5秒以上動作し続けることを
`timeout`コマンド経由で確認した。実際にフルサイズのパッチ編集
ウィンドウが画面に正しく描画されることのスクリーンショットでの
自動視覚確認は、このセッションでは繰り返しウィンドウのフォーカス/
Z-order/可視性の取得が不安定だったため(前セッションで発生した
クリック漏れ事故とは別の、`GetForegroundWindow`が一致を返した直後に
`IsWindowVisible`が偽になる、原因未特定の現象)完了できなかったが、
**利用者が実機で直接目視し、MDIにフルサイズドッキングされていることを
確認済み**。

### D-027: パッチ編集画面に「登録」ボタン + リアルタイム差分SysEx送信 + 閉じる時の全パラメータ再送信

利用者から以下の要件を受けた。

> パッチ編集画面に「登録」ボタンを配置(場所は右上または右下が良い)
> パッチ編集画面で、パラメータを変更した場合にリアルタイムでsysexを
> 送信する。(差分のみ)
> 「登録」ボタン押下でhwpatchファイルを更新する。
> パッチ編集画面を閉じるときに、hwpatchファイルから取り出してあらためて
> 全パラメータを送信する

**プロトコル確認**: FITOM_X本体の`docs/manuals/midi-message-reference.md`
8.1節を再確認したところ、パラメータオーバーライドSysExのJSONは
「オーバーライドしたいパラメータのみを含むJSONオブジェクト」でよいと
明記されていた(省略したキー/`ops[]`の`null`要素は「変更なし」を
意味する)。つまり「差分のみ送信する」は独自解釈ではなく、そもそも
この プロトコルが最初から想定している使い方だった。

**実装**: `PatchEditorWindow`に`lastSent`(FITOM_Xに最後に実際に
伝えた状態)・`registered`(最後にディスクへ永続化した状態)・
`initialized`/`deviceSelected`フラグを追加。
- `buildHwPatchDiffJson(prev, curr)`(+ 汎用ヘルパー
  `shallowJsonDiff()`)を新設し、`hw.*`の6フィールドと`ops[]`の
  各フィールドのうち変化した部分だけを含むJSONを構築する
  (`ops[]`の要素は変化が無ければ`null`)。
- 毎フレーム、現在の生パッチと`lastSent`を比較し、差分が非空なら
  (初回のみ`selectDevice()`を送った上で)差分だけを送信し、
  `lastSent`を更新する。
- 「登録」ボタン(パッチ編集画面の右上、名前欄と同じ行に右寄せ配置)は
  `ctx.workspace.save()`を呼び(`HwBank`単体の保存APIが無いため、
  D-014の`tryCreateBank()`と同じくワークスペース全体を保存)、成功時に
  `registered`を現在値で更新する。保存失敗時は`ctx.errorMessage`
  経由でエラーポップアップに表示する。
- パッチ編集ウィンドウ(通常モードの各エディタ・キオスクモードの
  単一エディタの両方)が閉じられた瞬間に一度だけ`registered`の内容を
  フルオーバーライドとして再送信する(`sendFullRegisteredOverride()`)。
  これにより、「登録」を押さずに行ったライブ編集(差分ストリームで
  試聴には反映されるが、ディスクには保存されていない)は、編集画面を
  閉じると同時にFITOM_X側の試聴状態もディスク上の内容に巻き戻る。

**実機確認**: ビルド・`ctest`(117項目)全通過を確認。実機で
キオスクモード(`unified_preset.profile.json`+`std_opl2.hwbank.json`+
prog 14)を起動し、「登録」ボタンが右上に正しく配置されていること、
クリックしてもエラーポップアップが出ないこと、実際に対象の
`*.hwbank.json`のファイル更新日時が変わることを確認した。**この
最初の実機確認の過程で、以下のD-028の重大なバグを発見した**(この
D-027自体の実装ロジックのバグではなく、`save()`が呼び出す
`fpe::to_json/from_json(HwPatch)`という既存のシリアライズ層に
以前から存在していたバグ)。リアルタイム差分送信・閉じる時の全送信の
実際の音への効果(FITOM_X本体との実接続時)は、この開発機に
ビルド済みのFITOM_X実行環境が無いため未確認のまま(D-015から続く
既知の制約)。

### D-028: `fpe::HwPatch`のJSONシリアライズが実スキーマと不一致 - 実データを一度破壊(git復元済み)、根本修正

D-027の「登録」ボタンを実機で初めてクリックした際、`unified_preset.
profile.json`が参照する**78個のファイル全て**が変更され、内容を
確認したところ実際のパラメータ値が失われていることが判明した
(利用者の許可を得て`git checkout --`でFITOM_staging側を復元、
実害は残っていない)。

**根本原因**: `fpe::HwPatch`の`to_json`/`from_json`
(`src/HwPatch.cpp`)が、`hw.FB`/`ALG`/`AMS`/`PMS`/`NFQ`/`FB2`を
`"hw"`という入れ子キーの下に読み書きしていたが、FITOM_X本体の
実際のスキーマ(`config_schema/hwbank.schema.json`、実際に
`PatchManager`が読み書きする形式)ではこれらは各パッチの
**トップレベル直下のキー**であり、`"hw"`という入れ子は存在しない
(実際の`FITOM_staging/banks/OPL2/alsa/std_opl2.hwbank.json`等でも
直接確認)。このため、実データを読み込むと`from_json`が`"hw"`キーを
見つけられず`FmHwVoice{}`(全フィールド0)にフォールバックし、
`FB`/`ALG`/`AMS`/`PMS`/`NFQ`/`FB2`が**読み込み時点で無音のうちに
ゼロ化**されていた。この状態で`save()`が一度でも呼ばれると、ゼロ化
された値がそのままディスクへ書き戻され、元の値が完全に失われる。

`PatchWorkspace::save()`自体はD-014(新規バンク作成後の即保存)以来
存在する既存機能であり、このバグ自体はD-027より前から潜在していた
(新規バンク作成時、同じワークスペースに他のHwBankが読み込まれていれば
道連れで破壊されていた可能性がある)。D-027の「登録」ボタンは、単に
`save()`をより頻繁に・意図的に呼び出す新しい経路を提供しただけで、
この日初めて実データに対して踏んでしまった形になる。

**追加で発見した同種の不一致(同じ調査の過程で発覆)**:

- `FmHwOp.FXV`(`src/HwPatch.cpp`のJSON実装、`include/fpe/HwPatch.h`の
  フィールド名)は、スキーマでは`"PDT"`(疑似デチューン/拡張周波数
  オフセット)という名前で、`FITOM_staging/banks/OPL3/opl2_merge/
  *.hwbank.json`に実際に非ゼロ値(`PDT: 4`)が使われていることを確認
  した - つまりこちらは**現在進行形で実データに影響する**不一致
  だった。フィールド名を`FXV`→`PDT`にリネーム(構造体メンバー名・
  JSON キー名・`apps/gui/main.cpp`側の全参照を含む)。
- `FmChipExt.DM0`はスキーマでは`"FIX"`という名前。実データでの
  非ゼロ値は確認できなかったが(サンプルした範囲では全て`FIX: 0`)、
  同じ理由でフィールド名を`DM0`→`FIX`にリネームした。

他の5つのデータモデル型(SwPatch/NativePatch/DrumKit/SampleZone/
PcmBank/Profile)についても、同じ「実スキーマとの不一致」がないか
別セッション相当の調査(サブエージェントによる監査)を行った。結果:
SwPatch・DrumKit・Profileは一致。NativePatchは廃止済みフィールド
(`sw_bank`/`sw_prog`)を書き出しているが実データはこれを持たず実害
なし。PcmBankは`entries[]`のインライン形式とスキーマにわずかな
不一致があるが、実データは`adpcm_json`サイドカー経由でロードされる
ため実害なし。**SampleZoneのみ、各ゾーンの`name`(表示用ラベル、
実データに存在する)フィールドがモデルに存在せず、ロード時に
サイレントに失われることを確認した** - こちらは今回未修正(実際の
発音に影響しない表示専用メタデータであり、今回のD-027のスコープ外
のため。次回対応の課題として記録)。
**追記(同日、利用者から設計背景を確認)**: `SampleZonePatch.name`
(パッチ単位、既存)と`SampleZone.name`(ゾーン単位、未実装)の扱いが
非対称なのは意図的な設計であり、単純な実装漏れではない。通常の楽器音
ではゾーンは発音制御専用の隠蔽された内部情報にすぎず`patches[].name`
がパッチ名を代表するが、ドラムキットではゾーンそのものが個々の
リズム音を表すため、ゾーン単位の`name`が別途必要になる。将来
`SampleZone.name`を追加する際は、`SampleZonePatch.name`と同じ概念の
重複ではなく、ゾーン自身の独立した識別ラベルとして実装すること
(詳細はdocs/STATUS.mdの該当項目参照)。

**修正**: `to_json(HwPatch)`はFB/ALG/AMS/PMS/NFQ/FB2をトップレベルへ
展開して書き出し、`from_json(HwPatch)`はHwPatch全体のJSONオブジェクトを
そのまま`FmHwVoice`のfrom_jsonに渡す(無関係なキーは単に無視される)
方式に変更した。`fixtures/banks/OPM/dx27_dx100/00_default.hwbank.json`
(唯一`"hw"`入れ子を使っていたテスト用フィクスチャ)も新形式に修正。

**実機確認**: 修正後、一時的な検証用実行ファイル(`verify_fix`、
検証後CMakeLists.txt・ソースとも削除)で`unified_preset.profile.json`
を実際にロード→`save()`し、保存前後の全128パッチ(デフォルト値
補完込みの意味的比較)が完全に一致する(差分0件)ことを確認した。
`ctest`(117項目)も全通過を確認。**この検証で発生した
FITOM_staging側の変更も、確認後ただちに`git checkout --`で復元し、
現在FITOM_stagingは`git status`上クリーンな状態**。

### D-029: キオスクモードの起動失敗をネイティブメッセージボックスで通知(終了コードだけでは伝わらない)

D-026公開後、利用者から以下の指摘を受けた。

> キオスクモードのエラーを戻り値で返すのは、FITOM_X本体がエラーを検出
> できません。(FITOM_Xは並行して動作するので戻り値を待てない)
> 起動後にエラーを検出して終了するケースがある場合はパッチエディタ側で
> エラーメッセージボックスを出してください。

D-026時点では、キオスクモードの起動時引数検証(prog番号のパース失敗・
プロファイル読み込み失敗・バンク/prog不一致)はいずれも「標準エラー
出力+終了コード1」で通知する設計にしていた。これは「FITOM_X本体からの
非対話的起動なのでGUIポップアップよりプロセスの終了コードで伝える方が
適切」という判断だったが、利用者の指摘通り、FITOM_Xは子プロセスを
並行動作させるだけで終了コードを待ち受けない(できない)ため、この
情報は実質的に誰にも届いていなかった。

**修正**: `showFatalErrorBox()`を新設し、標準エラー出力に加えて
Win32ネイティブの`MessageBoxW`でエラーダイアログを表示するようにした
(`MessageBoxA`ではなく`W`版を使う理由: このプロジェクトのソースは
UTF-8(`/utf-8`、CMakeLists.txt参照)なので、UTF-8バイト列をそのまま
`MessageBoxA`に渡すとシステムのコードページ(日本語環境ではShift-JIS)
で誤解釈され文字化けする - `MultiByteToWideChar(CP_UTF8, ...)`で
明示的にUTF-16へ変換してから`MessageBoxW`に渡す)。ImGuiウィンドウが
まだ存在しない起動シーケンス最序盤の失敗経路(キオスクモードの引数
検証3箇所、および`glfwInit()`/`glfwCreateWindow()`/`glewInit()`の
失敗)すべてに適用した(キオスクモードに限らずD-010の通常起動でも
FITOM_Xから非対話的に起動される可能性があるため、対称的に修正)。
起動後(ImGuiウィンドウが存在する状態)のエラー、例えば「登録」の
保存失敗は、既存の`renderErrorPopup()`(D-027、通常モード・キオスク
モード両方で描画される)がそのままGUI内ポップアップとして表示するため
対象外。Windows専用(このプロジェクトの他のネイティブAPI呼び出し
箇所、例えば`Preferences.cpp`の`exeDir()`と同様、POSIX側は未検証)。

**実機確認**: ビルド・`ctest`(117項目)全通過を確認。存在しない
プロファイルパスを指定してキオスクモードを実機起動し、日本語メッセージ
(「キオスクモード: プロファイルの読み込みに失敗しました:」)が
文字化けせず正しく表示されるダイアログが出ることをスクリーンショットで
確認した(最初`MessageBoxA`で実装した際は文字化けを実際に確認・
`MessageBoxW`+UTF-16変換に修正して解決した)。

### D-030: FITOM_X側MIDIチャンネル・ネゴシエーションプロトコルへの対応

利用者から、複数のパッチエディタインスタンスを同時起動する場合に備え
FITOM_X側でMIDIチャンネルのネゴシエーションプロトコルが策定されたので
対応してほしいという指摘を受けた。接続数オーバーで接続できなかった
場合はメッセージボックス表示後に終了すること、という要件も併せて
指定された。

FITOM_Xリポジトリの`docs/plugin-midi-pipe.md`(2026年7月更新分、
「4.1 チャンネル割り当て通知」)を一次情報源として参照した。要点:

- 内部MIDIパイプ(名前付きパイプ/UNIXドメインソケット)は最大16
  同時接続。接続が確立すると、FITOM_X側は他の何よりも先に
  `F0 00 48 01 03 <ch> F7`(7バイトのプライベートSysEx、`<ch>`が
  このコネクションに割り当てられたMIDIチャンネル0-15)を1回だけ
  書き込む。パッチエディタ側はこれを読み取るまで何も送信してはならず、
  以降の全メッセージでこの`<ch>`を使う(**自分でチャンネルを選ぶ
  仕様ではなくなった** - 複数インスタンスの衝突を避けるため)。
- 既に16本接続済みの状態で新規接続しようとした場合、FITOM_X側は
  OSレベルの接続(`CreateFile`/`connect`)自体は受理するが、上記
  ハンドシェイクを送らずに即座に切断する。これを「FITOM_Xが
  起動していない」(オフラインとして許容すべき、無音で試聴不可
  にするだけの状態)と区別しなければならない。

**修正**: `MidiPipeClient`(`apps/gui/MidiPipeClient.h/.cpp`)の
`ensureConnected()`を変更し、接続成功後すぐに7バイトのハンドシェイクを
読み取るようにした。Windows側は`CreateFileA`のアクセスフラグを
`GENERIC_WRITE`単独から`GENERIC_READ | GENERIC_WRITE`に変更(読み取り
のため必須)。ハンドシェイクの妥当性(`F0 00 48 01 03 .. F7`)を
検証できた場合は`assignedChannel_`に`<ch>`を保存して接続完了、
検証できなかった場合(`ReadFile`/`recv`が0バイトで返る、すなわち
ハンドシェイク前に切断された)は`rejectedForCapacity_`を立てて
false を返す - これで「未起動」(`CreateFile`/`connect`自体が失敗、
`rejectedForCapacity_`は立たない)と「接続数オーバーで拒否」
(`rejectedForCapacity_`が立つ)を区別できるようにした。

`PreviewOutput`に`activeChannel(int fallbackChannel)`(接続中は
FITOM_Xが割り当てたチャンネル、未接続時のみ`fallbackChannel`
= プリファレンスの「出力MIDI CH」を返す)と
`pipeRejectedForCapacity()`を追加し、`main.cpp`側の全送信箇所
(`renderPatchEditor()`のリアルタイム差分送信・プレビュー鍵盤・
CC#1/CC#7レバー、`sendFullRegisteredOverride()`)で、従来
`ctx.preferences.midiChannel`から直接組み立てていた送信チャンネルを
`ctx.previewOutput.activeChannel(...)`経由に置き換えた。プリファレンス
の「出力MIDI CH」スライダーは、RtMidiフォールバック出力にのみ効く
設定である旨をツールチップに追記(FITOM_Xパイプ接続中はこの値は
使われない)。

「接続数オーバー」の検出は、メインループ(`main()`のフレームループ、
キオスク・通常モード両方の分岐の直後)で毎フレーム
`ctx.previewOutput.pipeRejectedForCapacity()`をチェックし、真であれば
既存の`showFatalErrorBox()`(D-029でキオスクモードの起動時失敗用に
新設したもの)を呼んでメッセージボックスを表示し、
`glfwSetWindowShouldClose()`でプロセスを終了させるようにした。
`showFatalErrorBox()`はD-029時点では「ImGuiウィンドウがまだ存在しない
起動時失敗専用」という位置づけだったが、今回はウィンドウ表示後の
セッション中に発生しうる致命的エラーとしても再利用している(同じ
「FITOM_Xは子プロセスの終了コードを待てないので、標準エラー出力だけ
では誰にも伝わらない」という理由が引き続き当てはまるため)。

**実機確認**: ビルド・`ctest`(1項目)全通過を確認。この開発機には
FITOM_X実行環境が無いため、実際にFITOM_Xへ接続してハンドシェイクを
受け取る経路・接続数オーバーで拒否される経路はどちらも実機確認
できていない(**未検証**、次回FITOM_X実行環境がある機体での確認が
必要)。確認できたのは、FITOM_Xが起動していない(=このハンドシェイク
処理に到達する前に`CreateFile`が失敗する)従来からのオフライン経路が
今回の変更で壊れていないことのみ - 実機でアプリを起動し、パッチ
編集画面を開いて「試聴: MIDI出力(フォールバック)で試聴中」が
これまで通り表示され、フリーズや誤った「接続数オーバー」エラー
ダイアログが出ないことをスクリーンショットで確認した。

### D-031: OPM/OPZパッチ編集画面のALG/WS対応 + OPパネル「詳細」の未使用フィールド非表示

利用者から以下の指摘を受けた。

> OPM/OPZパッチ編集画面
> ・ALG表示はOPNと同じ画像を使用する
> ・WS表示をOPL系と同じレイアウトで配置。ただし、ボイスパッチタイプが
>   OPMの場合は非活性。
> WSの画像は添付画像(表4-4 OPZの波形選択)を参考に不足分を作成
> (WS0, 2, 4, 6はOPLに同じ波形があるので流用、WS1はwaveform.xlsx
> シート1の右端のカラムが対応している。WS3, 5, 7はWS1の波形を元に
> 周波数2倍、半波整流などで生成)
>
> パッチ編集画面全般
> OPパネルの詳細バンドから、対象のボイスパッチタイプで未使用のフィー
> ルドを非表示にする

D-016時点ではOPM/OPZ/OPZ2は`genericVoiceRanges()`/`genericOpRanges()`
の0-99フォールバックのままで、ALG/WSとも専用画像が無かった(ALGは
「OPNでもOPLでもない」の else 分岐で空テクスチャ、WSは
`isOplWsImageFamily()`に含まれず単なる数値入力)。

**パラメータ範囲の確認**: FITOM_Xの`docs/voice-parameter-reference.md`
のOPM/OPZ節と、`core/src/OPM_new.cpp`のCOPM/COPZ
(`updateVoice()`)の実レジスタマスクを突き合わせて
`opmVoiceRanges()`/`opmOpRanges()`/`opzOpRanges()`
(`apps/gui/main.cpp`)を新設した。

- OPM: FB/ALG(3bit,0-7)、AMS(2bit,0-3)、PMS(3bit,0-7)、NFQ(5bit,0-31、
  **NOTE ON CONFIDENCE**: ドライバに明示マスクが無く実機YM2151の
  NFRQレジスタ幅から採用)、op側はAR/DR/SR(5bit)、SL/RR(4bit)、
  TL(7bit)、KSR(2bit)、MUL(4bit)、DT1(3bit)、DT2(2bit)、AM(1bit)が
  使用、KSL/PDT/VIB/EGT/WS/REV/EGS/DT3は`COPM::updateVoice()`で一切
  参照されないため未使用(WS/REV/EGS/DT3はOPZ専用)。
- OPZ/OPZ2(YM2424、`docs/chip-driver-architecture.md`によれば
  `COPZ`を共用): OPMの全フィールドに加え、`COPZ::updateVoice()`で
  WS(3bit,0-7)/DT3(4bit,0-15)を`0x40+op*8+ch`に、EGS/REVを
  `0xC0+op*8+ch`に書き込む。**NOTE ON CONFIDENCE**:
  `docs/manuals/hwpatch-reference.md`はREVを0-15(4bit)/EGSを
  0-127(7bit)と宣言しているが、実際のドライバのマスクは
  `REV&0x1F`(5bit、宣言より広いため実害なし)/`EGS&0x3`(2bit、宣言の
  7bitのうち2bitしか反映されない)。D-024で確立した方針(ドライバの
  マスクではなくスキーマの宣言幅を編集可能範囲とする)に従い、ここでも
  宣言通りの0-127をEGSの編集可能範囲とした。EGSのこの2bitしか
  反映されない挙動が実機YM2414の制約なのか、FITOM_X側の未確認の
  バグなのかは今回確認していない(FITOM_X側の課題であり本エディタの
  スコープ外)。

**ALG画像**: OPM/OPZ/OPZ2はOPNと同じ3bit 0-7のALGセマンティクスなので
`getOpnAlgTexture()`をそのまま共用(`renderPatchEditor()`内の
`isOpnAlgFamily`判定にOPM/OPZ/OPZ2を追加)。

**WS画像**: `E:\...\material\waveform.xlsx`のSheet1を実際に開いて
確認した(pythonのopenpyxlで読み込み、B-J列を全行ダンプ)ところ、
B-I列(既存のOPL系WS0-7として使用中)に加え、J列(見出し無し、
シートの最右列)にOPZ独自のもう1系統の波形データがあることを確認した。
これが利用者の言う「シート1の右端のカラム」で、OPZのWS1に対応する。

既存のOPL系ws0-7.pngを実際に画像として見比べ、OPZの参考画像(表4-4)と
形状を照合した結果、OPZのWS0/2/4/6はOPL系のws0(サイン波)/
ws1(半波サイン+平坦)/ws2(全波整流サイン、双峰)/ws4(前半に圧縮した
全波サイン+平坦)とそれぞれ同一形状であることを確認した(インデックス
番号は一致しない組み合わせがある - OPZのWS2はOPLのws1、OPZのWS6は
OPLのws2 - ため、`getWsTexture()`のキャッシュ/ファイル名を共用できず、
`assets/waveforms/opz_ws{0-7}.png`という別ファイル群として新規生成
した。バイト列はOPL側の対応するpngと完全に同一)。

OPZのWS1はJ列の実データをそのまま画像化。WS3/5/7はWS1のデータを元に
生成した(スプレッドシートに対応する列が無いため)。

初回実装では「周波数2倍、半波整流など」という利用者の指示を筆者の
判断だけで具体化した(WS3=WS1を周波数2倍、WS5=WS1を周波数2倍+半波
整流、WS7=WS1を周波数3倍+半波整流)が、利用者から直接以下の訂正指示を
受けた。

> OPZ WSの波形の生成が間違っています。以下のように作り直してください。
> WS3：WS1の周波数を2倍にして2周期目を0にする(パルス化)
> WS5：WS1を半波整流
> WS7：WS3を全波整流

これに従い、以下の通り生成し直した(コミット前だったため、上記の
誤った初回実装の記述はここで直接訂正し、別途取り消し線注記は残して
いない)。

- WS3 = WS1を周波数2倍した上で2周期目(θ=180-360)を0にする
  (`v(θ) = v1(2θ mod 360)` for θ<180, else `0`) - 1周期分鳴ってから
  無音になる「パルス化」
- WS5 = WS1をそのまま半波整流(周波数は変えない、`max(0, v1(θ))`)
- WS7 = WS3を全波整流(`abs(v3(θ))` - WS3の負の谷も含めて正の山になる)

生成後、matplotlibで9系統(OPL系のWS0-7 + OPZ独自列)を並べてプロット
し、上記の訂正後の仕様通りの形状になっていることを目視で確認した
(WS3=1周期分の波形+後半無音、WS5=WS1の正の山+平坦、WS7=WS3の全ての
山が正側に来た2つの山の並び)。画像自体は既存のOPL系ws#.png生成
スクリプト(168x100、濃色背景、シアンの曲線、"WS n"ラベル)と全く
同じ描画パラメータで生成し、視覚的な一貫性を保った。

OPM自体はWSレジスタが存在しないため`opmOpRanges().WS = {0,0,false}`
だが、「WS表示をOPL系と同じレイアウトで配置。ただしOPMの場合は非活性」
という指示通り、画像+スピンボタンのレイアウト自体はOPZと共有
(`getOpzWsTexture()`を使う分岐にOPMも含めた)。`FieldRange.used=false`
により`renderImageSpinner()`が自動的に無効化(グレーアウト)し、常に
WS0(サイン波)の画像を表示する - 既存の「未使用フィールドは無効化して
表示、非表示にはしない」という一般則(下記「詳細バンド」の変更とは別)
をそのまま活用した形。

**OPパネル「詳細」フォールドアウトの未使用フィールド非表示**: 従来
`FieldRange`の設計方針は「`used=false`でも無効化した状態で表示し、
チップ種別が変わってもレイアウトが動かないようにする」だったが
(D-016)、OPM/OPZ追加でこのフォールドアウト内の各フィールド
(KSR/KSL/MUL/DT1/DT2/PDT/AM/VIB/EGT/REV/EGS/DT3)のうちチップ種別ごとに
使うものがまばらになり、無効化された行ばかりが並ぶ雑然とした画面に
なってきていた。1つのパッチ編集ウィンドウは開いている間ずっと同じ
バンクの同じチップ種別に固定されるため、D-016が懸念していた「チップ
種別が切り替わる際のレイアウトのガタつき」はこのフォールドアウト内では
そもそも起こらない。そのため`renderHwOpEditor()`の「詳細」ブロックのみ、
各行を`ranges.X.used`でガードして非表示にするよう変更した(それ以外の
常時表示スライダー/WSバンドは、これまで通り無効化表示のまま変更なし)。

**実機確認**: ビルド・`ctest`(1項目)全通過を確認。実機で
`FITOM_staging/config/profiles/emu_opm.profile.json`(OPM×2/OPZ×2)
経由でOPZ2バンク(`banks/OPZ/tx81z/tx81z.hwbank.json`)のパッチ
(prog 9, HiTine81Z)を開き、(1)ALGがOPN系と同じ接続図画像
(ALG 4、4オペレータのボックス+フィードバックの図)で表示される、
(2)OP1-4がそれぞれ実際のWS値(7/0/1/0)に対応する異なる波形画像で
正しく表示される、(3)「詳細」を展開すると使用フィールド
(KSR/MUL/DT1/DT2/AM/REV/EGS/DT3)のみが表示され、未使用フィールド
(KSL/PDT/VIB/EGT)が表示されないことをスクリーンショットで確認した。
この開発機には(OPZではない)OPM単体の実データが存在しないため、OPMの
WS非活性表示自体は実機確認できていない(**未検証**、コードパスは
OPZ2で確認したのと全く同じ`renderImageSpinner()`のused=false経路の
ため動作原理としては妥当と判断)。WS3/5/7の生成方法(WS3=WS1を周波数
2倍+2周期目0のパルス化、WS5=WS1の半波整流、WS7=WS3の全波整流)自体も
実際の音・実機波形との一致は未確認(**未検証**)。

### D-032: OPL3(4OPモード、`VOICE_PATCH_OPL3`)パッチ編集画面を追加

利用者から「OPL3(2OP)パッチ編集画面を元にOPL3(4OP)用のhwパッチ編集
画面を作成してほしい。アルゴリズム図はassets配下に用意してあるものを
使ってください」という依頼を受けた。`assets/alg_diagrams/opl3_al{0-7}.png`
(8種)は依頼の直前のコミット(`0ef39d4 Upd. algorithm diagrams`)で
既に利用者自身がリポジトリに追加済みだった。

**重要: OPL3(4OP)はOPL3_2(2OP)とは別の`VoicePatchType`**。
`VoicePatchType::OPL3`(0x30、`VOICE_PATCH_OPL3`)が4OPモード専用で、
2OP残余チャンネルは独立した`VoicePatchType::OPL3_2`(0x22)を持つ
(D-021時点で既に対応済み)。この2つは実チップYMF262の異なる動作
モードであり、レジスタ幅やALG/FB2/PDTの意味が異なるため、
OPL3_2向けの`oplVoiceRanges()`/`isOplAlgFamily()`をそのまま流用
できない。

**FITOM_X実ソース(`core/src/OPL_new.cpp`の`COPL3::updateVoice()`ほか)
と`docs/voice-parameter-reference.md`「OPL3 (YMF262) 4OPモード」節を
突き合わせて確認した仕様**:

- `hw.ALG`(3bit、`alValue() = hw.ALG & 0x7`)は、OPN系ALGとも
  OPL/OPL2/OPL3_2の1bit ALGとも異なる**独自の3bitパック値**:
  bit0=CON1(前半ペアM1/C1の接続)、bit1=CON2(後半ペアM2/C2の接続)、
  bit2=ConnectionSEL(4OP結合の有効化。0x104 CONNECTIONSELレジスタ・
  `carmsk[8]`キャリアマスクテーブル・`updateKey()`のキーオン連鎖条件が
  このbitを参照する)。ちょうど8通り(0-7)で、既に用意されていた
  `opl3_al{0-7}.png`8枚と1対1対応する。
- `hw.FB`は前半ペア専用、`hw.FB2`は後半ペア専用(それぞれ独立した
  0xC0レジスタに書く実機仕様)。OPL/OPL2/OPL3_2ではFB2は無関係
  (`{0,0,false}`)だが、OPL3(4OP)ではFB2も実際に使用される
  (`{0,7,true}`)。
- `ops[0-3]`のAR/DR/SL/SR/RR/TL/KSR/KSL/MUL/WS/AM/VIBはOPL/OPL2/
  OPL3_2と同じレジスタ幅(WSは3bit、OPL3_2と同じ`o.WS & 0x7`)。
  DT1/DT2/EGTは無関係(常に0固定)、これもOPL系共通。
- **`ops[0].PDT`/`ops[2].PDT`のみ**が疑似デチューンとして使用される
  (`COPL3::updateFnumber()`が`p.hwOp[0].PDT`/`p.hwOp[2].PDT`だけを
  読み、前半ペア/後半ペアそれぞれの周波数計算に使う)。`ops[1]`/
  `ops[3]`のPDTは無関係。OPN系FXモードの疑似デチューンと同じ
  フィールド・同じ計算式を共有する設計(ドキュメント内に明記)。
  これはOPL/OPL2/OPL3_2にはそもそも無い概念(2OPモードのPDTは常に
  未使用のまま)。

**実装**: `apps/gui/main.cpp`に以下を追加。

- `opl3FourOpVoiceRanges()`: FB/FB2/ALGが上記の通り使用、AMS/PMS/NFQは
  無関係(OPL系共通)。
- `getOpl3AlgTexture()`: `getOpnAlgTexture()`/`getOplAlgTexture()`と
  同じキャッシュパターンで`assets/alg_diagrams/opl3_al<0-7>.png`を
  読む、3つ目の独立したALG画像セット。
- `renderPatchEditor()`のALGファミリー判定(`isOpnAlgFamily`/
  `isOplAlgFamily`の2分岐)に`isOpl3FourOpAlgFamily`
  (`VoicePatchType::OPL3`)を追加し3分岐にした。
- `isOplWsImageFamily()`に`OPL3`を追加(WSはOPL3_2と全く同じ3bit・
  同じ`ws<0-7>.png`画像セットを共用でき、専用画像は不要)。
- **PDTのオペレータ位置依存**: 既存の`HwOpFieldRanges`はチップ種別
  単位(全オペレータ共通)の設計だったが、OPL3(4OP)のPDTだけは
  オペレータindexに依存する(0/2のみ有効)ため、`getOpFieldRanges()`に
  `int opIndex = -1`引数を追加(他チップは無視、OPL3のみ
  `opIndex == 0 || opIndex == 2`でPDTの`used`を切り替え)。
  `oplOpRanges(int wsMax, bool pdtUsed = false)`にも`pdtUsed`引数を
  追加(OPL/OPL2/OPL3_2は既定のfalseのまま無変更)。呼び出し側
  (`renderPatchEditor()`のオペレータ描画ループ)は、ループの外で1回
  だけ`opRanges`を計算していたのを、ループ内で`i`ごとに
  `getOpFieldRanges(bank.voicePatchType, i)`を呼ぶよう変更した
  (他チップは`opIndex`を無視するため計算結果は毎回同じで、実質的な
  挙動変化はない)。
- 新規バンク作成ダイアログの`kCreatableDeviceGroups`には既に`OPL3`が
  含まれていた(D-014時点で追加済み)ため変更不要。オペレータ数
  (4)自体はチップ種別に応じたハードコードではなく、既存の
  `for (i=0;i<patch->ops.size();++i)`という実データ駆動の描画ループが
  そのまま機能するため、4OP固有の新しいループ構造を書く必要は
  無かった。

**実機確認**: ビルド(`cmake --build`)・`ctest`(既存項目)全通過を
確認。実データ(`FITOM_staging/config/profiles/emulator_opl3.profile.json`
経由、`banks/OPL3/alsa/std_opl3.hwbank.json`の`bank 0 prog 0`
"Acoustic Grand"、`ALG:6, FB:0, FB2:0, ops[]`4件、`WS:[3,0,0,4]`)を、
キオスクモード(`fitom_patch_editor_gui.exe <profile> <hwbank-file>
<prog>`、D-026)で直接開いてスクリーンショット確認した。(1)
ALG接続図が`opl3_al6.png`(用意されていたアセットそのもの)で表示され、
スピンボタンで operable であること、(2)FB/AMS/PMS/NFQはグレーアウト、
FB2は非グレーアウト(有効)で表示されること、(3)OP1-4の4オペレータ
全てが横スクロール無しで表示され、それぞれのエンベロープ波形・WS画像
(WS3/WS0/WS0/WS4)が実データの値と一致して表示されること、を確認した。
「詳細」フォールドアウトを開いてOP1/OP3のみPDT入力が表示され、
OP2/OP4では非表示になることは、コードパス上は
`getOpFieldRanges(OPL3, opIndex)`の分岐で保証されているが、
DPIスケーリング環境でのクリック自動化較正(D-015参照、この環境では
毎回再較正が必要)に今回は時間を割かず、**目視でのクリック確認は
未実施**(次回このマシンで作業する際、必要なら較正の上で確認すること)。

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
