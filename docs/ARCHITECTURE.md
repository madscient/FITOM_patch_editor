# 構成と規約

このエディタが今どう作られていて、何を守る必要があるか。読み手は
開発者と AI。現在形で書き、実装が変わったらこの文書も書き換える。
経緯は書かない(判断の理由は `docs/DECISIONS.md`、作業の経過は
`docs/STATUS.md`)。

記述の確度: 断りの無いものは、コードを読んで確かめた事実である(動かして
確かめたものではない)。FITOM_X 側の振る舞いは、FITOM_X のソースと文書を
読んだ記録に基づく。動作を確かめていないものには「未検証」と書く。

## 全体構成

ビルド対象は2つ。

| ターゲット | 種類 | 中身 |
|---|---|---|
| `fpe_data` | 静的ライブラリ | `include/fpe/*.h`, `src/*.cpp`。JSON とC++構造体の相互変換、CRUD、保存。GUI にも MIDI 通信にも依存しない |
| `fitom_patch_editor_gui` | 実行ファイル | `apps/gui/`。Dear ImGui + GLFW + OpenGL3(GLEW)。`fpe_data` の上に乗る |
| `fpe_smoke_test` | テスト実行ファイル | `tests/smoke_test.cpp`。`fpe_data` だけをリンクする |
| `fpe_roundtrip_check` | 開発用の道具 | `tools/roundtrip_check.cpp`。実データの保存の往復を確かめる(「テスト」の節)。`-DFPE_BUILD_TOOLS=OFF` で外せる |

`-DFPE_BUILD_GUI=OFF` で GUI を外すと、vcpkg に GUI 系の依存を要求しない。

`fpe_data` の主なファイル:

| ファイル | 役割 |
|---|---|
| `PatchWorkspace` | プロファイルと参照先の全ファイルを読み、閲覧ツリー・CRUD・保存を提供する。ファイル I/O はここだけが行う |
| `Profile` | `*.profile.json`。バンク登録表(`banks` / `bank_overrides`)と、モデル化しないキーの保持 |
| `HwPatch` / `SwPatch` / `LayeredPatch` / `DrumKit` / `SampleZone` / `PcmBank` | 各バンクファイルの形 |
| `VoicePatchType` | チップ系統の値。CC#0 の直接デバイス選択値と、HwBank のタグを兼ねる |
| `ChipCapabilities` | `spec/chip-capabilities.json` を読み、チップごとのパラメータ定義を引く |
| `BuiltinVoices` | FITOM_X が JSON を経由せず内部で作る音色(OPLL 系 ROM 音色、DSG ビルトイン音色、内蔵リズム)の名前表と番号の規約 |
| `JsonUtil` | `getOr` / `getRequired` と `JsonError` |

GUI のファイル:

| ファイル | 役割 |
|---|---|
| `main.cpp` | 画面・編集ウィンドウ・ピッカー・キオスクモード・`main()` のすべて |
| `PreviewOutput` | 試聴の送信先を1つにまとめる。FITOM_X 内部パイプを優先し、無ければ RtMidi |
| `MidiPipeClient` / `RtMidiOutput` | それぞれのトランスポート(生バイト列の送信と接続状態だけ) |
| `MidiMessages.h` | MIDI バイト列とプライベート SysEx の組み立て。両トランスポートで共有する |
| `Preferences` | プリファレンスの読み書き |
| `ImageLoader` | PNG の読み込み(stb_image) |

依存はすべて vcpkg マニフェストモード(`vcpkg.json`)で解決する。
nlohmann-json、imgui(glfw-binding / opengl3-binding)、glfw3、glew、
rtmidi、stb。リポジトリにサードパーティのソースを置かない。

MSVC では `/utf-8` を全ターゲットに付ける。ソースのコメントと文字列
リテラルが UTF-8 の日本語だからで、外すと文字列リテラルが壊れうる。

### 実行時に探すファイル

