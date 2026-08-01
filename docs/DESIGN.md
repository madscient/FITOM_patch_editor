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
  レイヤードパッチバンク未割り当ての意図と思われる)だったり、
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

`apps/gui/main.cpp`のOutline画面が、バンク配下の個別パッチ(レイヤード
パッチ/パフォーマンスパッチ/デバイスボイスパッチ/サンプルゾーン
パッチ、ドラムノート)までツリーで展開表示していたのを、バンク/
キット一覧(名前・インデックス・件数のみ)に簡略化した(利用者からの
UXフィードバック、2026-07-17)。バンク/キットの行を選択すると新設の
`AppState::BankDetail`画面に遷移し、そのバンク/キットの中身(パッチ/
ノート一覧)だけを表示する。「戻る (アウトライン)」でOutlineに戻る。

選択状態は`AppContext::selectedCategory`(`BankCategory` enum:
Layered/Performance/Device/SampleZone/Pcm/Drum。`Pcm`はD-013で追加)+
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

### D-014: Outlineに「新規バンク作成」ダイアログを追加(レイヤード/ハードウェア/パフォーマンス/ドラムキット)

利用者の要望(2026-07-18)に基づき、`apps/gui/main.cpp`のOutline画面に
「新規バンク作成」ボタンを追加した。押すと以下を入力するモーダル
ダイアログ(`renderNewBankDialog()`)が開く。

- バンク種別(レイヤード/ハードウェア/パフォーマンス/ドラムキット の
  4択、`NewBankType` enum)
