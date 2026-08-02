#pragma once
#include <string>
#include <vector>

#include "fpe/VoicePatchType.h"

// BuiltinVoices: FITOM_X本体が「JSONのバンク定義(profile.jsonのhw_banks[]
// → *.hwbank.json)を一切経由せずに」内部で機械合成する2種類の音色の、
// 名前テーブルとアドレス変換規約。
//
// この2つは HwBankRegistry(このライブラリで言えば PatchWorkspace::
// deviceBanks())に一切現れないため、{voice_patch_type, bank, prog} を
// deviceBanks() の線形探索だけで解決しようとすると必ず「見つからない」と
// なる。実データ(../FITOM_staging)では両方とも普通に使われており
// (gm_layered_opll.patchbank.json のhw_bank=0参照、opna_builtin.drumkit.json
// / opll_rhythm.drumkit.json の voice_patch_type=112)、参照先の名前が
// 一切表示できない・パッチピッカーから選べないという不具合になっていた。
// FITOM_X本体側でも全く同じ理由で同じ不具合が出ており、2026-08-03の
// コミット75b9ac8「Fix. パッチピッカーでOPLL ROM音色・内蔵リズムが常に
// 空欄になるバグを修正」で修正されている。本ヘッダはその修正
// (PatchManager::getOpllRomPatches()/getOpllRomPatchByProg()、
// FITOMBridge.cppのkBuiltinRhythmChips/kOpnaRhythmNames/kOpllRhythmNames)を
// このエディタ側に移植したもの。
//
// 出典:
//  - FITOM_X core/src/PatchManager.cpp
//    (initOpllRomPatches() / resolveOpllRomVoice() / resolveBuiltinRhythm())
//  - FITOM_X gui/bridge/FITOMBridge.cpp
//    (getHwBankList() / getHwBankPatches() / getChannelMonitors())
//  - FITOM_X docs/patch-structure-design.md
//    「OPLL系ROM音色専用バンク」「内蔵リズム音源専用バンク」各節の
//    "GUIパッチピッカーでの列挙(2026年8月新設)" 追記
//  - FITOM_X config_schema/drumkit.schema.json (voice_patch_type=0x70の記述)

namespace fpe {

// 合成バンク1件分のエントリ。`prog` は配列添字ではなく、実際に
// hw_prog / patch_prog / Program Change として書き込むべき値。
struct BuiltinVoiceEntry {
    int prog = 0;
    std::string name;
};

// ─── OPLL系ROM音色(hw_bank == 0 固定) ────────────────────────────────
//
// OPLL/OPLLP/OPLLX/VRC7 のバンク0は「JSON定義不可の予約領域」で、
// FITOM_Xが opllRomPatches_[4][16] として内部合成する15音色×4変種を指す
// (下位4bit=0はユーザー音色との衝突回避のため意図的に無音として予約)。

// このライブラリが合成バンク0に与える表示名。FITOM_X本体の
// FITOMBridge::getHwBankList() が使う "ROM Preset" と同じ。
const char* opllRomBankName();

// OPLL系の変種セレクタ(0=OPLL, 1=OPLLX, 2=OPLLP, 3=VRC7)。
// OPLLファミリー以外なら -1。PatchManager::getOpllRomPatches() の逆引きと
// 同じ対応。
int opllRomVariantSel(VoicePatchType type);

// {voice_patch_type, bank} がOPLL系ROM音色バンクを指しているか。
// FITOM_XのresolveTriple()が resolveOpllRomVoice() へ分岐する条件と同じ。
bool isOpllRomVoiceRef(VoicePatchType type, int bank);

// `type` のカテゴリ配下に列挙すべきROM音色15件(instIndex 1-15、
// 添字0の無音は除く)。OPLLファミリー以外なら空。
// 各エントリの `prog` は (variantSel << 4) | instIndex ―― 配列添字では
// ないので注意(FITOM_X docs/patch-structure-design.md の「実装上の注意」:
// 生添字を送るとOPLL以外のカテゴリで別チップの音色が鳴る)。
std::vector<BuiltinVoiceEntry> opllRomVoices(VoicePatchType type);

// hw_prog そのものから1件を解決する。PatchManager::resolveOpllRomVoice()/
// getOpllRomPatchByProg() と全く同じデコード規則 ―― 上位3bitが変種、
// 下位4bitがROM音色番号で、**参照元のvoice_patch_type(CC#0)の値には
// 一切依存しない**。実データでも voice_patch_type=OPLL のToneLayerが
// hw_prog=35(=0x23、variantSel=2=OPLLP)でOPLLPの音色を指す例が普通に
// あるため、表示名はカテゴリではなくこちらで解決する必要がある
// (../FITOM_staging/banks/patches/gm_layered_opll.patchbank.json 参照)。
struct OpllRomVoiceRef {
    bool valid = false;
    VoicePatchType variant = VoicePatchType::None; // OPLL / OPLLX / OPLLP / VRC7
    int instIndex = 0;                             // 1-15
    std::string name;
};
OpllRomVoiceRef opllRomVoiceByProg(int hwProg);

// role=="builtin_swpatch_meta" バンク(fpe::BuiltinRef)の
// {patch_type, patch_no} からROM音色名を引く。FITOM_X本体の
// HwBank::findByBuiltinRef() が使う (patchType, patchNo) の対応と同じ
// (patch_type文字列 "OPLL"/"OPLLX"/"OPLLP"/"VRC7" が variantSel 0-3、
// patch_no が instIndex 1-15)。該当なしなら空文字列。
std::string opllRomVoiceName(const std::string& patchType, int patchNo);

// ─── 内蔵リズム音源(voice_patch_type == 0x70) ───────────────────────
//
// COPNARhythm(OPNA)/COPLLRhythm(OPLL)専用の解決経路
// (PatchManager::resolveBuiltinRhythm())。`patch_bank` はバンク番号では
// なく「対象チップのVoicePatchType」(OPN2=OPNA、OPLL=OPLL)、
// `patch_prog` はそのチップ内の楽器番号(=デバイスチャンネル番号)。
// OPL系内蔵リズム(COPLRhythm)はこの経路を使わず、VoicePatchType::OPL_RHY
// という通常のHwBankを使う点に注意(FITOM_X docs/terminology.md)。

struct BuiltinRhythmChip {
    VoicePatchType chipSel = VoicePatchType::None; // patch_bank に入る値
    std::string label;                             // "OPNA" / "OPLL"
};

// 選択可能な対象チップ一覧(固定2件)。
std::vector<BuiltinRhythmChip> builtinRhythmChips();

// chipSel(= patch_bank の値)の表示名。該当なしなら空文字列。
std::string builtinRhythmChipLabel(int chipSel);

// chipSelの内蔵リズムパート一覧(実機固定・チャンネル番号順)。
// OPNA=6パート、OPLL=5パート(DeviceFactory::defaultChCount()と一致)。
// 該当なしなら空。各エントリの `prog` はそのままパート番号。
std::vector<BuiltinVoiceEntry> builtinRhythmParts(int chipSel);

// chipSel/progからパート名を1件解決する。範囲外なら空文字列。
std::string builtinRhythmPartName(int chipSel, int prog);

} // namespace fpe