| 対象 | 探し方 |
|---|---|
| `assets/`(ALG 接続図・波形画像) | 実行ファイルのディレクトリから上方向に、`assets/alg_diagrams/opn_al0.png` を探す。実行ファイルの場所が取れなければカレントディレクトリから |
| `spec/chip-capabilities.json` | カレントディレクトリから上方向、見つからなければ実行ファイルのディレクトリから上方向(`assets/` と順が逆なのは、テストの実行ファイルが GUI のビルドが置いた写しと同じディレクトリにあるため。ソースの `spec/` を読ませる) |
| `fixtures/`(テスト) | カレントディレクトリから上方向に `fixtures/profile.json` を探す |
| プリファレンス | 実行ファイルと同じディレクトリの `FITOM_patch_editor.preference.json`。実行ファイルの場所が取れなければカレントディレクトリ |

`assets/` と `spec/` はビルド後に実行ファイルの隣へコピーされる
(`CMakeLists.txt` の POST_BUILD)。実行ファイルの場所の取得は Windows
(`GetModuleFileNameW`)だけ実装してあり、それ以外ではカレントディレクトリに
戻る。

## データモデル(FITOM_X のファイルとクラスの対応)

| ファイル | 構造体 | `PatchWorkspace` の入口 |
|---|---|---|
| `*.profile.json` | `Profile` | `profile()` |
| `*.patchbank.json` | `PatchBank` → `Patch` → `ToneLayer`(最大4) | `layeredPatchBanks()` |
| `*.swbank.json` | `SwBank` → `SwPatch` | `performanceBanks()` |
| `*.hwbank.json` | `HwBank` → `HwPatch` | `deviceBanks()` |
| `*.samplezonebank.json`(AWM) | `SampleZoneBank` → `SampleZonePatch` → `SampleZone` | `sampleZoneBanks()` |
| `*.pcmbank.json` と、それが指す `adpcm_json` | `PcmBank` → `PcmBankEntry`、`PcmSwPatchRef`(`swpatches[]`) | `pcmBanks()` |
| `*.drumkit.json` | `DrumKit` → `DrumNote` | `drumKits()` |
| `*.sccwave.json` | `SccWaveBank` → `SccWave`(`data` が16進文字列の形なら、変えない限りその文字列のまま書き戻す) | `sccWaveBanks()` |
| `sf2_banks[]` | 生の JSON のまま保持し、マージも差分もしない | — |

### バンク登録表

- `profile.json` の6つの登録配列(`hw_banks` / `patch_banks` / `sw_banks` /
  `drum_banks` / `scc_wave_banks` / `pcm_banks`)は、`"banks"` の下にある。
  `"banks"` はオブジェクトか、外部ファイルを指す文字列。
- `"bank_overrides"`(同じくオブジェクトか文字列)は `"banks"` の一部を
  上書き・追加する。`Profile::banks` / `Profile::bank_overrides` は読んだ
  ままの形(`BanksSource`)を持つ。
- `Profile::hw_banks` などのフラットな配列は、両者をマージした実効の登録表。
  CRUD はこれを直接読み書きする。外部ファイルの読み込みとマージ、保存時の
  差分の作り直しは `PatchWorkspace` が行い、`Profile` の `to_json` /
  `from_json` はファイル I/O をしない。
- `Profile::extra` は、エディタがモデル化しないトップレベルのキー
  (`hw_plugins`、`midi_inputs`、`devices` など)をそのまま持ち、保存時に戻す。

### チップ種別はプロファイルが決める

- `*.hwbank.json` 自身はチップ種別を持たない。`hw_banks[].group` の文字列を
  `stringToVoicePatchType()` で読んだ値が、そのバンクの `voicePatchType` に
  なる。同じファイルでも、登録する group が違えば別のチップとして開く。
- `hw_banks[]` は group で読み方が3つに分かれる。AWM は
  `SampleZoneBank`、ADPCM-B / ADPCM-A / PCM-D8 / SSGS-ADPCM は `PcmBank`、
  それ以外は `HwBank`。`pcm_banks[]` は常に `PcmBank` で、group があれば
  同じ規則でタグを付ける。
- 各バンクの `voicePatchType` / `bankIndex` / `role` / `sourceFile` は
  読み込み時に付ける実行時の情報で、バンクファイルの JSON には書かない。
- `bankIndex` は登録表の `bank` の値であり、`deviceBanks()` などの配列の
  添字ではない。参照からバンクを引くときは、種別と番号で探す
  (`findDeviceBank()` など)。

### 参照