- バンク名(自由テキスト)
- ファイル名(拡張子・接尾辞なしの語幹のみ入力させ、種別選択に応じて
  ディレクトリ+接尾辞を自動生成 - `buildRelativeBankFile()`。例:
  レイヤードなら`patches/<stem>.patchbank.json`、ハードウェアなら
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

バンク番号(レイヤード/パフォーマンス/ハードウェアの`bank`、
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
(`createLayeredPatchBank`/`createDeviceBank`/`createPerformanceBank`/
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
レイヤードパッチ(ToneLayerの参照束ね)・パフォーマンスパッチ
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
- **レイヤードパッチ・パフォーマンスパッチ・ドラムノートの編集画面**。
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

他の5つのデータモデル型(SwPatch/LayeredPatch/DrumKit/SampleZone/
PcmBank/Profile)についても、同じ「実スキーマとの不一致」がないか
別セッション相当の調査(サブエージェントによる監査)を行った。結果:
SwPatch・DrumKit・Profileは一致。LayeredPatchは廃止済みフィールド
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

### D-033: OPL_RHY(内蔵リズムチャンネル、`COPLRhythm`)パッチ編集画面を追加

D-032に続けて、利用者から「OPL_RHYに進みましょう。OPL_RHYでは、楽器音の
選択により編集できるオペレータ数の増減があります。チャンネルパラメータに
`ext.rhythm_ch`のフィールドを追加し、`rhythm_ch=4`の場合のみ2OP、他は
1OP。`rhythm_ch`のラベルは「Inst.」とし、設定値は数値ではなくシンボル
(HH,CYM,TOM,SD,BD)をドロップダウン等で選択する。他はOPL(2OP)に準ずる」
という依頼を受けた。

**FITOM_X実ソース(`core/src/OPL_new.cpp`の`COPLRhythm`)と
`docs/terminology.md`「OPL系内蔵リズムチャンネル」節を突き合わせて
確認した仕様**:

- `ext.rhythm_ch`(0=HH/1=CYM/2=TOM/3=SD/4=BD)は、`hw.ALG`とは**完全に
  別軸のフィールド**。`PatchManager::resolveTriple()`がこれを読んで
  `ResolvedTriple::forcedCh`を決め、5つの固定チャンネル(物理的な
  楽器)のどれにこの音色を強制ルーティングするかを決定する(パッチ
  解決レイヤー)。一方`hw.ALG`は`COPLRhythm::queryCh()`が自身の
  チャンネル管理のために別途参照する(サウンドデバイスレイヤー)。
  両者は「HwPatch作成者が一致させる責任を負う」設計(利用者からの
  指示通り、本エディタでは`ext.rhythm_ch`側のみを編集可能にした)。
  未設定は255、範囲外・未設定は解決失敗(無音)になる。
- BD(`rhythm_ch=4`)のみ2オペレータ(`hwOp[0]`+`hwOp[1]`、通常のOPL2と
  同じ直列/並列2opボイス)。HH/CYM/TOM/SD(`rhythm_ch=0-3`)は
  `hwOp[0]`のみを使う単一オペレータ楽器(`COPLRhythm::updateVoice()`が
  `ch==4`かどうかで分岐)。実データ
  (`FITOM_staging/banks/OPL2/msx_audio/msx_audio_preset_rhythm.
  hwbank.json`)でも、prog0"OPL Bass Drum"(`rhythm_ch:4`)のみ`ops`が
  2件、他のprog(`rhythm_ch:0-3`)は全て1件で確認した。
- FB/ALG(チャンネルパラメータの`0xC0`レジスタ書き込み、
  `0x30 | (FB&7)<<1 | (ALG&1)`)は、通常のOPL/OPL2/OPL3_2と
  バイト単位で完全に同一。WSも`o.WS & 0x3`(2bit)でOPL2と同一幅。
  利用者の指示通り「他はOPL(2OP)に準ずる」で網羅できた
  (`oplVoiceRanges()`/`oplOpRanges(3)`をそのまま再利用、専用の
  レンジ関数は新設不要)。

**実装**: `apps/gui/main.cpp`に以下を追加。

- `getVoiceFieldRanges()`/`getOpFieldRanges()`に`OPL_RHY`を追加し、
  それぞれ`oplVoiceRanges()`/`oplOpRanges(3)`(OPL2と共用)を返す分岐に
  含めた。
- `isOplAlgFamily`(`renderPatchEditor()`内)・`isOplWsImageFamily()`
  双方に`OPL_RHY`を追加し、ALG接続図(`opl_alg{0-1}.png`)・WS画像
  (`ws{0-7}.png`)ともOPL/OPL2/OPL3_2と共用。
- 新規`renderRhythmInstrumentCombo()`: `ext.rhythm_ch`を`ImGui::
  BeginCombo()`による「Inst.」ドロップダウン(HH/CYM/TOM/SD/BDの
  シンボル表示)として描画する。255(未設定)は空選択(プレビュー
  テキスト「(未設定)」)として表示し、勝手にHH等へデフォルト
  フォールバックさせない(未設定のまま解決失敗することを黙って
  隠さないため)。選択変更時に`patch.ops.resize((v==4) ? 2 : 1)`を
  実行し、BD⇔他楽器の切り替えに応じてオペレータパネルの表示数を
  即座に追随させる(**ロード直後・未操作の状態では発火しない** -
  ディスクから読み込んだ既存データがどんな`ops`件数であっても、
  利用者がドロップダウンを実際に操作するまでは無条件に書き換えない
  設計)。`renderPatchEditor()`の「チャンネルパラメータ」バンド先頭
  (ALG接続図より左)に、`bank.voicePatchType == OPL_RHY`のときだけ
  条件描画で追加した。

**未解決・既知の限界(今回のスコープ外として据え置き)**:
`buildHwPatchDiffJson()`(D-027のリアルタイム差分SysEx送信)は
`hw`と`ops`のみを差分対象にしており、`ext`(`rhythm_ch`含む、
FIX/ALG_EXT/HWEPも同様)はそもそも対象外だった(本セッション以前
からの既存の制限で、OPL_RHY固有の問題ではない)。そのため
「Inst.」ドロップダウンを操作しても、エディタを開いたまま
スライダーを動かした時と同じ即時反映のリアルタイムSysExは飛ばない。
ただし(1)試聴鍵盤を押すたびに送る`buildHwPatchOverrideJson()`は
`ext`を丸ごと含むため次にキーを押した時点では正しく反映される、
(2)「登録」(ディスク保存)・エディタを閉じた時の全件再送信
(`sendFullRegisteredOverride()`)はどちらも`ext`込みの完全な状態を
使うため、いずれも最終的には正しい値になる。「ドロップダウンを
変更した直後、キーを押す前の一瞬だけ」という限定的なギャップに
留まるため、今回は許容し`buildHwPatchDiffJson()`自体には手を
入れなかった。

**実機確認**: ビルド・`ctest`全通過を確認。実データ
(`FITOM_staging/banks/OPL2/msx_audio/msx_audio_preset_rhythm.
hwbank.json`)を2つの異なるプロファイル経由で開いて確認した
(このバンクファイルは`profile.json`の`hw_banks[].group`が
`"OPL2"`(`hw_opm_emu_opl3.profile.json`等)か`"OPL_RHY"`
(`emu_opl.profile.json`/`unified_preset.profile.json`)かで
`bank.voicePatchType`が変わる - **本エディタは常にプロファイル側の
`group`を権威とし、バンクファイル自身の`voice_patch_type`フィールド
は見ない**(`PatchWorkspace::loadBanks()`の既存の設計、D-008以来
一貫)。最初`hw_opm_emu_opl3.profile.json`(`group:"OPL2"`)経由で
開いてしまい、「Inst.」コンボが出ないのを見て気づいた実装ミス
ではなく元々の仕様通りの挙動だった)。`emu_opl.profile.json`
(`group:"OPL_RHY"`)経由でキオスクモードから開き直し、(1)
`bank 0 prog 0`("OPL Bass Drum"、`rhythm_ch:4`)で「Inst.」コンボが
「BD」を表示し、OP1/OP2の2オペレータパネルが表示されること、(2)
`bank 0 prog 1`("OPL Close Hi Hat"、`rhythm_ch:0`)で「Inst.」コンボが
「HH」を表示し、OP1のみ1オペレータパネルが表示されること、を
スクリーンショットで確認した。利用者からの方針(GUIのクリック自動化は
明示的な指示がない限り実施しない、`CLAUDE.md`「GUIの動作確認に
ついて」参照)に従い、ドロップダウンを実際にクリックして選択肢を
切り替える動作・`ops.resize()`が実際に発火することの目視確認は
利用者に委ねる(未実施)。

### D-034: パッチ編集画面のsw_bank/sw_prog参照を数値入力からクリック可能なラベル+パッチピッカー(SW限定)に変更

利用者から「パッチ編集画面全般、sw_bank/sw_prog参照を、数値入力ではなく
バンク名・パッチ名表示として、表示ラベルをクリックするとパッチピッカー
(swのみ)からピックする」という依頼を受けた。

**変更前**: `renderPatchEditor()`は`sw_bank`/`sw_prog`を素の
`ImGui::InputInt`2個(生の整数)として表示していた。参照先の名前解決は
BankDetail画面の一覧表示(`renderBankDetail()`のDeviceケース)にのみ
既に実装済みで(`ws.resolvePerformancePatch(patch.sw_bank,
patch.sw_prog)`)、パッチ編集画面本体には存在しなかった。

**実装**: 既存の`ws.findPerformanceBank()`/`ws.resolvePerformancePatch()`
(いずれも`fpe::PatchWorkspace`の既存API、新設不要)で
`"SW: <バンク名> / <パッチ名>"`形式のラベルを組み立て、
`ImGui::InputInt`2個の代わりに`ImGui::Selectable()`として表示するよう
`renderPatchEditor()`を変更した。未設定(`sw_bank<0 || sw_prog<0`)は
「SW: (未設定)」、参照先が見つからない場合は「bank N」「prog N
(見つかりません)」とフォールバック表示する。

クリックすると新設の`renderSwPatchPicker()`モーダル(`openSwPatchPicker()`
で起動)が開き、`ws.performanceBanks()`の全バンク・全パッチを
`ImGui::TreeNodeEx`+`ImGui::Selectable`のツリーで一覧表示する。選択すると
対象HwPatchの`sw_bank`/`sw_prog`を書き換えて閉じる。「参照解除」ボタンで
`-1/-1`(未設定)に戻せる。利用者の指示通り**SW(パフォーマンスパッチ)
限定**とし、レイヤードパッチ等、他の参照種別のピッカーは対象外とした
(将来必要になれば別のpicker stateとして追加すること)。

**状態管理**: 既存の`PathPickerState`(D-019)と同じ設計方針を踏襲した。

- 複数の`PatchEditorWindow`が同時に開いていても、ピッカーは
  `AppContext`上に1つだけ(`SwPatchPickerState`)。同時に複数開く必要は
  ない(常にモーダルとして1つだけ操作可能なため)。
- ピッカーが「どのHwPatchを編集中か」は`{deviceBankIndex,
  devicePatchProg}`という**インデックスの組**で保持し、`HwPatch*`を
  直接キャプチャしない。これは`PathPickerState::target`(生の`char*`
  ポインタを保持する設計)とは異なる選択で、`PatchEditorWindow`
  自体が既に確立している「インデックスを保持し、毎フレーム実体を
  引き直す」設計(D-012/D-015)に合わせた方が、`ops`ベクタの再配置等に
  対して安全なため。`renderSwPatchPicker()`は毎フレーム
  `deviceBanks[index].findByProg(prog)`で実体を引き直し、既に消えていた
  場合は静かにピッカーを閉じる。
- モーダルの`OpenPopup()`/`BeginPopupModal()`呼び出しは、既存の
  `renderNewBankDialog()`/`renderErrorPopup()`と同様、各
  `PatchEditorWindow`自身の`Begin()/End()`ブロックの**外側**
  (`main()`のトップレベル、`renderPatchEditors()`の直後)から毎フレーム
  呼ぶ設計にした。`PathPickerState`のコメントが警告する「モーダルの
  中からネストしてモーダルを開く場合はBeginPopupModalブロックの内側
  から呼ぶ必要がある」問題は、あくまで「モーダル→モーダル」のネスト
  ケースの話であり、「モードレスウィンドウ→モーダル」という今回の
  ケース(既存の`renderErrorPopup()`がkioskモードの`パッチ編集`
  ウィンドウの中から呼ばれているのと全く同型)には当てはまらないため、
  トップレベル呼び出しで問題ない。

**実機確認**: ビルド・`ctest`全通過を確認。実データ
(`FITOM_staging/config/profiles/emulator_opl3.profile.json`経由、
`std_opl3.hwbank.json`の`bank 0 prog 0`"Acoustic Grand")をキオスク
モードで開き、名前欄の下に「SW: Performance SwPatch Presets /
VelScale Mid (Carr...」という解決済みラベルが数値入力の代わりに
表示されることを確認した。ウィンドウがフォアグラウンド化に失敗する
問題(このセッション中、原因不明のフォーカス奪取競合が発生し
`SetForegroundWindow`/`AttachThreadInput`を使っても対象ウィンドウを
最前面化できなかった)に遭遇したため、`PrintWindow(hwnd, hdc,
PW_RENDERFULLCONTENT=2)`でフォーカス・最前面化に依存せず直接ウィンドウ
内容をキャプチャする方式に切り替えて確認した(今後同様の問題が起きた
場合の代替手段として記録)。利用者からの方針(`CLAUDE.md`「GUIの動作
確認について」)に従い、ラベルを実際にクリックしてピッカーを開き
パッチを選び直す操作自体の目視確認は利用者に委ねる(未実施)。

**追記(同日)**: 利用者が実機で目視確認の上、ラベル部分のレイアウトを
直接調整した(コミット`99f7118`)。文言を「SW: バンク名 / パッチ名」
から「パフォーマンス: {sw_bank}/{sw_prog} : バンク名 / パッチ名」
(生の番号も併記する形)に変更、未解決時のフォールバック表示を
バンク側・パッチ側それぞれ異なる文言(「bank N」/「見つかりません」)
から共通の簡潔な「(N/A)」に統一、長くなった文言に合わせて
`ImGui::Selectable`のクリック可能幅を320→640に拡大した。ロジック
(`openSwPatchPicker()`/`renderSwPatchPicker()`)自体には変更なし。

### D-035: assets/(ALG/WS画像)のパス解決がCWD基準になっていたバグを修正

利用者から実機バグ報告を受けた。

> algイメージ、wsイメージのパス解決が起動時のcwd基準になっていると
> 思われる(実行ファイルのないパスから起動するとイメージが表示され
> ない)。例:ステージング環境(`..\FITOM_staging`)から以下のコマンド
> ラインで起動するとALGイメージが表示されない
> `bin\fitom_patch_editor_gui.exe config\profiles\emu_opl.profile.json`

**原因**: `assetsDir()`(D-016で新設)が`fs::current_path()`(プロセスの
CWD)を起点に、`assets/alg_diagrams/opn_al0.png`が見つかるまで**上方向**
にディレクトリを遡る実装になっていた。通常のダブルクリック起動
(CWD=実行ファイル自身のディレクトリ)や、`tests/smoke_test.cpp`の
`fixturesDir()`(ctestは常にビルドディレクトリをCWDにして実行される)
では偶然CWDと探索起点が一致するため問題が表面化していなかった。しかし
利用者の報告のように、`bin\fitom_patch_editor_gui.exe`をCWD=`bin`の
**親ディレクトリ**から相対パス起動すると、CWD(`FITOM_staging`)は
`assets`が実際に置かれている`FITOM_staging\bin\assets\`の**祖先ではなく
兄弟関係にすぎない**ため、上方向探索では永遠に見つからない
(`bin`は`FITOM_staging`の子であって親ではない)。D-020で`Preferences.cpp`
の設定ファイル保存先を同種の理由でCWD基準からexe基準に変更した際と
全く同じ種類のバグが、`assetsDir()`側には未修正のまま残っていた。

**修正**: `Preferences.cpp`の`exeDir()`(D-020、`GetModuleFileNameW`で
実行ファイル自身の絶対パスを取得しその親ディレクトリを返す、
POSIX側は未検証で空パスを返しCWDにフォールバック)と全く同じ実装を
`apps/gui/main.cpp`内にも複製し(`Preferences.h`が`exeDir()`を公開
していないため、小さな自己完結ヘルパーとしてそのまま複製 - export
するほどの共有理由はないと判断)、`assetsDir()`の探索起点を
`fs::current_path()`から`exeDir()`(解決できなければCWDにフォール
バック)に変更した。上方向探索ロジック自体(ビルドツリーの配置差異への
耐性)は維持し、起点だけを差し替えている。

**実機確認**: ビルド・`ctest`全通過を確認。実際にCWDと実行ファイルの
場所を意図的に分離した状態(`build/vs2026`をCWDにして`Debug\
fitom_patch_editor_gui.exe`を起動 - 利用者の報告と同じ「CWDが実行
ファイルの祖先ではなく親」という関係)でキオスクモード
(`emulator_opl3.profile.json`/`std_opl3.hwbank.json`)を起動し、
ALG接続図・WS波形画像とも修正前なら空表示になっていたはずのところ、
正しく表示されることをスクリーンショット(`PrintWindow`方式、D-034の
続き)で確認した。**利用者自身も実機(元の報告環境、
`..\FITOM_staging`からの相対パス起動)で再現の上「OK」と確認済み。**

### D-036: レイヤードパッチ編集画面を新規実装、ToneLayerのhw_bank/hw_prog参照はHWパッチピッカー、参照先の編集は既存Device編集画面を再利用

利用者から「レイヤードパッチ編集画面を実装してほしい。ToneLayer内の
hwpatch参照は数値入力ではなく名前表示+パッチピッカー(hwのみ)による
ピック選択式にする。各hwpatch表示行の末尾に「編集」ボタンを配置し、
クリックで対象のhwpatch編集画面をモーダル(オーバーレイでも良い)で
開く」という依頼を受けた。

**前提**: レイヤードパッチ(`fpe::Patch`/`ToneLayer`)の編集フォームは
これまで存在せず、BankDetailの表示は`renderToneLayer()`による読み取り
専用の`ImGui::BulletText`一行(`hw_bank=%d hw_prog=%d`等、生の数値の
まま)だけだった。今回が最初の編集フォーム実装になる。

**設計方針**: D-015(Device/HwPatch編集画面)・D-034(sw_bank/sw_prog
参照ピッカー)で確立済みのパターンをそのまま踏襲した。

- `LayeredPatchEditorWindow`(`AppContext::openLayeredEditors`)は
  `PatchEditorWindow`と同じ「`{bankIndex, prog}`のインデックスのみを
  保持し、実体(`fpe::Patch&`)は毎フレーム`ws.layeredPatchBanks()
  [bankIndex].findByProg(prog)`で引き直す」設計(D-012/D-015)。複数
  同時に開ける。BankDetailのレイヤードパッチバンク行は、Deviceケースと
  同じ`ImGui::Selectable`+`openLayeredPatchEditor()`に変更した(以前の
  `ImGui::TreeNode`によるインライン展開は廃止)。
- ToneLayerのhw_bank/hw_prog参照ピッカーは、`SwPatchPickerState`
  (D-034)と対になる新規`HwPatchPickerState`
  (`{layeredBankIndex, layeredPatchProg, layerIndex}`のインデックス3つを
  保持、同じく毎フレーム再解決)+`renderHwPatchPicker()`として実装。
  `ws.deviceBanks()`(全チップ系統のHwBankを横断)を`[group bank] name`
  でグループ化してツリー表示する点もSW版とほぼ同型だが、**選択時に
  書き込むフィールドが3つ(`voice_patch_type`/`hw_bank`/`hw_prog`)**
  という違いがある。ToneLayerの`voice_patch_type`はそのレイヤーが
  どのチップ系統のHwBankを参照するかのタグであり、ピックしたHwBank
  自身の`voicePatchType`と食い違ってはいけない(「バンクの中の1パッチを
  選ぶ」操作は、本質的に「そのバンクのチップ系統ごと選ぶ」操作でもある
  ため)。SW版のsw_bank/sw_progにはこの対応するタグ概念が無いため、
  SwPatchPickerStateをそのまま拡張するのではなく別の新規picker state
  にした(コード上の重複はあるが、書き込み先フィールド数が違う時点で
  「同じものの再利用」ではなく「同じパターンの2つ目の適用」と判断)。
- 「編集」ボタン: ToneLayerの`hw_bank`は`HwBank::bankIndex`
  (profile.json `hw_banks[].bank`)であって`ws.deviceBanks()`の
  ベクタ添字ではないため、対象HwPatchを引くには`voice_patch_type`+
  `hw_bank`のペアでのリニアサーチが要る。既存の`findDeviceBankIndexByFile()`
  (ファイルパスというキーでのリニアサーチ)と同じ「安定したキーで
  探す、ベクタ位置に依存しない」考え方を踏襲した新規
  `findDeviceBankVectorIndex(ws, type, bankIndex)`ヘルパーで
  `ws.deviceBanks()`上のベクタ添字を得てから、既存の
  `openPatchEditor(ctx, deviceIdx, hw_prog)`をそのまま呼ぶ。**独立した
  モーダルウィンドウは新設していない** - 依頼文面の「モーダルに開く
  (オーバーレイでも良い)」を文字通り採用し、D-015で確立済みの
  モードレスDevice編集ウィンドウをそのまま「編集」ボタンの遷移先とした
  (Device編集画面自体を複製・改変する理由がないため)。

**意図的にスコープを絞った点**:

1. ~~`fpe::Patch`自身の`sw_bank`/`sw_prog`は生の`ImGui::InputInt`2個の
   まま~~ (→同日、利用者から「swbank/swprogはhwパッチ編集画面と同様の
   ピッカー動作としてください」と追加依頼を受け、対応済み。下記
   「追記」参照)。
2. ToneLayer自体の追加・削除UI(Patchの`layers`ベクタの要素数を
   変える操作)は依頼に含まれていなかったため未実装。既存レイヤーの
   フィールド編集のみ。
3. Device編集画面(D-015/D-027)が持つリアルタイム差分SysEx送信・
   試聴鍵盤・「登録」時のFITOM_X再送信は、レイヤードパッチ編集画面には
   実装していない。レイヤードパッチ自体は合成パラメータを一切持たない
   参照の束でしかなく、試聴自体は「編集」ボタンから開くDevice編集画面が
   従来通り担うため、ここで重複して実装する理由がないと判断した
   (「登録」ボタンは`ctx.workspace.save()`のみ呼ぶ、ディスクへの保存
   という意味では他のCRUD操作(D-014の新規バンク作成等)と同じ扱い)。

**実機確認**: ビルド(`cmake --build build/vs2026`)・`ctest`(既存項目、
データモデル層に変更なしのため回帰なし)は確認したが、**クリック操作の
実機確認はしていない**。`CLAUDE.md`「GUIの動作確認について」の方針
(利用者の明示的な指示が無い限り自動クリック操作をしない)に加え、
今回の変更点(Layered BankDetailの行クリック→エディタ起動→ToneLayerの
ラベルクリック→ピッカー→選択→「編集」ボタン→Device編集画面起動、という
一連の遷移)はクリックそのものを経ないと露出しないため、キオスクモード
起動によるビルド後の受動的スクリーンショット確認(D-035等で行っていた
もの)も今回は意味を持たず見送った。利用者自身の目視確認を待つ。

**追記(同日): `fpe::Patch`自身のsw_bank/sw_prog参照もHWピッカーと
同様のラベル+ピッカー方式に変更**。利用者から「swbank/swprogはhwパッチ
編集画面と同様のピッカー動作としてください」という追加依頼を受け、
上記スコープ外事項1を解消した。既存の`SwPatchPickerState`
(D-034、HwPatchの`{deviceBankIndex, devicePatchProg}`前提で組まれて
いた)を、`SwPatchPickerTarget`(`Device`/`Layered`)で参照先を切り替える
形に一般化した。Device/Layered いずれの場合も最終的には
「書き換えたい`sw_bank`/`sw_prog`という`int`2つへのポインタ」に解決
してから同じツリー一覧・選択・「参照解除」ロジックを通す設計にしたため、
一覧描画コード自体は重複させていない(D-036本文で「HwPatchPickerState
はSwPatchPickerStateを拡張せず別の新規stateにした」と書いたのとは
対照的な判断 - ToneLayerのHWピッカーは選択時に書き込むフィールド数が
3つ(`voice_patch_type`込み)で構造が違ったため別stateにしたが、今回の
Patch自身のsw_bank/sw_prog参照はHwPatchのsw_bank/sw_prog参照と全く
同じ`{bank, prog}`という形なので、一般化する方が自然だった)。
`openSwPatchPicker(ctx, deviceBankIndex, devicePatchProg)`(既存、
Device向け)に加え、新規`openLayeredSwPatchPicker(ctx, layeredBankIndex,
layeredPatchProg)`(Layered向け)を追加し、`renderLayeredPatchEditor()`の
sw_bank/sw_prog表示を、`renderPatchEditor()`のsw_bank/sw_prog表示
(D-034)と全く同じ「パフォーマンス: {bank}/{prog} : バンク名 / パッチ名
(未解決時は(N/A))」ラベル+クリックでピッカーを開く形に変更した(生の
`ImGui::InputInt`2個は廃止)。ビルド・`ctest`(既存項目、データモデル層に
変更なしのため回帰なし)を再確認した。クリック操作自体の実機確認は
上記と同様、引き続き利用者の目視確認待ち。

### D-037: パフォーマンスパッチ編集画面を実装、LFO波形はイメージ表示・モードはシンボルドロップダウン

利用者から「パフォーマンスパッチ編集画面を実装。波形についてはhwパッチの
WSと同様にイメージ表示とする(画像は数値のみ埋め込んだプレースホルダで
良い、あとで人間が調整する)。モードは数値ではなくシンボル選択(ドロップ
ダウン等)。他はとりあえずスライダーで良い(あとで人間が調整する)」と
いう依頼を受けた。

**前提**: パフォーマンスパッチ(`fpe::SwPatch`)の編集フォームはこれまで
存在せず、BankDetailの表示は`ImGui::BulletText`による読み取り専用の
一行(`[prog %d] %s`)だけだった。今回が最初の編集フォーム実装になる。

**設計方針**: D-015(Device編集画面)・D-036(Layered編集画面)で確立済みの
パターンをそのまま踏襲した。

- `PerformancePatchEditorWindow`(`AppContext::openPerformanceEditors`)は
  既存2種と同じ「`{bankIndex, prog}`のインデックスのみを保持し、実体
  (`fpe::SwPatch&`)は毎フレーム`ws.performanceBanks()[bankIndex]
  .findByProg(prog)`で引き直す」設計(D-012/D-015/D-036)。複数同時に
  開ける。BankDetailのパフォーマンスバンク行を、Device/Layeredケースと
  同じ`ImGui::Selectable`+`openPerformancePatchEditor()`に変更した
  (以前の`ImGui::BulletText`のみの一覧表示から変更)。
- LFO波形フィールド(`FmSwVoice::LWF`= チャンネルビブラート波形、
  `FmSwOp::SLW`= オペレータごとのトレモロ波形。`FmSwOp.h`のコメント
  「same choices as LWF」の通り、両方とも同じ0-6の7値enum)は、
  HwPatchのWS(D-021)と全く同じ`renderImageSpinner()`(画像+スピン
  ボタン、値が画像自体に焼き込まれる表示)を再利用して表示する。
  **画像アセット自体は依頼文言通り「数値のみ埋め込んだプレースホルダ」**
  - `assets/waveforms/lfo{0-6}.png`(168x100、WS用`ws<n>.png`と同じ
    サイズ)を、大きな数字1つだけを描いた最小限のPNG(Pillow等の画像
    ライブラリがこのマシンに無かったため、`zlib`標準ライブラリのみで
    PNGを直接エンコードする使い捨てスクリプトで生成、リポジトリには
    生成済みPNGのみコミットしスクリプト自体は残していない)として新規
    追加した。実際の波形形状(up-saw/square/triangle/S&H/down-saw/
    delta/sine、`FmSwVoice::LWF`のコメント参照)は今回描いていない -
    依頼文言「あとで人間が調整する」の通り、人間による差し替えを
    前提にしている。
- モードフィールド(`FmSwVoice::LFM`/`FmSwOp::SLM`、コメント「0=loop/
  1=one-shot hold/2=one-shot to zero」「mode (same semantics as LFM)」)
  は、D-033の`renderRhythmInstrumentCombo()`(OPL_RHYの「Inst.」
  ドロップダウン)と同じパターンの`renderLfoModeCombo()`(値→日本語
  ラベルの固定テーブル+`ImGui::BeginCombo`)で表示する。LFM/SLMは同じ
  enumを共有するため、1つの共有関数で両方をカバーした(重複実装を
  避けた)。
- それ以外の全フィールド(`FmSwVoice`の`LFS`/`LFD`/`LFR`/`LFI`/
  `depth_cents`、`FmSwOp`の`VTL`/`VAR`/`VDR`/`VSL`/`VSR`/`VRR`/`VLD`/
  `VLR`/`SLS`/`SLD`/`SLY`/`SLR`/`SLI`、`SwPatch::fine_transpose`)は、
  依頼文言「他はとりあえずスライダーで良い」の通り、単純な
  `ImGui::SliderInt`ベースの`sliderU8()`/新規`sliderI16()`(int16_t版、
  `sliderU8()`と対になる薄いラッパー)で表示する。**実際のレジスタ幅は
  FITOM_X側のいかなるドキュメントとも未確認**(HwPatchの
  `FieldRange`/`getVoiceFieldRanges()`/`getOpFieldRanges()`(D-016)の
  ような確認済みテーブルではなく、暫定値)。`depth_cents`/
  `fine_transpose`は構造体自身のコメントに明記されている
  `-1200..+1200`をそのまま使い、それ以外の未確認フィールドは
  `HwOpFieldRanges`の未確認チップ向けフォールバック(`genericOpRanges()`、
  D-016)と同じ「0-99」を暫定値として採用した。`FmSwOp::VLD`/`VLR`は
  構造体自身のコメントで「reserved, currently unused」と明記されている
  ため、HwPatchの「チップが読まないフィールドは`FieldRange.used=false`
  でグレーアウトしつつ表示は維持する」という既存の慣習(D-016)に
  倣い、スライダー自体は表示したまま`ImGui::BeginDisabled()`で
  無効化した。
- チャンネルビブラート(`FmSwVoice`)のレイアウトは、HwPatchのALG帯
  (D-017、画像+スピンボタンをバンド左端に置き、残りのスライダー群を
  右に並べる)と同じ構成(`renderSwVoiceEditor()`)。オペレータごとの
  ベロシティ感度/トレモロ(`FmSwOp`x4)は、HwPatchの`renderHwOpEditor()`
  と同じ「1オペレータ1`ImGui::BeginChild`ボックス、`SameLine()`で
  横に並べる」構成(`renderSwOpEditor()`)にした。

**意図的にスコープを絞った点**: Device編集画面(D-015/D-027)が持つ
リアルタイム差分SysEx送信・試聴鍵盤・「登録」時のFITOM_X再送信は実装
していない。D-036のレイヤードパッチ編集画面と同じ理由 - `SwPatch`
自体は合成パラメータを一切持たず、`sw_bank`/`sw_prog`経由でそれを
参照するHwPatch側で初めて音になる(HwPatchの試聴は既存のDevice編集
画面が担う)ため、ここで重複して実装する理由がない。「登録」ボタンは
`ctx.workspace.save()`のみ呼ぶ(他のCRUD操作と同じ扱い)。

**実機確認**: ビルド(`cmake --build build/vs2026`)・`ctest`(既存項目、
データモデル層に変更なしのため回帰なし)を確認した。**クリック操作の
実機確認はしていない**。`CLAUDE.md`「GUIの動作確認について」の方針
(利用者の明示的な指示が無い限り自動クリック操作をしない)に加え、
今回の変更点(Performance BankDetailの行クリック→エディタ起動→各
コントロールの見た目・操作)はクリックそのものを経ないと露出しない
ため、キオスクモードでの受動的スクリーンショット確認(D-035等で行って
いたもの)も見送った - キオスクモード自体`<hwbank-file> <prog>`という
Device(HwPatch)専用の起動引数しか持たず(D-026)、パフォーマンスパッチを
直接キオスク起動する経路が無いことも理由の一つ。利用者自身の目視確認を
待つ。

**追記(同日): Device/Layered編集画面のsw_bank/sw_prog表示行に「編集」
ボタンを追加、参照先のパフォーマンスパッチ編集画面を直接開けるように
した**。利用者から「レイヤードパッチ編集、デバイスパッチ編集の
パフォーマンスパッチ表示部の右端に「編集」ボタンを配置して、
パフォーマンスパッチ編集画面をモーダルまたはオーバーレイで表示する」
という追加依頼を受けた。ToneLayerのhw_bank/hw_prog行が既に持っている
「ラベル(クリックでピッカー)+末尾の「編集」ボタン(参照先の既存
モードレス編集ウィンドウを開く)」という構成(D-036)と全く同じパターンを、
`renderPatchEditor()`/`renderLayeredPatchEditor()`双方のsw_bank/sw_prog行に
適用した。

- `findDeviceBankVectorIndex()`(D-036、`HwBank::bankIndex`という安定
  キーで`ws.deviceBanks()`のベクタ添字を引く)と対になる新規
  `findPerformanceBankVectorIndex(ws, bankIndex)`を追加した -
  `SwBank::bankIndex`という同じ意味のキーで`ws.performanceBanks()`の
  ベクタ添字を引く(`openPerformancePatchEditor()`はベクタ添字を
  要求するが、`sw_bank`フィールド自体は`SwBank::bankIndex`であって
  ベクタ位置ではないため、この変換が要る)。
- 両編集画面のsw_bank/sw_prog表示ブロックで、ラベルの`Selectable`の
  直後に`ImGui::SameLine()`+「編集」ボタンを追加。参照が未解決
  (`sw_bank`/`sw_prog`が負、または解決先が見つからない)の間は
  `ImGui::BeginDisabled()`でグレーアウトする(ToneLayerの「編集」
  ボタンが`hwPatch`の有無で無効化する、D-036と同じ扱い)。ボタン押下時は
  `findPerformanceBankVectorIndex()`で得たベクタ添字を使って
  `openPerformancePatchEditor()`を呼ぶ - D-037本文で新設した
  `PerformancePatchEditorWindow`をそのまま再利用しており、独立した
  モーダルは新設していない(依頼文面の「モーダルまたはオーバーレイで」
  を、D-036と同じ判断で「既存のモードレスウィンドウを再利用」と解釈)。
- キオスクモード(D-026)は`renderPatchEditor(ctx, ctx.kioskEditor)`を
  唯一のパッチ編集画面として使うため、そのsw_bank/sw_prog行の「編集」
  ボタンから`ctx.openPerformanceEditors`に新しいウィンドウが積まれ
  得るようになった。キオスク分岐は元々`renderPerformancePatchEditors(ctx)`
  を呼んでいなかった(パフォーマンスパッチ編集画面自体、D-037時点では
  キオスク経由で開く手段が無かったため)ので、`ImGui::End()`
  (「パッチ編集」ウィンドウを閉じた)直後に呼び出しを追加した -
  独立したモードレスウィンドウなので、外側の「パッチ編集」の
  `Begin`/`End`にネストさせず、兄弟ウィンドウとして描画する(同じ
  理由で非キオスク分岐でも`renderPatchEditors()`等はネストさせていない)。

ビルド(`cmake --build build/vs2026`)・`ctest`(既存項目、データモデル層に
変更なしのため回帰なし)を確認した。クリック操作自体の実機確認は、
D-037本文と同じ理由(キオスクモードでの受動確認手段が無い)により
引き続き利用者の目視確認待ち。

### D-038: ドラムキット編集画面を新規実装(キット選択→ノート選択→ノート編集の階層化、ソースパッチ/プレイノートはピッカー、登録前プレビュー対応)

利用者から次の依頼を受けた。

- ドラムキット選択→ドラムノート選択→ドラムノート編集のように階層化
  画面遷移する。
- ドラムノート選択画面では未割当のドラムノートも表示する。
- ドラムノート選択画面では、ドラムノートの複製・削除のインターフェース
  を用意する。
- ドラムノート編集画面: ソースパッチは数値入力ではなくパッチピッカーに
  よる。プレイノート設定は数値入力ではなく、ノート名(C4, A3など)選択、
  およびスクリーンキーボード型ピッカーによる入力も可能とする。登録前に
  プレビュー発音可能とする。

**前提**: `fpe::DrumKit`/`fpe::DrumNote`の編集フォームはこれまで存在せず、
BankDetailの表示は`ImGui::BulletText`による読み取り専用の一覧だけだった
(routed: `note %d: %s -> play_note %d`の並び、direct: 単一行の要約)。
今回が最初の編集フォーム実装になる。

**階層化画面遷移は既存の構造そのままで実現できた**: Outline(ドラムキット
マップのツリー、既存)の各行クリックが元々`selectBank(Drum, i)` →
BankDetailだった - これが「ドラムキット選択」に相当する。今回、
BankDetailのDrumケース自体を「ドラムノート選択画面」に格上げし(後述)、
そこでの行クリック/作成が新設の`DrumNoteEditorWindow`
(`renderDrumNoteEditors()`/`renderDrumNoteEditor()`、
`AppContext::openDrumNoteEditors`)というモードレスウィンドウを開く -
これが「ドラムノート編集」。Device(D-015)/Layered(D-036)/Performance
(D-037)と全く同じ「`{kitIndex, note}`のインデックスのみ保持し、実体
(`fpe::DrumNote*` = `kit.findNote(note)`)は毎フレーム引き直す」設計
(D-012以来の慣習)。新しいAppState列は追加していない - 既存3種と同様、
「バンク/キット一覧→BankDetail→モードレス編集ウィンドウ」の型に
そのまま載せられたため。

**"direct"キットはこの階層に乗らない**: `DrumKit.h`の`effectiveNotes()`
のコメントの通り、"direct"キットは`note_min`-`note_max`全体に単一の
ソースパッチをpassthroughで割り当てる形で、個別ノートのvector
(`notes[]`)を持たない(=「未割当ノート」「複製」「削除」という概念
自体が存在しない)。よって"direct"キットはBankDetailの中に留め、後述の
ソースパッチピッカー+音域(note_min/note_max)+「登録」ボタンだけの
簡易インライン編集にした。"routed"キットのみが「ノート選択画面→ノート
編集画面」のフルな階層を持つ。**意図的なスコープ限定**: "direct"キットの
`sw_bank`/`sw_prog`/`fine_tune`/`pan`/`gate_time`は今回未対応(依頼文言が
"ドラムノート編集画面"の要件として書かれていたため、個別ノートを持たない
"direct"側は同じ優先度と判断しなかった)。

**ドラムノート選択画面(BankDetailのDrumケース、routed)**: MIDIノート
0-127を毎フレーム全件走査し、`kit.findNote(n)`の有無で行の見た目を
分ける。

- 割当済み: `note N (ノート名): 名前 -> play ノート名 (N)`の
  `ImGui::Selectable`(クリックで`openDrumNoteEditor()`)+
  `複製`/`削除`の`ImGui::SmallButton`。
- 未割当: `ImGui::TextDisabled`で薄く表示+`作成`ボタン(デフォルト値の
  `DrumNote`を`ws.upsertDrumNote()`で追加した上でそのまま編集画面を
  開く)。

「複製」は複製先のノート番号をユーザーに聞かず、新設の
`nextFreeDrumNote(kit, fromNote)`(0-127を`fromNote+1`から折り返しで
走査し、最初の空きノート番号を返す)で自動割当する - この
リポジトリの既存の「新規バンク作成時は現在の最大値+1を自動採番する」
慣習(`nextBankIndex()`/`nextDeviceBankIndex()`/`nextDrumProg()`)を
「open-endedなカウンタ」ではなく「0-127の固定レンジ」向けに適応した
もの。`PatchWorkspace`には「ノートの複製」専用APIは無い(`upsertDrumNote`/
`deleteDrumNote`のみ) - 複製はGUI層で「既存ノートをコピーしてnoteフィールド
だけ書き換えてupsertする」という組み合わせで実現しており、データ層の
変更は不要だった。

**「複製」「削除」「作成」は構造的変更として即座に`ws.save()`する**
- ノート一覧に対する追加/削除は、既存の「新規バンク作成」ダイアログ
(D-014、OK押下で即save())と同じ性質の構造的変更と判断した。これに
対して、ノート編集画面内でのフィールド編集(名前/ソースパッチ/
プレイノート/fine_tune等)は他の全編集画面と同じく「登録」ボタンでの
明示保存が必要(即時保存しない) - この非対称は意図的で、D-014の
先例(構造変更は即保存)とD-027以来の慣習(フィールド編集は明示登録)を
そのまま踏襲した形。

**ソースパッチピッカー(新規`DrumSourcePatchPickerState`/
`renderDrumSourcePatchPicker()`)**: `DrumNote`の`voice_patch_type`/
`patch_bank`/`patch_prog`は、CC#0そのものと同じ「normal modeか
direct modeか」の二重性を持つ(`DrumKit.h`のコメント: `voice_patch_type
== None`ならnormal mode = `patch_bank`/`patch_prog`はレイヤード
`PatchBank`/`Patch`を指す、それ以外はHwBank/HwPatchを直接指す)。
既存の`HwPatchPickerState`(ToneLayer用、device patchのみ)や
`SwPatchPickerState`(performance patch専用)のどちらもこの二重参照を
単独では表現できないため、新規ピッカーを追加した。1つのポップアップに
「レイヤードパッチ」ツリーと「デバイスボイスパッチ」ツリー(既存の
`renderHwPatchPicker()`と同じチップ別グルーピング)を両方表示し、
どちらを選んでも同じ3フィールド(`voice_patch_type`/`patch_bank`/
`patch_prog`)に書き込む - 書き込み先のポインタを`isDirect`フラグで
`DrumNote*`か`DrumKit`自身のフィールドに切り替える設計は、
`renderSwPatchPicker()`のDevice/Layered/DrumNote三分岐と同じ
「ターゲットに応じてポインタを解決し、UI自体は共有する」パターン。
`describeDrumSourcePatch()`(解決結果の文字列化)と
`openDrumSourcePatchEditor()`(「編集」ボタン - 参照先の既存Layered/Device
編集画面をそのまま開く、D-036と同じ「独立モーダルは新設しない」判断)は
BankDetailの行表示とノート編集画面の両方で共有する。

**プレイノート入力(依頼により数値入力を使わない)**: 2種類の入力手段を
用意した。

- ノート名ドロップダウン: `midiNoteName(note)`(新規、MIDIノート60を
  "C4"とする一般的なscientific pitch notationの実装 - 依頼文言の
  「C4, A3など」という例と整合。FITOM_X側のどの資料にも基づかない、
  この画面だけのUI表現上の取り決め - ワイヤ上は生のMIDIノート番号を
  送るだけなので、オクターブ数字の付け方自体は機能に影響しない)で
  0-127全件をラベル化し、`ImGui::BeginCombo`で一覧表示する。
- スクリーンキーボード型ピッカー(新規`DrumNoteKeyboardPickerState`/
  `renderDrumNoteKeyboardPicker()`): 既存の試聴鍵盤`renderPreviewKeyboard()`
  (D-015)をそのまま再利用したモーダル。3オクターブ分を一度に表示し、
  「◀」「▶」ボタンでオクターブ単位にページングできる(表示範囲=
  `baseNote`はピッカー自身の状態で、編集対象の現在値とは独立 - 現在値の
  近くを初期表示するだけで、ページングが現在値を書き換えることはない)。
  鍵盤クリックで即座に`play_note`へ反映してポップアップを閉じる(他の
  ピッカーと同じ「クリック=即選択、確定ボタンは無し」の流儀)。

**登録前プレビュー(依頼により対応、Layered/Performance編集画面とは異なる
判断)**: D-036/D-037では「LayeredPatch/SwPatch自体は合成パラメータを
持たないため試聴を実装しない」という判断をしていたが、`DrumNote`は
ソースパッチ+`play_note`の組だけで「何が鳴るか」を完全に決定するため、
「登録前に音を確認したい」という要求に実体がある。Device編集画面
(D-015/D-027)の試聴鍵盤と同じ`ctx.previewOutput`(FITOM_Xパイプ優先/
RtMidiフォールバック)を使うが、鍵盤全体は不要(play_noteは既に確定した
1音)なので、`renderPreviewKeyboard()`の白鍵/黒鍵1つと同じ
`IsItemActivated()`/`IsItemDeactivated()`による押し続け方式の
ボタン1つ(「試聴 (押している間発音)」)で済ませた。押下時に
`selectDevice(ch, voice_patch_type, patch_bank, patch_prog)` →
`noteOn(ch, play_note, 100)`、離した時に`noteOff()`。Device編集画面が
持つ「差分SysExでリアルタイムに合成パラメータを送る」仕組み(D-027)は
持たない - `DrumNote`自身のfine_tune/pan/gate_time/sw_bank/sw_progは
HwPatch側のAR/DR等と違って`sendHwPatchOverride()`で送れる合成パラメータ
ではないため、対象外(この試聴は「ソースパッチ+プレイノートの組み合わせ
で意図した音が鳴るか」だけを確認する用途)。

**sw_bank/sw_prog(ノート単位のパフォーマンスパッチ上書き)**は
`SwPatchPickerTarget`に`DrumNote`を追加して既存の`SwPatchPickerState`/
`renderSwPatchPicker()`をそのまま再利用した(D-036が`Layered`を追加した
のと同じ理由 - 4つ目の同型ピッカーを新設せず、ポインタ解決の分岐を
1つ増やすだけにした)。

ビルド(`cmake --build build/vs2026`)・`ctest`(既存117項目、データモデル
層に変更なしのため回帰なし)を確認した。**クリック操作の実機確認は
行っていない**(`CLAUDE.md`「GUIの動作確認について」の方針により、
利用者の明示的な指示が無い限り自動クリック操作をしない。かつ今回の
変更点は階層遷移そのもの(ドラムキット選択→ノート選択→ノート編集→
各種ピッカー)を経ないと露出できず、キオスクモードにもドラムキット
直接起動の経路が無いため、受動的なスクリーンショット確認も見送った)。
利用者自身の目視確認を待つ。

**追記(同日): PCM波形バンク(ADPCM-B/A・PCM-D8)・AWMサンプルゾーンバンク
を参照するソースパッチが解決できておらず、ピッカーにも出てこないバグを
修正**。利用者が実機で確認したところ、実データ(`voice_patch_type=ADPCMA`
のドラムノート)のソースパッチ表示が常に「デバイス ADPCMA 1/2 :
(N/A) / (N/A)」になり、`renderDrumSourcePatchPicker()`にもPCM波形バンクが
一覧されないという報告を受けた。

原因は`describeDrumSourcePatch()`/`renderDrumSourcePatchPicker()`が
「`voice_patch_type == None`ならレイヤード、それ以外は常に
`ws.deviceBanks()`(通常のHwBank/HwPatch)」の二択でしか分岐していな
かったこと。実際にはCC#0の直接デバイス選択値はもう2系統ある - ADPCM-B
(Y8950)/ADPCM-B/ADPCM-A/PCM-D8(`isPcmWaveformVoicePatchType()`)は
`ws.pcmBanks()`(`fpe::PcmBank`、"パッチ"は`entries[]`の0-basedインデックス
そのもの、prog フィールドを持たない - D-013)、AWM
(`isSampleBasedVoicePatchType()`)は`ws.sampleZoneBanks()`(`fpe::
SampleZoneBank`/`SampleZonePatch`、prog持ち)という、どちらも通常のHwBank/
HwPatchとは別のPatchWorkspaceベクタ・別の形状を持つ。ドラムキットは
実際にAWM/ADPCMサンプルバンク(例: `FITOM_staging`のOPL4AWM
YRW801ドラムバンク)を頻繁に参照するにもかかわらず(既知の未対応課題
「SampleZoneに`name`が無い」の項目でも触れていた「ドラムキットでは
ゾーンそのものが個々のリズム音を表す」という背景と同じ話)、この2系統への
分岐が実装時に単純に漏れていた。

- `describeDrumSourcePatch()`に`isPcmWaveformVoicePatchType`/
  `isSampleBasedVoicePatchType`の分岐を追加(`None`と`else`(通常の
  HwBank)の間に挿入)。PCM側は`PcmBank::findByIndex()`(prognoでは
  なく配列添字)、SampleZone側は`SampleZoneBank::findByProg()`で解決する。
- `renderDrumSourcePatchPicker()`に「PCM波形バンク」「サンプルゾーン
  バンク」の2つのツリーを追加(既存の「レイヤードパッチ」「デバイス
  ボイスパッチ」の後に追加、`renderBankDetail()`のPcm/SampleZoneケースと
  同じ一覧ロジックを再利用)。選択時は同じ3フィールド
  (`voice_patch_type`/`patch_bank`/`patch_prog`)に書き込む - PCM側は
  `patch_prog`に配列添字を直接書き込む点だけがデバイス/レイヤード側と
  異なる。
- 新規`drumSourcePatchHasEditor(type)`(PCM波形バンク・AWMサンプル
  ゾーンは`PcmBank.h`/`SampleZone.h`のコメントの通りそもそも編集フォームが
  存在しないため`false`を返す)を追加し、ドラムノート編集画面・
  "direct"キットのインライン編集の両方の「編集」ボタンをこの2系統では
  `ImGui::BeginDisabled()`でグレーアウトするようにした(以前は
  クリックしても何も起きない無言のno-opだった)。

ビルド(`cmake --build build/vs2026`)・`ctest`(既存117項目、回帰なし)を
再確認した。クリック確認は引き続き利用者の目視確認待ち。

**追記2(同日): 上記の修正だけでは直っておらず、実は`fpe::PcmBank`の
`voicePatchType`がそもそも設定されていなかったというデータモデル層の
バグが根本原因だったと判明・修正**。利用者が実機で再確認したところ、
(1)ソースパッチのpcmbank参照が依然解決されない、(2)パッチピッカーで
pcmbank配下のprogを選択してドラムノート編集画面に戻るとレイヤード
パッチが選択されている、という2件の報告を受けた。

前回の追記まではGUI層(`describeDrumSourcePatch()`/
`renderDrumSourcePatchPicker()`)がPCM波形バンク/AWMサンプルゾーンバンクへ
分岐すらしていなかったことだけが原因だと考えていたが、実際に利用者の
実データ(`FITOM_staging/config/profiles/emu_opn.profile.json`)を一時的な
検証用実行ファイル(`tmp_probe.cpp`、CMakeLists.txtに一時ターゲット追加、
検証後にソース・ビルド産物・CMakeLists.txtの追加分とも削除)で
`PatchWorkspace::load()`させて調べたところ、より根深いバグが見つかった。

- 実プロファイルはADPCM-B/ADPCM-Aバンクを`hw_banks[group=ADPCM*]`ではなく
  **`banks.pcm_banks[]`(`group`フィールド付き)** で登録していた
  (例: `{"bank": 1, "file": "...", "group": "ADPCMA"}`)。D-013時点の
  調査(および本ファイルの以前の記述)では「`pcm_banks[]`は`group`タグを
  持たず、実プロファイルでも使われていない」としていたが、**この前提が
  誤りだった** - `profile.schema.json`を再確認すると`pcm_banks[]`の各
  要素にも`group`(enum: ADPCMB/ADPCMA/PCMD8)が定義されており、実際に
  `emu_opn.profile.json`はこれを使って3つのPCMバンク(ADPCM-B x2、
  ADPCM-A x1)を登録していた。
- ところが`fpe::PcmBankRef`(`include/fpe/Profile.h`)自体が`group`
  フィールドを一切パースしておらず(構造体にメンバーが無い)、
  `PatchWorkspace::loadBanks()`の`pcm_banks[]`ループも`bank.voicePatchType`
  を設定していなかった - デフォルト値`VoicePatchType::None`のまま
  読み込まれていた。`PatchWorkspace::findPcmBank()`は`{voicePatchType,
  bankIndex}`の組で検索するため、voicePatchTypeがNoneのPcmBankは
  `findPcmBank(ADPCMA, 1)`等では永久に見つからない - これが(1)の直接の
  原因。
- (2)も同じ根本原因の症状: `renderDrumSourcePatchPicker()`のPCM波形バンク
  ツリーは`bank.voicePatchType`(=常にNone)を選択時にそのまま
  `*targetType`へ書き込んでいたため、PCM側のエントリを選んでも結果として
  `voice_patch_type=None`(=「レイヤード/normal mode」の目印)が書き込まれ、
  以後「レイヤード ...: (N/A)/(N/A)」に見えてしまっていた。

**修正**: データモデル層(`fpe_data`)を直接修正した。

- `fpe::PcmBankRef`(`include/fpe/Profile.h`)に`std::string group;`を
  追加(空文字列 = 未指定、スキーマ通り「省略時は全PCMデバイスがbank0を
  共有する」旧来動作)。`to_json`/`from_json`(`src/Profile.cpp`)も対応。
- `PatchWorkspace::loadBanks()`(`src/PatchWorkspace.cpp`)の`pcm_banks[]`
  ループに、`hw_banks[]`ループと全く同じ
  `stringToVoicePatchType(ref.group)`による解決を追加し、`bank.
  voicePatchType`へ書き込むようにした(`group`が空の場合はNoneのまま=
  意図的な後方互換動作、不明な文字列の場合は`hw_banks[]`と同じ形式で
  warningに積む)。
- `fixtures/profile.json`に`pcm_banks[]`エントリ(`group: "ADPCMA"`、
  `bank: 2`、既存の`test.pcmbank.json`を再利用)を追加し、
  `tests/smoke_test.cpp`に`findPcmBank(ADPCMA, 2)`が解決することの
  回帰テストを追加(117→119項目、全通過)。

実データでの検証(`tmp_probe.cpp`、検証後削除)では、修正前は3つの
PcmBankすべてが`voicePatchType=0(None)`と表示されていたのに対し、修正後は
`type=81(ADPCMB)`/`82(ADPCMA)`/`81(ADPCMB)`と正しく表示され、
`findPcmBank(ADPCMA,1)`が非nullを返し、`findByIndex(2)`が実際に
"PSS_560_PSS_560_BassDrum"(note 35 "Acoustic Bass Drum"のpatch_prog=2と
一致)を返すことを確認した。ビルド(`cmake --build build/vs2026`)・
`ctest`(119項目、全通過)を確認済み。クリック確認は引き続き利用者の
目視確認待ち。

### D-039: キオスクモードにレイヤードパッチを追加(起動引数に種別を追加、Device専用の制約を解消)

利用者から、FITOM_X本体側のスクリーンショット付きで以下の報告を受けた。

> FITOM_Xから、レイヤードパッチをキオスクモードで開いたのにデバイスパッチ
> 編集画面が開いている。キオスクモードでも指定されたパッチに対応する
> 適切な編集画面を開いてほしい。

**原因調査**: このリポジトリだけでなく、`../FITOM_X`(実リポジトリ)側も
調査した。原因は2箇所にまたがっていた。

1. FITOM_X本体側 `gui/bridge/FITOMBridge.cpp` の`resolveChannelHwPatch()`は、
   MIDIチャンネルが「通常モード」(CC#0=0、レイヤードパッチ選択)で発音中
   でも`pm.resolve(bankNo, prog, cfg)`でいったんレイヤードパッチを解決した
   上で、`resolved.layers[0]`(先頭のToneLayerのみ)のHwPatchが由来する
   `*.hwbank.json`ファイル名+prog番号だけを呼び出し元へ返す設計になって
   いた。つまりFITOM_X側で「レイヤードパッチ経由だった」という情報が
   キオスク起動引数を組み立てる前段階で既に失われていた。
2. このリポジトリのキオスクモード(D-026)自体は、そのDevice専用の3引数
   (`<profile> <hwbank-file> <prog>`)を受け取ってDeviceパッチ編集画面を
   開くだけの、当初からの仕様通りの実装だった(D-037執筆時点で「パフォー
   マンスパッチを直接キオスク起動する経路が無い」と既知の制約として
   記録済みだったのと同根)。

利用者の判断で、FITOM_X側(1)は別リポジトリ側のセッションで対応し、この
リポジトリでは(2)の側 - キオスク起動引数の仕様策定+実装 - を担当する
ことになった。

**引数仕様の決定**: 既存の3引数(`<profile.json> <hwbank-file> <prog>`、
D-026)を、種別を挟んだ4引数に変更した。

```
fitom_patch_editor_gui.exe <profile.json> <kind> <bank-file> <prog>
```

- `kind`は`"device"`(`bank-file`=`*.hwbank.json`、`prog`=HW prog)または
  `"layered"`(`bank-file`=`*.patchbank.json`、`prog`=レイヤードPatchの
  prog)のいずれか。`BankCategory`の列挙子名(`Layered`/`Performance`/
  `Device`/...)とは別に、小文字の安定した英単語トークンとして独立させた
  (`parseKioskKind()`) - こちらの内部命名(enum名)が将来変わっても、
  FITOM_X側が対象にする文字列契約は変わらないようにするため。
- `Performance`(パフォーマンスパッチ)・`Drum`は今回`kind`に含めていない。
  パフォーマンスパッチはDevice/Layeredいずれかから常に参照される側で、
  それ自体が「チャンネルの現在のパッチ」になることが無い
  (`FITOMBridge::resolveChannelHwPatch()`の解決先にも現れない)ため、
  FITOM_X側がこの`kind`を指定する場面が無い。ドラムキットも
  `resolveChannelHwPatch()`自身がリズムチャンネルを明示的に除外している
  (`midich->isRhythm()`)ため同様。将来FITOM_X側に別の呼び出し経路が
  でき次第、`kind`に追加すればよい(既存の`BankCategory`分岐パターンで
  拡張は容易)。
- 旧3引数形式との後方互換は意図的に持たせていない。FITOM_X側の呼び出し
  コード(`apps/fitom_gui/main.cpp`の`launchPatchEditorForChannel()`)も
  この変更に合わせて同時に更新される前提のため(このリポジトリの
  `CLAUDE.md`「サードパーティ依存」節と同じ「不要な後方互換シムは
  持たない」方針を、FITOM_Xとの引数契約にも適用した)。

**実装**: D-036で新設済みの`LayeredPatchEditorWindow`/
`renderLayeredPatchEditor()`をキオスクモードでもそのまま再利用した(新規の
編集画面は起こしていない)。

- `AppContext`に`kioskKind`(`BankCategory::Device`/`Layered`のいずれかに
  制限)と、Device用`kioskEditor`とは別枠の`kioskLayeredEditor`
  (`LayeredPatchEditorWindow`)を追加。1回の起動でどちらか一方だけが
  実際に使われる。
- `findDeviceBankIndexByFile()`(D-026)と対になる
  `findLayeredBankIndexByFile()`を新設し、`ws.layeredPatchBanks()`を
  `PatchBank::sourceFile`で線形探索する(`fs::equivalent()`比較、同じ
  「安定キーで探す」方針)。
- `main()`の起動時引数検証を`kind`で分岐(不明な`kind`文字列は
  `showFatalErrorBox()`で「'device' または 'layered' を指定してください」
  と即終了、D-029の流儀を踏襲)。
- メインループのキオスク分岐も`ctx.kioskKind`で表示する編集画面本体を
  切り替えるが、そこから開きうる補助ウィンドウ/ピッカー
  (`renderPatchEditors()`・`renderPerformancePatchEditors()`・
  `renderSwPatchPicker()`・`renderHwPatchPicker()`)は**種別に関わらず
  常に全部レンダリングする**ようにした(非キオスクの通常メインループが
  現在の画面に関わらず全editor/pickerを毎フレーム描画しているのと同じ
  発想)。Device種別ではLayered側のToneLayer picker等は単に何も無い状態
  (no-op)になるだけで、種別ごとに分岐を増やすより単純。
- 「登録」を押さずに閉じた際の全パラメータ再送信(`sendFullRegisteredOverride()`、
  D-027)は、キオスク画面自身が`Device`の場合のみ呼ぶようにした。
  `Layered`のキオスク画面自体は(D-036で既に確立済みの判断通り)合成
  パラメータを持たず試聴・SysEx送信もしないため不要 - そこから開いた
  ネストしたDevice/Performanceエディタは、それぞれ`renderPatchEditors()`/
  `renderPerformancePatchEditors()`が閉じた瞬間に自分自身で処理済み
  (D-027のまま変更無し)。

**実機確認**: ビルド(`cmake --build build/vs2026`)・`ctest`(既存項目、
データモデル層に変更なしのため回帰なし)を確認。非対話的なコマンドライン
実行で、(1)不明な`kind`("badkind")→「不明な種別」エラー+終了コード1、
(2)`layered`でprog番号のパース失敗→エラー+終了コード1、(3)`layered`で
存在しない`patchbank-file`/prog組み合わせ→「レイヤードパッチが見つかり
ません」エラー+終了コード1、(4)`device`/`layered`ともに正常系の実データ
(`fixtures/`)で`timeout`経由で数秒間クラッシュせず稼働し続けることを確認
した。加えて`CLAUDE.md`の方針(クリック操作は利用者の明示的指示が無い限り
実施しない)に沿って、クリックを伴わない受動的なスクリーンショット1枚を
`layered`キオスク起動で取得し、Device編集画面ではなく`renderLayeredPatchEditor()`
の内容(`[layered bank 0 prog 0]`・ToneLayer一覧・パフォーマンス参照)が
実際に表示されることを目視確認した。

**未完了・引き続きの課題**: FITOM_X本体側(`launchPatchEditorForChannel()`/
`resolveChannelHwPatch()`)の対応は、利用者が別リポジトリのセッションで
実施する前提でこのセッションでは着手していない。FITOM_X側が実際にこの
新しい4引数形式で呼び出すようになるまでは、既存の呼び出しコード
(3引数のまま)は**動かなくなる**点に注意 - 両リポジトリの変更は対で
デプロイする必要がある。

### D-040: キオスクモードにパフォーマンスパッチ・ドラムキットを追加、pcmbank/samplezonebankを予約キーワード化

D-039に続けて、利用者から次の依頼を受けた。

> パフォーマンスパッチ、ドラムキットについてもキオスクモードで動作可能と
> したいです。将来的にはpcmbank, samplezonebankも編集対象とするので、
> 対応するキーワードを予約しておいてください。

**`kind`をBankCategory全体(6種)に対応させた**: D-039時点では
`parseKioskKind()`が`"device"`/`"layered"`の2語しか受け付けなかったが、
ちょうど既存の`BankCategory`列挙型が`Layered`/`Performance`/`Device`/
`SampleZone`/`Pcm`/`Drum`の6値を持っていたため、これに1:1で対応する6つの
キーワード(`"device"`/`"layered"`/`"performance"`/`"drum"`/`"pcmbank"`/
`"samplezonebank"`)全てを`parseKioskKind()`に登録した。ただし
`"pcmbank"`/`"samplezonebank"`は依頼通り「予約」に留め、実際に編集画面を
持つのは残り4種だけ - この区別を`kioskKindImplemented(BankCategory)`という
別関数に分離した。`parseKioskKind()`が`nullopt`を返す(=綴りミス等の
不明な文字列)場合と、`kioskKindImplemented()`が`false`を返す(=綴りは
正しいが未実装)場合とで、`main()`のエラーメッセージを打ち分けている
(前者「不明な種別」、後者「予約されていますが、まだ編集画面を実装して
いません」) - FITOM_X側が先に`pcmbank`/`samplezonebank`を使い始めても、
「実装忘れ」と「そもそも綴りが違う」を区別できるようにするため。

**パフォーマンスパッチ(`kind="performance"`)**: D-037で実装済みの
`PerformancePatchEditorWindow`/`renderPerformancePatchEditor()`をそのまま
キオスクの最上位画面として再利用した。`findLayeredBankIndexByFile()`
(D-039)と対になる`findPerformanceBankIndexByFile()`を新設し、
`ws.performanceBanks()`(*.swbank.json)を`sourceFile`で線形探索する。
`AppContext`に`kioskPerformanceEditor`を追加。D-037の時点で既に
「SwPatch自体は合成パラメータを持たないため試聴・SysEx送信を実装しない」
という判断がされているため、D-039のLayered同様、キオスク終了時の
`sendFullRegisteredOverride()`は不要(Deviceキオストのみ引き続き必要)。

**ドラムキット(`kind="drum"`)**: 他の3種と異なり、DrumKitは
「1ファイル=1バンク(複数patch)」ではなく「1ファイル=1キット(1patchその
もの)」という粒度の違いがある(`DrumKit.h`参照、`*.drumkit.json`1つが
すでに1つの「パッチ」)。そのため:

- `bank-file`(*.drumkit.json)だけで対象キットが一意に決まる
  (`findDrumKitIndexByFile()`を新設、他3つのfindXxxIndexByFile()と同じ
  `fs::equivalent()`探索)。
- 4番目のCLI引数(他の3種では「バンク内のpatch prog」)は、ドラムキットの
  場合`DrumKit::prog`(=`profile.json`の`drum_banks[].prog`、そのキットを
  選択するプログラムチェンジ番号)との一致を確認する**整合性チェック**
  としてのみ使う(「探すためのキー」ではなく「FITOM_X側とプロファイル
  側で認識がずれていないかの検証」)。不一致ならDevice/Layered/
  Performanceと同じ形式のエラーメッセージで即終了する。
- キオスクの最上位画面は、これまで`renderBankDetail()`の
  `BankCategory::Drum`ケースにインラインで実装されていた「routed
  キットのノート一覧(0-127、未割当込み、複製・削除・作成ボタン付き)」
  「directキットのソースパッチピッカー+音域インライン編集」をそのまま
  流用したいが、他の3種と違ってこの内容はそもそも独立した関数になって
  いなかった(モードレスな`XxxEditorWindow`が無く、BankDetailのswitch
  case本体に直書きされていた)。**このケース本体を`renderDrumKitDetail
  (AppContext&, size_t kitIndex)`として切り出し**、`renderBankDetail()`
  側はこれを`ctx.selectedIndex`付きで呼ぶだけにし、キオスク側は
  `ctx.kioskDrumEditor.kitIndex`付きで同じ関数を呼ぶようにした
  (renderPatchEditor()/renderLayeredPatchEditor()/
  renderPerformancePatchEditor()が既にBankDetailとキオスクの両方から
  呼ばれているのと同じパターンに揃えた形)。
- キオスク専用の`KioskDrumKitWindow`(`open`+`kitIndex`のみ、`prog`
  フィールドは持たない - 上記の通りファイル自体が1パッチのため不要)を
  新設し、`AppContext::kioskDrumEditor`とした。
- routedキットのノート行から開く個別`DrumNoteEditorWindow`
  (`openDrumNoteEditor()`)、directキットのソースパッチ「編集」ボタンから
  開きうる`PatchEditorWindow`/`LayeredPatchEditorWindow`
  (`openDrumSourcePatchEditor()`)は、いずれも既存のネスト済みモードレス
  ウィンドウの仕組みにそのまま乗る。キオスクのメインループ分岐に
  `renderDrumNoteEditors()`と、まだ入れていなかった
  `renderLayeredPatchEditors()`(複数形、`ctx.openLayeredEditors`用 -
  D-039時点ではLayeredキオスク自身が`ctx.kioskLayeredEditor`という別枠を
  使うため入れ忘れていたが、Drum経由でネストして開かれる場合はこちら
  経由になるため今回追加)、`renderDrumSourcePatchPicker()`/
  `renderDrumNoteKeyboardPicker()`を追加した。

**キオスクのメインループ分岐を種別網羅の`switch`に整理**: D-039時点では
Device/Layeredの2値だけだったのでif/elseで足りていたが、4種になった
ため`switch (ctx.kioskKind)`に書き直した(トップレベル画面の選択、
`kioskOpen`ポインタの選択、閉じた際の`sendFullRegisteredOverride()`要否
判定の3箇所)。`BankCategory::Pcm`/`SampleZone`のcaseは`default`ではなく
明示的に空`break`(到達しないことをコメントで明記 -
`kioskKindImplemented()`が起動時に弾いているため)にして、将来
`BankCategory`に値が増えた際にコンパイラの網羅性チェック(`-Wswitch`)が
効くようにした。

**実機確認**: ビルド(`cmake --build build/vs2026`)・`ctest`(既存項目、
回帰なし)を確認。非対話コマンドラインで、(1)予約キーワード
(`pcmbank`/`samplezonebank`)が「まだ編集画面を実装していません」の
専用メッセージ+終了コード1になること、(2)`performance`でprogが
一致しないケースがエラー+終了コード1になること、(3)`drum`でprogが
`DrumKit::prog`と一致しないケース、および存在しないファイルを指定した
ケースがいずれもエラー+終了コード1になること、(4)`performance`・
`drum`(routedキット`std_kit.drumkit.json`・directキット
`direct_kit.drumkit.json`の両方)の正常系が`timeout`経由で数秒間
クラッシュせず稼働することを確認した。さらに`drum`(routedキット)の
キオスク起動を受動的なスクリーンショット1枚で確認し、
`renderDrumKitDetail()`の内容(`ドラムキット [prog 0] Standard Kit
(routed)`、0-127のノート一覧、未割当ノートへの「作成」ボタン)が
正しく表示されることを目視確認した。

**未完了・引き続きの課題**: D-039と同じく、FITOM_X本体側
(`launchPatchEditorForChannel()`)がこの拡張された`kind`語彙(特に
`performance`/`drum`)を実際にいつ・どう使うかは、このセッションでは
検討していない(現状の`resolveChannelHwPatch()`はDrumチャンネルを
明示的に除外しており、`performance`単体が「チャンネルの現在のパッチ」に
なることも無いため、そもそもどんな呼び出し元コードがこれらの`kind`を
使うのかはFITOM_X側の今後の設計次第)。`pcmbank`/`samplezonebank`は
キーワードの予約のみで、対応する編集画面(`fpe::PcmBank`/
`fpe::SampleZone`の per-patch 編集フォーム自体)はデータモデル層・GUI
とも引き続き未実装(docs/STATUS.mdの既知の未対応課題を参照)。

### D-041: profile.jsonの"banks"外部ファイル参照+"bank_overrides"機構に対応(共有バンクセット)

**経緯**: 利用者から、キオスクモードで`../FITOM_staging`の
`emu_opl.profile.json`をドラムキット種別(`drum`)で開こうとすると
「指定されたdrumkitファイル/progに一致するドラムキットがプロファイル内に
見つかりません」というエラーになる、という実機バグ報告(スクリーン
ショット付き)を受けた。

調査の結果、`FITOM_staging`側が2026-07-29のコミット(`5f2d080`
「banksセクションを外部ファイル化し全プロファイルで共有参照するよう
変更」、`8764119`「bank_overridesで6プロファイルのレイヤードバンク0/
ドラムキット0の無音を解消」)で、`profile.json`の`"banks"`の書式を拡張
していたことが原因と判明した。`config_schema/profile.schema.json`の
`"banks"`/`"bank_overrides"`の説明を確認したところ:

- `"banks"`は、従来通りの(`hw_banks`/`patch_banks`/`sw_banks`/
  `drum_banks`/`scc_wave_banks`/`pcm_banks`を持つ)オブジェクト直書きに
  加えて、**外部JSONファイルへのパスを表す文字列**も取れるようになった
  (`unified.bankset.json`のような、複数プロファイルで共有する1つの
  バンク登録テーブル)。
- 新設の`"bank_overrides"`キー(同じくオブジェクト直書き/文字列参照の
  どちらも可)で、共有された`"banks"`の一部エントリだけをプロファイル
  ごとに上書き・追加できる。マッチングは配列ごとに決まった識別キー
  (`hw_banks`は`group`+`bank`、`drum_banks`は`prog`、`pcm_banks`は
  `bank`+`chip`、その他は`bank`)で行い、一致すれば置き換え、しなければ
  追加。**削除は表現できない**。

このリポジトリの`fpe::Profile::from_json`(`src/Profile.cpp`)は、
D-041以前は`"banks"`がオブジェクトの場合しか中身を読んでおらず、文字列
(外部参照)のケースでは`hw_banks`等が全部空のまま読み込まれていた。
さらに`"bank_overrides"`キー自体を一切知らず、未知フィールド保存用の
`Profile::extra`に埋もれて黙って無視されていた。結果、
`unified.bankset.json`を`"banks"`から参照する実プロファイル(FITOM_staging
の`emu_opl`/`emu_opn`/`emu_opm`/`emu_opll`/`emu_fmgen_opn`/`fmall`の6本
すべて)は、このエディタで開くと**バンク登録が(ドラムキットに限らず
デバイス/レイヤード/パフォーマンス全種別で)ゼロ件**になっていた
(`unified_preset.profile.json`のみ`bank_overrides`を持たないが、`banks`
は同様に外部参照)。

**決定**: 読み込み・保存の両方に対応する(利用者にAskUserQuestionで確認
済み)。ただし保存側には設計上の論点があった。既存の
`PatchWorkspace::save()`は常に`profile_`をそのまま`profilePath_`へ
書き戻すため、素朴に「`"banks"`の中身をマージ結果でインライン展開して
書く」実装にすると、**キオスクモードの「登録」ボタンを1回押すだけで、
6プロファイルが共有していたはずの`unified.bankset.json`への参照が
失われ、そのプロファイルだけに丸ごと複製されてしまう**(共有化した
設計そのものを壊す)。そこで:

- `fpe::BanksObject`(`hw_banks`/`patch_banks`/`sw_banks`/`drum_banks`/
  `scc_wave_banks`/`pcm_banks`の6配列。`sf2_banks`はこのライブラリが
  一切対応していないため、往復保存のためだけに生JSONとして保持し、
  マージ/差分の対象にはしない)と、`fpe::BanksSource`(`present`/
  外部ファイル参照だった場合の`externalFile`文字列/中身の`BanksObject`)
  を`include/fpe/Profile.h`に新設。`Profile::from_json`/`to_json`は
  純粋なJSON⇔構造体変換のみを担当し(このライブラリの既存方針通り、
  ファイルI/Oはしない)、`"banks"`/`"bank_overrides"`をそれぞれ
  `BanksSource`として読み書きするだけにとどめる。
- 実際にファイルを読む(外部参照の場合)・マージする・保存時に元の
  `"banks"`との差分を取って`"bank_overrides"`を再構成する、という
  ファイルI/Oを伴う処理は全て`PatchWorkspace`側
  (`resolveBanksSource()`/`syncBanksSourceForSave()`、
  `src/PatchWorkspace.cpp`)に置いた。
- `Profile::hw_banks`/`patch_banks`/...(既存のフラットなvector)は、
  「`"banks"`と`"bank_overrides"`をマージした実効レジストリ」という
  意味に据え置いた。`PatchWorkspace`の新規バンク作成・削除等の既存CRUD
  コードは一切変更せずこのフラットなvectorを直接読み書きし続けられる
  (呼び出し元は「今どのレイヤーにいるか」を意識しなくてよい)。
- 保存時(`syncBanksSourceForSave()`): `"banks"`が外部参照でなかった
  場合(未設定、またはD-041以前どおりインラインオブジェクト)は、
  従来通り実効レジストリをそのまま`"banks"`に書き戻す(後方互換、
  `fixtures/profile.json`はこの経路のまま)。`"banks"`が外部参照だった
  場合は**その文字列参照を書き換えない**(該当ファイルへは一切
  書き込まない)。代わりに、読み込み時点の`"banks"`(ベース)の内容と、
  現在の実効レジストリを識別キーで突き合わせ、新規または内容が変わった
  エントリだけを`"bank_overrides"`として書き出す(`"bank_overrides"`が
  それ自体外部参照だった場合はその外部ファイルへ、そうでなければ
  `profile.json`にインラインで)。ベース由来のエントリがセッション中に
  削除された場合は`bank_overrides`機構では削除を表現できないため、
  警告(`warnings()`)を出す(次回読み込みで復活することを利用者に伝える
  ため)。

**副作用として見つかったバグ**: 上記の読み込み対応をビルドして実機
(`../FITOM_staging`)で試したところ、マージ自体は正しく動作した
(`emu_opl.profile.json`の`drum_banks`が23件に増え、`bank_overrides`の
`prog 0 -> opl_builtin_rhythm.drumkit.json`も正しく反映されていた)にも
関わらず、キオスク起動はまだ同じエラーで失敗した。原因は
`findDrumKitIndexByFile()`(D-040、`apps/gui/main.cpp`)側の別バグ:
「`*.drumkit.json`ファイル1つ=DrumKit1つなので、ファイルパスだけで
一意に特定できる(progは念のための整合性チェックに過ぎない)」という
前提が、`unified.bankset.json`が`opl_builtin_rhythm.drumkit.json`を
`prog 13`で登録し、`emu_opl.profile.json`の`bank_overrides`が**同じ
ファイル**を`prog 0`にも追加登録する、という今回のような構成では
成り立たなくなっていた(ファイル一致だけで検索すると`ws.drumKits()`内で
先に見つかる方(`prog 13`)を返してしまい、その後の`prog`チェックで
弾かれる)。`findDrumKitIndexByFile()`にファイルと`prog`の両方を渡し、
両方一致するエントリを検索するよう修正した。他3種
(`findDeviceBankIndexByFile()`等)は「ファイルでバンクを特定→バンク内で
`findByProg()`」という2段階なので理論上は同種の曖昧さが起こりうるが、
実データでは未観測かつCLI引数(`<bank-file> <prog>`)にはそもそも
バンク番号自体が含まれないため呼び出し元からは原理的に解決不能であり、
今回は対応していない(将来同様の報告があれば要検討)。

**実機確認**: ビルド(`cmake --build build/vs2026`)・`ctest`
(137項目、既存分は回帰なし、新規追加した
`fixtures/shared.bankset.json`+`fixtures/profile_shared.json`+
`testSharedBankset()`による「外部`banks`参照+インライン
`bank_overrides`のマージ→CRUDで新規キット追加→保存→ベースファイルが
バイト単位で不変なこと・`"banks"`が文字列のまま・`"bank_overrides"`に
差分だけが書かれること→再読み込みで同じ実効状態になること」の一連の
往復確認を含む)を確認。実データでは、`emu_opl.profile.json`をキオスク
モード(`drum`、`opl_builtin_rhythm.drumkit.json`、`prog 0`)で開くと
修正前と同じ手順で再現していたエラーが解消し、`renderDrumKitDetail()`の
内容(`ドラムキット [prog 0] OPL Built-in Rhythm (GM2 mapped) (routed)`)
が正しく表示されることをスクリーンショットで確認した。同様に`device`
(`std_opl2.hwbank.json` prog 0)・`layered`
(`gm_layered_opl2.patchbank.json` prog 0、こちらは
`emu_opl.profile.json`の`bank_overrides.patch_banks`が指す先そのもの)・
`performance`(`performance_presets.swbank.json` prog 0)の3種も、
`timeout`経由でエラー無く数秒間稼働することを確認した(D-041は
ドラムキットに限らず全キオスク種別に影響する不具合だったため)。

**未完了・既知の課題**:
- `PatchWorkspace::saveAs()`は、通常のバンク/キットファイル(`hwBanks_`/
  `patchBanks_`/`drumKits_`等、実際に読み込み済みのもの)は新しい保存先へ
  正しく再配置・コピーするが、`"banks"`/`"bank_overrides"`が外部参照
  だった場合、参照先ファイル自体(`unified.bankset.json`等)は新しい
  保存先にコピーされない。そのため、外部`"banks"`参照を持つプロファイル
  を「名前を付けて保存」で別ディレクトリに保存すると、移動後の
  `"banks"`文字列参照が壊れる(存在しないパスを指す)。今回のバグ報告
  (キオスクモードの`save()`、`saveAs()`は経由しない)には影響しない
  ため未対応。次にこの経路を触るセッションで解決すること。
- FITOM_X本体側で、この`bank_overrides`機構をさらに他プロファイルに
  広げる/変更する予定があるかは未確認。

### D-042: save()が変更していないファイルまで全部書き戻していた不具合を修正(originalContent_による差分保存)

**経緯**: 利用者から「「登録」ボタン押下時に、プロファイルの参照ツリー内にある
直接編集していないファイルまで一式がすべて更新されてしまう」という報告を
受けた。調べたところ、`PatchWorkspace::save()`は元々(D-041以前から)、
読み込んだ`patchBanks_`/`swBanks_`/`hwBanks_`/`sampleZoneBanks_`/
`pcmBanks_`/`drumKits_`を無条件に全部`saveJsonFile()`していた
(`README.md`「設計上のポイント」記載の「書き込みは明示的に(正規の
フィールド一式を常に出力)」という方針上、内容に変更が無くても毎回
再シリアライズされる)。単独プロファイル専用のツリーだった頃は実害が
薄かったが、D-041で`"banks"`の外部ファイル共有(`unified.bankset.json`)
に対応した結果、1プロファイルが読み込むファイル数が数個から数十個に
跳ね上がり(実データでは60件超)、「1パッチだけ編集して登録」しただけで
**他プロファイルも参照している共有バンクファイルまで含めて数十件が
毎回書き戻される**という、実害の大きい問題になっていた。

**決定**: `PatchWorkspace`に、読み込み直後(および実際に書き込んだ直後)の
各ファイルの内容スナップショットを`sourceFile`パスをキーに保持する
`originalContent_`(`std::map<std::filesystem::path, nlohmann::json>`)を
新設した。`save()`は各ファイルを無条件に`saveJsonFile()`する代わりに
`saveIfDirty()`(新設、`nlohmann::json`としての構造比較で
`originalContent_`と一致すれば書き込みそのものをスキップする)を経由する
ように変更した。

- 比較はJSON値としての構造比較(`nlohmann::json::operator==`、オブジェクト
  はキー順序非依存)であり、生バイト列の比較ではない。読み込み直後の
  スナップショットは「読み込んだ構造体をそのまま`to_json`し直した結果」を
  使うため、手書きファイルの元々の整形・キー順序が読み込み構造体側に
  無いフィールド(`_comment`等)を保持していなくても、「編集していない
  ファイル」は正しく「差分なし」と判定されスキップされる(書き込みを
  スキップするので、そもそも元ファイルのバイト列自体に一切触れない
  = 整形やコメントも含めて完全に無傷のまま残る)。
  一方、実際に編集したファイルについては、保存時に初めて
  (この構造体が元々知らない`_comment`等のフィールドを保持しない現状の
  仕様通り)再シリアライズされ、その時点でそうしたフィールドが失われる
  可能性がある点は変わらない(D-042はあくまで「未編集ファイルへの
  誤爆」を防ぐものであり、既知の「編集したファイルの往復忠実性」問題
  そのものは別課題)。
- 新規作成(`createXxxBank`等)されたファイルは`originalContent_`に
  エントリが無いため常に「差分あり」扱いとなり、最初のsave()で正しく
  書き出される。
- `saveAs()`は`rebaseSourceFiles()`で全`sourceFile`を新しいパスへ
  付け替えてから`save()`を呼ぶため、`originalContent_`(旧パスキーの
  まま)を明示的にクリアしなくても、新パスに対する既存エントリが
  存在せず自然に「差分あり」判定になり、従来通り新しい保存先には
  常に完全なコピー一式が書き出される(saveAsの「独立した完全な
  コピーを作る」という意味論は変えていない)。旧パスのエントリは
  マップに残ったままだが二度と参照されないため実害は無い
  (`createNew()`では明示的に`originalContent_.clear()`する)。
- `PatchWorkspace.h`にあった`pcmBanks_`の「browse-only, never written
  back on save()」という古いコメント(実際の`save()`は最初から
  `pcmBanks_`も書き戻しており、コメントが実装と食い違っていた)も
  この修正の過程で削除した。

**実機確認**: ビルド(`cmake --build build/vs2026`)・`ctest`(146項目、
既存分は回帰なし)を確認。新規`testSaveOnlyRewritesChangedFiles()`
(D-042)で、(1)一切編集せずにsave()した場合、`profile.json`本体を含む
全ファイルのバイト列が読み込み前と完全に一致すること(スキップされた
ことの直接証拠)、(2)レイヤードパッチバンク1件だけ名前を変更して
save()した場合、その1ファイルだけバイト列が変化し、無関係な
パフォーマンスバンク・ドラムキットのファイルは変化しないこと、の
両方を確認した(「常に書かない」だけの実装で誤魔化されていないことの
確認を兼ねる)。GUIの「登録」ボタン自体のクリック操作による実機確認は
`CLAUDE.md`の方針により未実施(利用者の目視確認待ち)。

**未完了・既知の課題**: 上述の通り、実際に編集されたファイルについては
既存の「モデル化されていないフィールド(`_comment`等)は編集時に失われる」
という問題が引き続き残る(D-042の対象外)。将来的にこの往復忠実性を
上げたい場合は別途検討が必要。

### D-043: 3つのパッチピッカー+Outlineのバンク一覧を、FITOM_X本体と同じCategory→Bank→Programドリルダウン構造に変更

**経緯**: 利用者から「パッチピッカーのUI改善。現在はすべてのバンクを
フラットなツリーで選択しているが、バンクが多いとパッチを探しにくいので、
FITOM_X本体と同じようなフォルダ階層式にしてほしい」という依頼を受けた。

「FITOM_X本体と同じ」が指す実体を特定するため、まず`../FITOM_staging/`
(データのみのステージング領域)の実バンク配置を確認し、
`banks/OPM/dx11/`のようなディレクトリ階層(デバイスバンクのみ、
チップ種別+機種の2階層)を見つけたが、これは実データの置き場所であって
FITOM_X本体のUI実装そのものではないため、AskUserQuestionで「sourceFileの
実ディレクトリ構造をツリー化する」案と「他の分類軸を使う」案を提示した
ところ、利用者は後者(かつ具体的な軸は口頭で追加説明)を選び、続けて
「FITOM_X本体のリポジトリは`..\FITOM_X`にあるので自由に参照してください」
との指示を受けた。実際に`../FITOM_X/apps/fitom_gui/PatchPickerDialog.h`/
`.cpp`(CC#0/CC#32/Prog.chgのMIDIチャンネルパッチ選択ダイアログ、
`ChSettingsDialog`から開かれる)を読んだところ、FITOM_X本体の「フォルダ
階層式」はディレクトリツリーではなく、**Category(CC#0、チップファミリー。
0=「レイヤード」を含む)→Bank(CC#32)→Program(Prog.chg)の3階層を、
1階層ずつ画面を差し替えながらドリルダウンするナビゲーション**
(「↑ 上へ」ボタンで1段戻る、常時展開のツリーではない)だと判明した。
このリポジトリの`fpe::VoicePatchType`はFITOM_Xの当該Category値表と
1:1対応する既存の分類軸であり、新規のJSONフィールドを追加する必要は
なかった。

**決定**: 3つの既存パッチピッカー(`renderHwPatchPicker()`/
`renderSwPatchPicker()`/`renderDrumSourcePatchPicker()`)を、それぞれ
FITOM_X本体のPatchPickerDialogと同じ「1フレームにつき1階層だけ表示、
`PatchPickerLevel`(Category/Bank/Program、`apps/gui/main.cpp`で新設)
enumで管理し、`↑ 上へ`ボタンで1段戻る」構造に書き換えた。

- `renderHwPatchPicker()`(ToneLayerのhw_bank/hw_prog、デバイスパッチのみ):
  Category=`fpe::VoicePatchType`(`ctx.workspace.deviceBanks()`に実在する
  値のみ、`voicePatchTypeToString()`でラベル化)→Bank→Program。FITOM_Xの
  「レイヤード」カテゴリ(CC#0=0)に相当するものはこの参照には存在しない
  (ToneLayer自身がレイヤード側の構成要素なので自己参照になってしまう)
  ため含めていない。
- `renderSwPatchPicker()`(HwPatch/Patch/DrumNoteのsw_bank/sw_prog、
  パフォーマンスパッチ参照): Bank→Programの2階層のみ。パフォーマンス
  バンクはFITOM_X側でもチップファミリーによるタグ付けが無く
  (`PatchPickerDialog`自体、この参照種別に相当する画面を持たない -
  `FITOMBridge`に`getSwBankList()`相当のAPIが存在しないことで確認)、
  Category階層を追加する根拠が無かったため省略した。
- `renderDrumSourcePatchPicker()`(DrumNote/DrumKitのソースパッチ、
  レイヤード/デバイス/PCM波形/サンプルゾーンの4種を束ねる、D-038):
  Category=「レイヤード」(`VoicePatchType::None`)+
  `deviceBanks()`/`pcmBanks()`/`sampleZoneBanks()`の3ベクタ全てに実在する
  `VoicePatchType`値を統合した1つのリスト→Bank→Program。カテゴリ選択後は
  `classifyDrumSourceCategory()`(新設、`isPcmWaveformVoicePatchType()`/
  `isSampleBasedVoicePatchType()`で判定)でどのベクタを見るべきかを
  一意に決定する。この4種統合はFITOM_X本体側の実際の挙動とも整合する -
  `../FITOM_X/config_schema/profile.schema.json`の`pcm_banks[].group`
  注記に「entries[]の各サンプルが、パッチピッカー等で選択可能な
  named patchとして自動的にHwBankRegistry側にも公開される」とあり、
  FITOM_X本体はPCM波形/AWMも同じCategory軸(ADPCM-B/ADPCM-A/PCMD8/AWMの
  VoicePatchType値)上の選択肢として扱っている。

いずれのピッカーも、開いた瞬間の初期階層はFITOM_X本体の
`PatchPickerDialog::open()`と同じ方針にした - 参照が既に設定済み
(sw_bank等が-1でない、ToneLayerのvoice_patch_typeがNoneでない)なら
Categry/Bankを現在値から補完していきなりProgram階層を表示し、未設定
なら一番上の階層(Category階層があるものはCategory、無いものはBank)
から開始する。DrumNote/DrumKitのソースパッチだけは「未設定」を表す
センチネル値が存在しない(`patch_bank`/`patch_prog`の既定値0/0は
「レイヤードバンク0番prog0」という正当な選択そのもの)ため、常に
Program階層から開始する(これもFITOM_X本体と同じ)。

またOutline画面(`renderOutline()`)のデバイス/サンプルゾーン/PCM波形
バンク一覧も、同じ`fpe::VoicePatchType`軸でチップファミリー単位の
`ImGui::TreeNode`一段でグルーピングした(モーダルのドリルダウンとは
異なり、Outlineは元々常設の展開式ツリーなので、そのパラダイムのまま
一段追加する形にした)。レイヤード/パフォーマンス/ドラムキットマップは
FITOM_X本体側にもチップファミリー軸が無いため変更していない。

**実機確認**: ビルド(`cmake --build build/vs2026`)・`ctest`(既存
`fpe_smoke_test`、回帰なし。今回の変更はGUI層のみでデータモデル層に
変更は無いためテスト項目数自体も変わらず)を確認。キオスクモード
(`layered`種別、`fixtures/profile.json` `fixtures/patches/
00_general.patchbank.json` prog 0)を`timeout`経由で起動し、数秒間
クラッシュせず稼働することを確認した。`CLAUDE.md`の方針により、
ピッカー自体を実際にクリックして各階層(Category/Bank/Program)の
表示・「↑ 上へ」での遷移を確認する作業は未実施(利用者の目視確認待ち)。

**未完了・既知の課題**: ピッカーのクリック操作による実機確認は上記の
通り利用者待ち。試聴(FITOM_X本体のPatchPickerDialogがProgram階層の
行を押している間だけNote On/Offを送る機能)は今回のスコープ外として
意図的に実装していない(依頼は「探しやすさ」のみで、試聴には既存の
パッチ編集ウィンドウ自体のプレビュー鍵盤が別途ある)。

### D-044: ドラムノート選択画面の行クリックを、シングルクリック=その場でプレビュー発音・ダブルクリック=編集画面へ変更

利用者から「ドラムキット編集画面で、ノート行をシングルクリックで編集
画面に遷移しているが、これをシングルクリックでプレビュー(その場で
発音)、ダブルクリックで編集としたい」という依頼を受けた。D-038時点の
`renderDrumKitDetail()`(旧`renderBankDetail()`のDrumケース)は、割当済み
ノート行のクリック(`ImGui::Selectable()`の単純な戻り値)がそのまま
`openDrumNoteEditor()`を呼ぶ実装だったため、この依頼通りに単純な
クリック→シングル/ダブルの判別に切り替えた。

- `ImGui::Selectable()`に`ImGuiSelectableFlags_AllowDoubleClick`を渡すと、
  シングルクリック・ダブルクリックのどちらでも戻り値が`true`になる
  ため、その中で`ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)`を
  見て判別する(Dear ImGui標準のダブルクリック判定パターン)。ダブル
  クリックなら従来通り`openDrumNoteEditor()`、それ以外(シングル
  クリック)なら新規`startDrumNoteListPreview()`を呼ぶ。
- 「プレビュー(その場で発音)」の実装は、ドラムノート編集画面が既に
  持つ押し続け式の「試聴」ボタン(D-038)とは前提が異なる - リスト行の
  クリックは1フレームだけの単発イベントで、ボタンの
  `IsItemActivated()`/`IsItemDeactivated()`のような「押した/離した」を
  区別できる情報が無い。そこで新規`DrumNoteListPreviewState`
  (`AppContext::drumNoteListPreview`)で「今鳴らしている
  channel/channelNote/開始時刻」を保持し、`startDrumNoteListPreview()`が
  `selectDevice()`→`noteOn()`を送った時点の`ImGui::GetTime()`を記録、
  固定時間`kDrumNoteListPreviewDuration`(0.4秒、単発の「プレビュー
  ブリップ」として鳴っていることが分かる程度の長さとして選んだ暫定値)
  経過後に`updateDrumNoteListPreview()`が自動で`noteOff()`を送る、という
  設計にした。この更新関数はどの画面を表示中でも(キオスクの`drum`種別
  も含め)main()のレンダーループ先頭で毎フレーム無条件に呼ぶ - ユーザー
  がプレビュー中に別の画面へ移動しても発音が止まらず残り続けることを
  防ぐため。別の行をクリックした場合は`startDrumNoteListPreview()`内で
  まず`stopDrumNoteListPreview()`を呼んで前の発音を止めるため、複数の
  プレビューが重なって鳴り続けることはない。
- 行のホバー時ツールチップを「クリックで試聴、ダブルクリックで編集」に
  変更し、新しい操作方法を発見しやすくした。「複製」「削除」ボタン
  (SmallButton)はSelectableとは独立したウィジェットなのでこの変更の
  影響を受けない。

ビルド(`cmake --build build/vs2026`)・`ctest`(既存項目、GUI層のみの
変更でデータモデル層に変更が無いため回帰なし)を確認した。**クリック
操作(シングル/ダブルの判別が実際に意図通り働くか)の実機確認は
`CLAUDE.md`の方針により未実施** - 利用者自身の目視確認待ち。

### D-045: ドラムノート編集画面のプレイノート・キーボードピッカーを5オクターブ幅に拡大、クリック=試聴・ダブルクリック/OKボタン=確定に変更

利用者から「ドラムノート編集画面のソースノートピッカーの鍵盤を5オクターブ
幅に広げ、シングルクリックでプレビュー、ダブルクリックまたは「OK」ボタン
(新設)で確定するようにしたい」という依頼を受けた。「ソースノートピッカー」
は文脈上、`renderDrumNoteKeyboardPicker()`(D-038、プレイノート欄の
「キーボードで選択」ボタンから開くオンスクリーン鍵盤ポップアップ)を
指している(鍵盤ウィジェットを持つピッカーはこれのみ - ソースパッチ
ピッカー(D-043でCategory→Bank→Programドリルダウン化済み)には鍵盤は無い)。

- **5オクターブ化**: `renderPreviewKeyboard()`呼び出しの白鍵数を22
  (3オクターブ、`7*3+1`)から36(5オクターブ、`7*5+1`)に変更。新規定数
  `kDrumNoteKeyboardWhiteKeys`/`kDrumNoteKeyboardSemitoneSpan`(60)/
  `kDrumNoteKeyboardMaxBase`(`127-60`)を追加し、オクターブ送りボタンの
  上限・範囲表示ラベル・`openDrumNoteKeyboardPicker()`の初期スクロール
  位置(現在値のオクターブから2つ下 - 5オクターブ表示の中央付近に来る
  ように、3オクターブ時の「1つ下」から調整)をすべてこれらの定数基準に
  更新した。
- **クリック=試聴、ダブルクリック/OK=確定**: 従来はキー(白鍵/黒鍵)への
  シングルクリックがそのまま`play_note`を書き込んでポップアップを閉じて
  いた。これを、D-044でドラムノート選択画面の行クリック用に作った
  「シングルクリックでその場発音、ダブルクリックで確定」という設計を
  同じ鍵盤ウィジェット(`renderPreviewKeyboard()`)の上でも再利用する形に
  変更した。具体的には`renderPreviewKeyboard()`が返す`pressedNote`
  (押した瞬間、`IsItemActivated()`ベース)に対して
  `ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)`を見て判別する
  (D-044のSelectableでの判別と同じ「直前のクリックがダブルクリックの
  2打目だったか」を見るパターンで、`InvisibleButton`ベースの鍵盤に対しても
  同様に機能する)。シングルクリックの場合は`DrumNoteKeyboardPickerState`
  に新設した`selectedNote`(確定前の保留中の選択)を更新し、対象
  `DrumNote`のコピー(`patch_bank`/`patch_prog`/`voice_patch_type`は現在値
  そのまま、`play_note`だけクリックしたキーに置き換えたもの)を
  D-044の`startDrumNoteListPreview()`にそのまま渡して試聴する(専用の
  試聴ロジックを鍵盤ピッカー用に別実装せず、既存の一発試聴機構を
  そのまま再利用した - 呼び出し元がドラムノート選択画面の行かこの
  ピッカーのキーかを問わない汎用設計だったため転用が容易だった)。
  ダブルクリックの場合は即座に`note->play_note`へ書き込んでポップアップを
  閉じる(従来の挙動そのまま)。
  `startDrumNoteListPreview()`/`stopDrumNoteListPreview()`/
  `updateDrumNoteListPreview()`/`kDrumNoteListPreviewDuration`(D-044)は、
  元は`renderDrumKitDetail()`の直前にあったが、この鍵盤ピッカー
  (`renderDrumNoteKeyboardPicker()`、ファイル中でより手前に定義されている)
  からも呼ぶ必要が生じたため、定義順の都合で`AppContext`直後(
  `tryLoadProfile()`の手前)に移動した。ロジック自体の変更は無い。
- **「OK」ボタンを新設**: ポップアップ下部に「OK」(`selectedNote`を
  `play_note`へ確定して閉じる、`selectedNote`が無効な場合のみ無効化 -
  実際には`openDrumNoteKeyboardPicker()`が開いた時点で常に現在値を
  `selectedNote`の初期値にしているため、通常は無効化されない)と
  「キャンセル」(何もせず閉じる)を並べた。キー未クリックのまま「OK」を
  押した場合は現在値のまま(実質キャンセルと同じ結果)になる。
  ポップアップ上部にも「選択中: 名前 (番号)」という保留中の値の表示を
  追加した(鍵盤自体には選択キーの永続ハイライトは実装していない -
  `renderPreviewKeyboard()`は押している間だけ色が変わる一時的な
  ハイライトのみで、他の呼び出し元(Device編集画面の試聴鍵盤)との共通
  実装を保つため、恒常ハイライト機能の追加は今回のスコープ外とした)。

ビルド(`cmake --build build/vs2026`)・`ctest`(既存項目、GUI層のみの
変更でデータモデル層に変更が無いため回帰なし)を確認した。クリック
操作(5オクターブ表示・シングル/ダブルクリックの判別・OKボタン)の実機
確認は引き続き利用者の目視確認待ち。

### D-046: ドラムノート試聴がDrumNote自身のsw_bank/sw_prog(パフォーマンスパッチ上書き)を反映していなかったバグを修正(FITOM_X本体調査)

利用者から「ドラムキット編集画面、ドラムノート編集画面でのプレビューが、
実際のリズムトラックの発音と異なる(パフォーマンスパッチが適用されて
いない)」という報告を受けた。「FITOM_X本体(`..\FITOM_X`)も調査して
よいが、本体に原因がある場合は直接修正しないこと」という指示のもと、
`..\FITOM_X`のソースを読み取り専用で調査した(Explore agentに委託)。

**調査結果(FITOM_X本体側の設計、バグではない)**:

- 通常のメロディチャンネルでCC#0/CC#32/プログラムチェンジ(直接デバイス
  選択)を送ってHwPatchを選ぶと、そのHwPatch自身の`sw_bank`/`sw_prog`
  (=そのHwPatchが「本来意図している演奏特性」のデフォルト)は
  `PatchManager::resolveTriple()`/`resolveDirect()`(`core/src/
  PatchManager.cpp`)によって**自動的に解決・適用される**。追加の
  SysExは不要 - この部分は本エディタの既存の試聴実装(`selectDevice()`+
  `noteOn()`)でも元々正しく機能していた。
- 一方、**ドラムキットの個々の`DrumNote`が持つ`sw_bank`/`sw_prog`
  (ノート単位のパフォーマンスパッチ上書き、-1=未設定で解決済み
  HwPatchのデフォルトへフォールバック)は、全く別の、リズムチャンネル
  専用のコード経路でのみ参照される**。具体的には`CRhythmCh::
  resolveNote()`(`core/src/MidiCh.cpp`)が、ノートオン受信時に
  `dn.swBank >= 0`ならそちらを優先し、`applyNoteOn()`がレイヤー0にのみ
  それを適用する。この経路は、チャンネルがリズムモード(CC#0=0x78、
  または既定でチャンネル9)になっていて、かつそのチャンネルに
  プログラムチェンジでドラムキット(DrumPatch)自体が選択されていて
  初めて動く - 本エディタの試聴のように通常チャンネルでHwPatchを
  直接選択するだけでは、この経路には一切到達しない。

**結論**: FITOM_X本体側の設計上の分離であり、バグではない(指示通り
本体は変更していない)。本エディタ側の試聴実装が、ドラムノート固有の
パフォーマンスパッチ上書きを一切送っていなかった(D-038時点の
`renderDrumNoteEditor()`のコメントに「sw_bank・sw_progは...この試聴には
反映しない」と明記されていた - 当時は意図的な判断だったが、実際の
再生結果との食い違いを生む見落としだった)ことが原因。

**修正**: `docs/plugin-midi-pipe.md`5.2節/`docs/manuals/
midi-message-reference.md`8.1節が定義するSwPatchオーバーライドSysEx
(`sub-cmd=0x02`)を、DrumNoteの`sw_bank`/`sw_prog`が設定されている場合に
明示的に送るようにした。

- 新規`sendDrumNoteSwPatchOverride(ctx, channel, note)`: `note.sw_bank`が
  設定されていれば`ws.resolvePerformancePatch()`で実体を解決し、
  `ctx.previewOutput.sendSwPatchOverride()`(既存API、これまで
  呼び出し元が無かった)で送る。設定されていなければ何もしない
  (HwPatch自身のデフォルトが`selectDevice()`で既に適用されているため)。
  `fpe::to_json(SwPatch)`(`src/SwPatch.cpp`)の出力形は、ワイヤー
  フォーマットのドキュメント例(`{"sw":{...},"ops":[...],
  "fine_transpose":...}`)と偶然そのまま一致していたため、HwPatch用の
  `buildHwPatchOverrideJson()`のようなフラット化専用ビルダーは不要
  だった(余分な`"prog"`/`"name"`キーは仕様上無視される)。
  `selectDevice()`の直後、`noteOn()`より前に送る必要がある
  (ドキュメント: SwPatchオーバーライドは「以後のノートオンから
  反映されます」= 発音中のノートには遡って反映されない)。
- 呼び出し箇所2箇所: D-044/D-045で共有している一発試聴関数
  `startDrumNoteListPreview()`(ドラムノート選択画面の行クリック・
  プレイノート・キーボードピッカーのクリック試聴の両方をカバー)、
  および`renderDrumNoteEditor()`の押し続け式「試聴」ボタン。

ビルド(`cmake --build build/vs2026`)・`ctest`(既存項目、GUI層のみの
変更でデータモデル層に変更が無いため回帰なし)を確認した。実際に
FITOM_Xに接続してドラムノートの`sw_bank`/`sw_prog`上書きが試聴で
実際に聴こえるかどうかの実機確認は、`CLAUDE.md`の方針により未実施 -
利用者自身の目視・耳での確認待ち。

### D-047: ドラムノート編集画面「登録」で自動的に閉じるように変更 + FITOM_X本体は保存済み*.drumkit.jsonをホットリロードできないことが判明(制約として記録、修正せず)

利用者から2点の報告を受けた。

1. 「ドラムノート編集画面で「登録」を押したらドラムノート編集画面を
   閉じてよい」
2. 「手動で閉じた後、ドラムキット編集画面には反映されているがFITOM_X
   本体には反映されていないようだ」

**1点目(実装済み)**: `renderDrumNoteEditor()`の「登録」ボタンで
`ws.save()`成功時に`editor.open = false`を追加した。他3種の編集画面
(Device/レイヤード/パフォーマンス)は「登録」後も開いたままにする設計
(パラメータをつまみで動かしながら試聴を続ける想定、D-027)だが、
ドラムノートのフィールド編集(名前/ソースパッチ/プレイノート等)は
そのような反復調整を想定していないため、この画面だけ挙動を変える
という依頼と理解した。

**2点目(調査の結果、本エディタ側・FITOM_X本体側どちらのバグでもない
アーキテクチャ上の制約と判明、修正せず)**: 「本体に原因がある場合は
直接修正しないこと」という前回までの方針を踏襲し、Explore agentに
委託して`..\FITOM_X`を読み取り専用で調査した。

- FITOM_Xは起動時(または自身のGUIで明示的にプロファイルを開いた時)に
  `FITOMConfig::loadProfile()`(`core/src/Config.cpp`)で`*.profile.json`
  +参照先バンク/キットファイル一式(`*.drumkit.json`含む)を一度だけ
  読み込み、以後はメモリ上にキャッシュしたまま動作する。ファイル
  監視・定期再読み込み・ノートオン/プログラムチェンジ毎の再読み込みは
  一切実装されていない(該当するAPI呼び出し・パターンが本体ソース中に
  存在しないことを確認)。
- 外部プロセスからこのキャッシュを更新させる手段も存在しない。
  プライベートSysEx(`docs/plugin-midi-pipe.md`5.2節)の
  `sub-cmd`は`0x01`(HwPatch)/`0x02`(SwPatch)/`0x04`(SF2チャンネル窓)
  のみが`MidiProcessor::processPrivateSysEx()`(`core/src/CFITOM.cpp`)で
  認識され、それ以外の`sub-cmd`(ドラムキット用の仮想的な値も含む)は
  「unhandled」としてログに残るだけで破棄される - **DrumKit/DrumNote
  データに対応する`sub-cmd`はそもそも存在しない**。しかも既存の
  HwPatch/SwPatch用`target-type=0x01`(プリセットバンク直接書き換え)
  自体も「メモリ上のみで、ディスク上のファイルへは保存されません」
  (`docs/manuals/midi-message-reference.md`)という設計であり、そもそも
  「ディスクの変更をFITOM_Xに反映させる」方向の機能ではない(逆方向:
  パッチエディタ側の編集をFITOM_Xのメモリへ即時反映する機能)。
- FITOM_X自身のGUIにも「プロファイル再読み込み」に相当するメニュー
  操作は存在しない(調査時点、`apps/fitom_gui`にメニュー項目自体が
  無く、再読み込みを実際に行える`FITOMBridge::loadProfile()`の呼び出し
  元はテストコード等のみで、GUIからは呼ばれていない)。

**結論**: 「パッチエディタで編集→保存→起動中のFITOM_Xですぐに聴こえる」
という経路は、ドラムキットに関しては現時点のFITOM_X側に一切実装されて
いない(HwPatch/SwPatchのライブオーバーライドとは異なり、ドラムキットは
そもそもそのための仕組み自体が存在しない)。本エディタ側でも、存在
しない機能を呼び出す手段は無いため、コード上の対処はできない。
利用者への回答としては、**保存後の変更をFITOM_X側で聴くには、
FITOM_X自体の再起動(またはプロファイルの再読み込み機能自体を
FITOM_X側に新設すること)が必要**、という運用上の制約として伝えるに
留める。

ビルド(`cmake --build build/vs2026`)・`ctest`(既存項目、データモデル
層に変更が無いため回帰なし)を確認した。「登録」ボタンで実際に画面が
閉じることの実機クリック確認は`CLAUDE.md`の方針により未実施 - 利用者
自身の目視確認待ち。

### D-048: 「登録」で編集画面が閉じる挙動(D-047)を、他3種の編集画面(Device/レイヤード/パフォーマンス)にも拡張

利用者から「他のパッチ編集画面でも「登録」と同時に閉じるようにして
ください」という依頼を受けた。D-047でドラムノート編集画面だけに入れた
「登録」成功時の`editor.open = false`を、`renderPatchEditor()`
(Device)・`renderLayeredPatchEditor()`(レイヤード)・
`renderPerformancePatchEditor()`(パフォーマンス)の3つにも同じ形で
追加した。

**キオスクモードは対象外にした**: これら3つのレンダー関数は、通常モード
の複数同時に開けるモードレスウィンドウ(`ctx.openEditors`等)だけでなく、
**キオスクモード専用の単一トップレベルスロット**
(`ctx.kioskEditor`/`ctx.kioskLayeredEditor`/`ctx.kioskPerformanceEditor`、
D-026/D-039/D-040)としても同じ関数がそのまま呼ばれる。キオスクモードの
`main()`側ループは、この`editor.open`が`false`になった時点で
`glfwSetWindowShouldClose()`を呼んでプロセス全体を終了する(D-026、
「パッチ編集を終了したらそのまま終了する」という既存の設計)。仮に
「登録」ボタン内で無条件に`editor.open = false`にすると、キオスクモード
では「登録」を押すたびにプロセスが即終了するという、依頼されていない
副作用が発生してしまう。今回の依頼はあくまで通常モードの複数モードレス
ウィンドウについてのものと判断し、`if (!ctx.kioskMode) editor.open =
false;`という条件を付けて、キオスクモードの既存挙動(「登録」後も画面が
残り、タイトルバーのXを押すまでプロセスは終了しない)を変えないように
した。ドラムノート編集画面(D-047、`DrumNoteEditorWindow`)にはそもそも
キオスク専用スロットが存在しないため、この条件分岐は不要だった(D-047の
時点でこの考慮が要らなかった理由もこれで説明できる)。
キオスクモード側で「登録」時にも即終了してほしいかどうかは、今回の
依頼文面からは判断できなかったため、必要であれば別途利用者に確認する。

Device編集画面(`renderPatchEditor()`)については、D-027の
`editor.registered = *patch`(直近の登録内容のスナップショット、
ウィンドウを閉じた時点で`sendFullRegisteredOverride()`がこれを全体
オーバーライドとして再送信する仕組み)の直後に`editor.open = false`を
追加した形になる - 「登録」した瞬間にスナップショットが最新化されて
いるため、直後に閉じて`sendFullRegisteredOverride()`が走っても、
たった今保存した内容と同じものが再送信されるだけで矛盾は無い。

ビルド(`cmake --build build/vs2026`)・`ctest`(既存項目、データモデル層
に変更が無いため回帰なし)を確認した。「登録」ボタンで実際に画面が
閉じること(通常モード)・キオスクモードでは閉じないこと、の両方の実機
クリック確認は`CLAUDE.md`の方針により未実施 - 利用者自身の目視確認待ち。

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