| 参照 | 指す先 |
|---|---|
| `HwPatch.sw_bank` / `sw_prog`、`Patch.sw_bank` / `sw_prog` | パフォーマンスパッチ。-1 は未設定 |
| `ToneLayer.voice_patch_type` / `hw_bank` / `hw_prog` | デバイスパッチ |
| `DrumNote`(と "direct" キット)の `voice_patch_type` / `patch_bank` / `patch_prog` | `None` ならレイヤードパッチ。PCM 系なら `PcmBank` の `entries[]` の添字(0始まり)。AWM なら `SampleZonePatch` の prog。それ以外はデバイスパッチ |
| `DrumNote.sw_bank` / `sw_prog` | ノート単位のパフォーマンスパッチ上書き。-1 は未設定 |

`deviceBanks()` に現れない参照先がある(下の「FITOM_X 固有の注意点」):
OPLL 系のバンク0、DSG、内蔵リズム(0x70)、無音(0x7f)。名前の解決は
`BuiltinVoices` を通す。

PSG 系(SSG / EPSG / DCSG / SAA / SCC)は、バンクの番号空間を1つ共有し、
SSG のタグで登録される。参照からバンクを探すときは
`hwBankLookupVoicePatchType()` を必ず通す。パッチが実際にどのチップ用かは
`ext.target_voice_patch_type` が決めるので、チップごとの判断(表示する
フィールド、値の範囲、保存時の省略)は `effectiveVoicePatchType()` を通す。

### "direct" ドラムキット

"direct" キットは `notes[]` を持たず、`note_min`〜`note_max` の範囲全体に
1つのソースパッチを割り当てる。`DrumKit::effectiveNotes()` が、"routed" なら
`notes[]` をそのまま、"direct" なら範囲を展開したノート一覧を返す。

## 情報源(FITOM_X のどこを正とするか)

エディタは FITOM_X のコードを共有せず、ファイル形式と MIDI パイプの
プロトコルだけで FITOM_X とつながる。何を正とするかは対象ごとに決めてある。

| 対象 | 正とするもの(FITOM_X リポジトリ内) |
|---|---|
| ファイルの形(キー名、階層、既定値) | `config_schema/*.schema.json` |
| チップごとのパラメータ(使うフィールド、値域、実効範囲、刻み、有効条件) | `spec/chip-capabilities.json`。このリポジトリの `spec/` にそのままの写しを置く |
| VoicePatchType の値 | `core/include/fitom/FITOMdefine.h` |
| group 文字列とその別名、`hw_banks[]` の読み分け | `core/src/Config.cpp` |
| 参照の解決(ROM 音色、内蔵リズム、DSG、PSG 共有) | `core/src/PatchManager.cpp` |
| 内蔵リズムの対象チップ一覧、キオスク起動の呼び出し側 | `gui/bridge/FITOMBridge.cpp` |
| パッチピッカーの構造 | `apps/fitom_gui/PatchPickerDialog.*` |
| MIDI パイプとプライベート SysEx | `docs/interfaces/plugin-midi-pipe.md`、`docs/manuals/midi-message-reference.md` |
| 各フィールドの意味 | `docs/manuals/hwpatch-reference.md` ほか `docs/` の各文書 |

守ること:

- チップごとの範囲・未使用判定・有効条件を、コードに手書きしない。
  `fpe::ChipCapabilities` 経由で読む。写しが見つからないとき、エディタは
  全フィールドを暫定表示にして警告を出し、編集は止めない。
- FITOM_X 側が更新されたら、`spec/chip-capabilities.json` を差し替えて
  `ctest` を通す(手順は `spec/README.md`)。スキーマが変わったときは、
  実データで `fpe_roundtrip_check` も回す(「テスト」の節)。
- FITOM_X を読むときは `git show origin/main:<path>` で読み、FITOM_X の
  作業ツリーには触らない。FITOM_X 側に原因がある問題は、FITOM_X を直接
  直さず利用者に報告する。
- 実データ(FITOM_staging の各プロファイル)は検証に使うが、推測で
  直さない。食い違いは警告として出し、利用者に報告する。
- 各構造体・フィールドには、根拠にした FITOM_X の文書をコメントで書く。
  確かめていない箇所には `NOTE ON CONFIDENCE` と書く。

## 読み書きの規約

### 読み込み

- 欠けたフィールドは既定値で埋め、エラーにしない(`json_util::getOr`)。
  例外はプロファイル自体の JSON が壊れているときで、`JsonError` を投げる。
- 参照先のファイルが無い・壊れている場合は `warnings()` に積んで読み飛ばし、
  残りは使えるようにする。
- `banks.*[].file` の相対パスは、プロファイルファイルのあるディレクトリを
  基点に解決する。`adpcm_json` は `*.pcmbank.json` のあるディレクトリを
  基点にする。
- PCM 波形バンクのエントリの一覧は、FITOM_X と同じく、`adpcm_json`
  (adpcm_packer の出力。`entry_no` は並び順)の上に、`*.pcmbank.json` 自身の
  `entries[]`(スキーマの形。`entry_no` / `start_offset` / `end_offset`)を
  `entry_no` ごとに重ねて作る。範囲は 0〜127 で、外れたものは警告して無視する。
  参照の `patch_prog` は `entry_no` で引く(`PcmBank::findByEntryNo()`)。
  書き戻すのはファイル自身の `entries[]`(`ownEntries`)だけで、`adpcm_json` 由来の
  エントリは書かない。
- group の別名(`OPNA` / `OPNB` → OPN2、`SCCP` → SCC、`PSG` / `PCM` など)は
  読み込みでだけ受け付ける。`HwBankRef::group` は元の文字列をそのまま持ち、
  そのまま書き戻す。

### 書き込み

- 書き出すフィールドは、基本は正規の一式。
- `*.hwbank.json` だけは、そのチップが読まないフィールドを省く
  (`to_json(HwBank)` が `pruneHwPatchJson()` を通す)。ただし次のものは
  必ず残す。
  - 既定値でないキー(仕様書に漏れがあっても値を失わないため)。
  - `driver_support: "pending"` の付いたパラメータ。
- 省くかどうかの判定は、バンクのタグではなく `effectiveVoicePatchType()` の
  チップで行う。
- 試聴用の SysEx で送る HwPatch は、省略せず全フィールドを送る。
- nlohmann の `to_json` / `from_json` は、対象の型と同じ名前空間に置く。
  無名名前空間に入れると、MSVC で ADL が効かずビルドエラーになる。

### 保存(`PatchWorkspace::save()`)

- 書くのは、内容が変わったファイルだけ。読み込み直後(と書いた直後)の
  内容を `originalContent_` に持ち、JSON として比較して同じなら書かない。
  比較はバイト列ではなく構造で行う。
- 1回の `save()` の中では、比較の基準を最初に固定する。同じファイルを
  指すオブジェクトが2つあるとき、最初に書いた側を優先し、後の側では
  書かない。両方を別々に編集した場合は、片方の編集が失われる。
- 新規に作ったファイルは基準が無いので、必ず書く。
- `"banks"` が外部ファイルを指す場合、その参照も参照先のファイルも
  書き換えない。基準との差分を `"bank_overrides"` に書く
  (`bank_overrides` 自体が外部ファイルならそのファイルへ)。照合のキーは
  配列ごとに決まっている(`hw_banks` は group+bank、`drum_banks` は prog、
  `pcm_banks` は bank+chip、その他は bank)。`bank_overrides` は削除を
  表せないので、ベース由来のエントリを消すと警告を出す。
- `"banks"` が外部参照でなければ、実効の登録表をそのまま `"banks"` に書く。
- 各バンクファイルと `bank_overrides` / `banks` のオブジェクトでは、最上位の
  モデル化していないキー(`_comment`、実データの `note` / `source` など)を
  `extra` に持ち、そのまま書き戻す(`json_util::unknownKeys()` /
  `mergeUnknownKeys()`。プロファイル自体は `Profile::extra`)。パッチより下の
  階層のモデル化していないキーは保持しないので、編集したファイルでは失われる。
- サンプルゾーン(`*.samplezonebank.json`)と PCM 波形バンクは、エディタで
  編集しないフィールドもスキーマどおりすべてモデル化する。AWM のゾーンの
  ピッチ校正値などは、書き戻すと失われては困る。
  編集していないファイルは書かないので、そのまま残る。
- `saveAs()` は、読み込んだ全ファイルの相対配置を保ったまま新しい場所へ
  移して書く。`PcmBank` の `adpcm_json` / `bin_file` もコピーする。
  - 外部参照の `"banks"`(共有のバンクセット)はコピーせず、新しい場所から
    元のファイルを指す相対パスに書き換える(ドライブが違えば絶対パス)。
    バンクセットの中の `file` はプロファイルのディレクトリを基点に解決される
    ので、バンクファイル自体は新しい場所へコピーする。
  - 外部参照の `"bank_overrides"` は、プロファイル自身のデータとして
    新しい場所へ書く。

### GUI からの保存の単位

- 構造の変更(新規バンク作成、ドラムノートの作成・複製・削除)は、その場で
  `save()` する。
- フィールドの編集は、各編集画面の「登録」で `save()` する。

## FITOM_X 固有の注意点

### JSON を経由しない音色

- **OPLL 系 ROM 音色**: OPLL / OPLLP / OPLLX / VRC7 / OPLLEX のバンク0は
  JSON で定義できない予約領域で、FITOM_X 内部の ROM 音色を指す。
  - `hw_prog` は `(変種 << 4) | 音色番号(1-15)`。音色番号0は無音の予約。
    変種は `hw_prog` から決まり、参照元の voice_patch_type には依存しない。
  - OPLLEX では上位ビットが OPLLEX 内の BANK(0=OPLL / 1=OPLLX / 2=OPLLP /
    3=VRC7)を選び、60音色になる。
  - プロファイルが OPLL 系のバンク0を JSON で登録していても FITOM_X は
    使わないので、ピッカーには出さない。
- **DSG(YM2163)**: ユーザー音色を持たない。`*.hwbank.json` も group の
  選択肢も無い(`isValidHwBankTag(DSG)` は偽)。どのバンク番号を指しても、
  20音色(波形5種×エンベロープ4種)のビルトイン音色になる。PSG 系の共有
  番号空間には含めない。
- **内蔵リズム(voice_patch_type = 0x70)**: `patch_bank` は対象チップの
  VoicePatchType(OPN2 = OPNA、OPLL、DSG)、`patch_prog` はそのチップの
  パート番号として読む。DrumNote / DrumKit でだけ正規の値で、ToneLayer の
  voice_patch_type はスキーマで 0x01〜0x6F に限られる。OPL 系の内蔵リズムは
  この経路ではなく、通常の HwBank `OPL_RHY` を使う。
- **無音(0x7f)**: CC#0 の選択値で、FITOM_X は bank / prog を見ない。
  エディタは bank / prog に 0 を書く。
- `role == "builtin_swpatch_meta"` のバンクは、ROM 音色と DSG 音色に
  パフォーマンスパッチを結びつける。1つのファイルに `builtin.patch_type` で
  区別して混在し、`patch_no` の範囲は OPLL 系 1〜15、DSG 0〜19。

### チップ

- `OPL_RHY` の `ext.rhythm_ch` は 0=HH / 1=CYM / 2=TOM / 3=SD / 4=BD、255 が
  未設定(未設定は解決失敗で無音)。オペレータ数は BD だけ2、他は1
  (spec の `operator_count`)。
- PSG 系の参照では、FITOM_X は CC#0(参照の voice_patch_type)で鳴らす
  チップを決める。ピッカーが書く voice_patch_type は、バンクのタグではなく
  利用者が選んだカテゴリ。
- `DEVICE_*`(SSGEX、SCCEX、OPL2EX など)は VoicePatchType ではない。
  エディタにはデバイス種別の概念が無く、追従は不要。
- 既知の食い違い: `hw_banks[]` に ADPCM 系の group を書いた場合、エディタは
  `*.pcmbank.json` として読み、FITOM_X は hwbank として読む。実データの
  ADPCM 系はすべて `pcm_banks[]` 経由なので表面化していない。

### MIDI パイプ

- 接続先は Windows が `\\.\pipe\FITOM_X_MIDI`、それ以外が
  `/tmp/fitom_x_midi.sock`(POSIX 側は未検証)。FITOM_X が起動していない
  のは正常な状態で、エラーにしない。
- 接続直後に FITOM_X が `F0 00 48 01 03 <ch> F7` を送り、この接続の
  チャンネルを割り当てる。読むまで何も送らない。以後はこのチャンネルを使う
  (`PreviewOutput::activeChannel()`)。プリファレンスの MIDI CH は RtMidi の
  ときだけ使う。
- 接続は最大16。超えると FITOM_X はハンドシェイク無しで切断する。これは
  未起動と区別し、メッセージボックスを出して終了する。
- 試聴用のパラメータ上書き(メモリ上の一時的な上書き):
  HwPatch は `F0 00 48 01 01 00 <ch> 00 <JSON> F7`、SwPatch は sub-cmd 0x02。
  JSON に含めたキーだけが変わる(差分で送れる)。HwPatch の JSON は
  `buildHwPatchOverrideJson()` で作る。SwPatch の上書きは次のノートオンから
  効く。
- バンクの直接編集(「登録」で送る。FITOM_X のメモリ上のバンクを書き換え、
  ディスクには書かない):
  - HwPatch `F0 00 48 01 01 01 <VPT> <HwBank> <HwProg> 00 <JSON> F7`
  - SwPatch `F0 00 48 01 02 01 <SwBank> <SwProg> 00 <JSON> F7`
  - レイヤードパッチ `F0 00 48 01 06 <PatchBank> <Prog> <JSON> F7`
  - ドラムキット `F0 00 48 01 07 <DrumBank=0> <Prog> <JSON> F7`。FITOM_X の
    ノートは `enabled` が偽だと鳴らないので、各ノートに `"enabled": true` を
    付ける(`buildDrumKitOverrideJson()`)。
  - FITOM_X に登録されていないバンクへの編集は無視される。新規に作った
    バンクは、FITOM_X を再起動するまで反映されない。
- FITOM_X はプロファイルを起動時に一度だけ読み、ファイルを監視しない。
- HwPatch 自身の `sw_bank` / `sw_prog` は、直接デバイス選択の時点で FITOM_X が
  適用する。DrumNote の `sw_bank` / `sw_prog` はリズムチャンネルの経路でしか
  適用されないので、試聴では SwPatch の上書きを明示的に送る。

### キオスク起動

- FITOM_X は子プロセスの終了コードを待たない。起動時のエラーは
  メッセージボックスで出す(`showFatalErrorBox()`)。
- 起動引数の kind(`device` / `layered` / `performance` / `drum` /
  `pcmbank` / `samplezonebank`)は FITOM_X との契約で、内部の列挙子名とは
  独立させてある。`pcmbank` / `samplezonebank` は予約語で、編集画面は無い。

## GUI の構成

### 画面と状態

- 画面は `AppState`(MainMenu / FileBrowser / Outline / BankDetail)で切り替える。
  Outline でバンクを選ぶと、`BankCategory`(Layered / Performance / Device /
  SampleZone / Pcm / Drum)と配列の添字を覚えて BankDetail に移る。
- 編集ウィンドウは4種類(デバイス / レイヤード / パフォーマンス /
  ドラムノート)。モードレスで、`AppContext` の配列に積み、`AppState` に
  関係なく毎フレーム描く。同じパッチを2回開こうとすると、既存のウィンドウを
  前に出す。
- 編集ウィンドウやピッカーは、対象の実体(ポインタ)ではなく
  `{バンクの添字, prog}` などの番号を持ち、毎フレーム引き直す。対象が
  消えていれば、編集ウィンドウは「既に存在しません」と表示し、ピッカーは
  閉じる。
- "routed" ドラムキットのノート一覧と "direct" キットの編集フォームは、
  BankDetail の中に描く(`renderDrumKitDetail()`、キオスクと共用)。

### 編集ウィンドウの共通の振る舞い

- 右上に「リセット」と「登録」(`renderEditorHeaderButtons()`)。
  - 「リセット」は、その画面が初めて描かれたフレームで取った値(`original`)に
    戻す。保存はしない。「登録」しても `original` は更新しない。
  - 「登録」は `save()` し、成功したらバンク直接編集の SysEx を送り、
    ウィンドウを閉じる。キオスクモードの最上位の画面では、閉じるとプロセスが
    終わる。"direct" キットのフォームもキオスクでは同じく終了する。routed
    キットのキオスクは最上位に「登録」が無く、ノートの編集ウィンドウが
    閉じるだけ。
- デバイスパッチ編集画面だけが、試聴の音を編集に追従させる。
  - 毎フレーム、最後に送った値(`lastSent`)との差分を SysEx で送る。
    `ext` も差分に含める。
  - 閉じるときに、最後に登録した値(`registered`)を全フィールドで送り直し、
    登録していない編集を FITOM_X 側でも取り消す。
- レイヤード / パフォーマンスの編集画面は、合成パラメータを持たないので
  試聴しない。ドラムノート編集画面は「試聴」ボタン(押している間だけ鳴る)を
  持つ。
- 参照(sw_bank / sw_prog、ToneLayer の HW 参照、ドラムノートのソース
  パッチ)は、名前のラベルとして表示し、クリックでピッカーを開く。隣の
  「編集」ボタンは参照先の既存の編集ウィンドウを開く(専用のモーダルは無い)。
  編集画面の無い参照先(ROM 音色、内蔵リズム、DSG、無音、PCM、AWM)では
  無効にする。

### チップ別パラメータの表示(デバイスパッチ編集画面)

- `ChipCapabilities::resolve()` の結果で描く。
  - チップが読まないフィールドは出さない。
  - 条件を満たさず今は読まれないフィールド(`active == false`)は灰色にする。
  - ドライバ未対応(`driver_support: "pending"`)は通常どおり編集でき、
    ラベル末尾に `*` を付けてツールチップで説明する。`##id` で ID は変えない。
- スライダーの範囲は実効範囲。ただしパッチがそれを超える値を持っていれば、
  そこまで広げる。
- `values[]` はドロップダウン、1ビットの `bits[]` はチェックボックス。
  ALG 接続図や WS 波形の画像があるチップでは、画像の左右に ◀▶ を置いた
  スピナーを優先する。
- 編集画面の鍵盤は C2〜C7 の5オクターブ。左に CC#1 / CC#7 のレバー。

### ピッカー

- ピッカーは3種(SW、HW、ドラムソース)と、プレイノート用の鍵盤ピッカー。
  状態は `AppContext` に1つずつ持ち、モーダルとして `main()` の最上位で描く。
- 構造は FITOM_X のピッカーと同じ Category → Bank → Program のドリルダウン
  (`PatchPickerLevel`)。SW ピッカーは Bank → Program だけ。
  - カテゴリは読み込んだバンクから作り、OPLL 系・OPLLEX・DSG・内蔵リズム
    (ドラムソースだけ)を補う。PSG 系のバンクが1つでもあれば5チップを全部出す。
  - HW ピッカーとドラムソースピッカーの末尾には「無音」がある。
- クリックは試聴と仮選択、ダブルクリックか「OK」で確定。仮選択は開いた時点の
  値から始まる。試聴は固定時間(0.4秒)で自動的に止まる単発の音で、
  `updateOneShotPreview()` を毎フレーム呼んで止める。
- モーダルの中から別のモーダルを開くときは、子の描画を親の
  `BeginPopupModal` 〜 `EndPopup()` の内側で呼ぶ。外で呼ぶと、親が入力を
  塞いだまま何も描かれない状態になる。
- パスの入力欄は、ラベルの行、`##id` の入力欄、その直後の「参照...」ボタンの
  組にする。「参照...」は共用のパスピッカー(`PathPickerState`)を開く。

### キオスクモード

- 引数がちょうど4つ(`argc == 5`)のとき。プロファイル全体を読み、
  バンクファイルを `fs::equivalent()` で照合して対象を探す。検証はウィンドウを
  作る前に行う。drum はファイルと prog の両方で探す(同じファイルが
  複数の prog で登録されることがあるため)。
- 対象の編集画面1枚を、ビューポート全体に固定して描く。タイトルバーの
  閉じるボタンか、最上位の画面の「登録」の成功で、プロセスごと終わる。
- ネストして開いた編集ウィンドウとピッカーは、kind に関係なく全部描く
  (開かれていなければ何もしない)。
- kind の分岐は網羅的な `switch` で書き、`default` を使わない。

### その他

- 起動時、プリファレンスを読んだ後、引数のプロファイル、無ければ自動読み込み
  のプロファイルを開く。引数はプリファレンスを書き換えない。
- エラー表示は、ウィンドウを作る前と致命的なものは `showFatalErrorBox()`
  (標準エラー出力と、Windows では `MessageBoxW`)、それ以外は
  `renderErrorPopup()`。
- 日本語フォントは `loadFonts()` が候補パスから探す。Windows 以外の候補は
  未検証。

## テスト

- `ctest` で動くのは `fpe_smoke_test` 1本。テストフレームワークは使わず、
  `CHECK` マクロで項目を数える。作業ディレクトリはソースのルート、
  `TIMEOUT 120`。
- 範囲は `fpe_data` だけ。VoicePatchType の変換と判定、BuiltinVoices、
  読み込み、CRUD と保存・再読み込み、共有バンクセット、変更したファイルだけを
  書くこと、ChipCapabilities の解決(分岐の継承を含む)、hwbank の省略、
  既定値、モデル化していないキーとサンプルゾーン・PCM のフィールドの往復。
  GUI はテストしない。
- `spec/chip-capabilities.json` の値域や条件もテストで確かめているので、
  写しを差し替えて意図しない変更があれば落ちる。
- MSVC の Debug ビルドでは、`main()` の冒頭で CRT の assert ダイアログを
  標準エラー出力に向ける(ダイアログの抑止そのものは未検証)。
- テスト用データは `fixtures/`。共有バンクセット(`shared.bankset.json` と
  `profile_shared.json`)は、同じ drumkit ファイルを2つの prog で登録する
  状況も再現している。
- 実データでの往復の確認には `fpe_roundtrip_check`(`tools/roundtrip_check.cpp`、
  ctest には入れていない)を使う。
  - 使い方: `fpe_roundtrip_check <profile.json> <出力先>`。出力先は空か
    存在しないディレクトリ。
  - プロファイルを読み、出力先の中へ `saveAs()` して元のファイルと比べる。
    既定値以外の値が消えたキーと値が変わったキーを数える。
  - 終了コード: 0 は問題なし、1 は欠落か変化あり、2 は実行しなかった(引数・
    読み込みの失敗、出力先の外へ書く恐れ)、3 は元のファイルが変わった。
  - 元のデータには書き込まない。出力は元の絶対パスの構成を出力先の中に
    写して置く。書く前に全部の書き先が出力先の中か確かめ、書いた後に元の
    ファイルが変わっていないかをバイト単位で確かめる。
  - FITOM_X のスキーマが更新されたら、FITOM_staging の全プロファイルで回す。
    モデル化していないフィールドの取りこぼしがここで見つかる。
  - 「既定値」の判定は簡易なもの(0 / false / 空文字 / null、`rhythm_ch` の
    255、`sw_*` の -1)。FITOM_staging の全プロファイルで、省略されたのは
    hwbank の既知フィールドのこれに当たる値だけだった(D-057 の省略)。

## 環境固有の注意点

- リポジトリをクラウド同期やネットワークのドライブに置くと、大きめの
  書き込みが途中で打ち切られることがある(原因は特定できていない)。
  観測した症状:
  - テキストファイルが UTF-8 の文字の途中で切れて保存される。
  - `git submodule add` が `.git/modules/.../config` の書き込みで失敗する。
  - `.git/index` が壊れ、`git status` が全ファイルを削除扱いにする。
    `git read-tree HEAD` で戻る。作業ツリーは無事なことが多い。
- 対策: ファイルを書き換えたら `wc -l`・`tail`・`git diff` などで中身を
  確かめてから次へ進む。git の操作が失敗したら、まず `git status` /
  `git log` で被害の範囲を見る。
- 開発と確認は Windows でしか行っていない。POSIX のパイプ経路、実行ファイルの
  場所の取得、フォントの候補パスは未検証。
- Windows の `MessageBoxA` は UTF-8 を化かす。UTF-16 に変換して
  `MessageBoxW` を使う。
- リポジトリに2つのパスで到達できる環境(ジャンクションなど)で、同じビルド
  ツリーを両方のパスから configure / build すると、MSBuild がプロジェクトを
  2つと見なし、MSB8028(中間ディレクトリの共有)の警告を出す。中間ディレクトリに
  tlog のフォルダが2つできるのが目印(フォルダ内の `*.lastbuildstate` に、
  どちらのパスのビルドかが書いてある)。使っていない方のパスの tlog フォルダを
  消すと警告は消える(確認済み)。ビルドは1つのパスからだけ行う。
