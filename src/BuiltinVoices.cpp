#include "fpe/BuiltinVoices.h"

#include <optional>

namespace fpe {

namespace {

// ROM音色名。FITOM_X core/src/PatchManager.cpp の initOpllRomPatches() の
// kNames[4][16] をそのまま移植したもの(出典もそちらのコメント通り:
// https://github.com/plgDavid/misc/wiki/Copyright-free-OPLL(x)-ROM-patches
// ―― 耳コピによる非公式な近似データで、正確な公式名称ではない可能性が
// ある点に留意)。添字0はINSTナンバー0(ユーザー音色、このバンクでは
// 常に無音)用のダミーで未使用、1-15が実際のROM音色に対応する。
const char* const kOpllRomNames[4][16] = {
    // 0: OPLL (YM2413) / OPLL2
    {"", "Violin", "Guitar", "Piano", "Flute", "Clarinet", "Oboe",
     "Trumpet", "Organ", "Horn", "Synthesizer", "Harpsichord",
     "Vibraphone", "Synthesizer Bass", "Acoustic Bass", "Electric Guitar"},
    // 1: OPLLX (YM2423)
    {"", "Strings", "Guitar", "Electric Guitar", "Electric Piano 2",
     "Flute", "Marimba", "Trumpet", "Harmonica", "Tuba",
     "Synth Brass 2", "Short Saw", "Vibraphone", "Electric Guitar 2",
     "Synth Bass 2", "Sitar"},
    // 2: OPLLP (YMF281)
    {"", "Electric Strings", "Bow Wow", "Electric Guitar", "Organ", "Clarinet",
     "Saxophone", "Trumpet", "Street Organ", "Synth Brass", "Electric Piano",
     "Bass", "Vibraphone", "Chime", "Tom Tom 2", "Noise and Tone"},
    // 3: VRC7
    {"", "Buzzy Bell", "Guitar", "Wurly", "Flute", "Clarinet", "Synth",
     "Trumpet", "Organ", "Bells", "Vibes", "Vibraphone", "Tutti",
     "Fretless", "Synth Bass", "Sweep"},
};

// variantSel(0-3) -> VoicePatchType。PatchManager::resolveOpllRomVoice() の
// kVariantMap と同じ(4-7は未定義)。
constexpr VoicePatchType kOpllRomVariants[4] = {
    VoicePatchType::OPLL,  // 0 (OPLL2もVoicePatchType::OPLLを共有)
    VoicePatchType::OPLLX, // 1
    VoicePatchType::OPLLP, // 2
    VoicePatchType::VRC7,  // 3
};

// 各チップの内蔵リズムパート名(実機固定、prog=パート番号=デバイス
// チャンネル番号の順序に対応)。FITOM_X gui/bridge/FITOMBridge.cpp の
// kOpnaRhythmNames / kOpllRhythmNames をそのまま移植。
const char* const kOpnaRhythmNames[] = {
    "Bass Drum", "Snare Drum", "Top Cymbal", "Hi-Hat", "Tom", "Rim Shot"
};
const char* const kOpllRhythmNames[] = {
    "Hi-Hat", "Top Cymbal", "Tom", "Snare Drum", "Bass Drum"
};

} // namespace

const char* opllRomBankName() { return "ROM Preset"; }

int opllRomVariantSel(VoicePatchType type) {
    for (int i = 0; i < 4; ++i) {
        if (kOpllRomVariants[i] == type) return i;
    }
    return -1;
}

bool isOpllRomVoiceRef(VoicePatchType type, int bank) {
    return bank == 0 && opllRomVariantSel(type) >= 0;
}

std::vector<BuiltinVoiceEntry> opllRomVoices(VoicePatchType type) {
    std::vector<BuiltinVoiceEntry> result;
    const int variantSel = opllRomVariantSel(type);
    if (variantSel < 0) return result;
    result.reserve(15);
    for (int instIndex = 1; instIndex < 16; ++instIndex) {
        BuiltinVoiceEntry e;
        // 配列添字ではなく (variantSel << 4) | instIndex を書き込む
        // (BuiltinVoices.h の opllRomVoices() のコメント参照)。
        e.prog = (variantSel << 4) | instIndex;
        e.name = kOpllRomNames[variantSel][instIndex];
        result.push_back(std::move(e));
    }
    return result;
}

OpllRomVoiceRef opllRomVoiceByProg(int hwProg) {
    OpllRomVoiceRef ref;
    if (hwProg < 0 || hwProg > 0xFF) return ref;
    const int variantSel = (hwProg >> 4) & 0x7;
    const int instIndex = hwProg & 0xF;
    if (variantSel >= 4 || instIndex == 0) return ref; // 未定義変種 / 無音予約
    ref.valid = true;
    ref.variant = kOpllRomVariants[variantSel];
    ref.instIndex = instIndex;
    ref.name = kOpllRomNames[variantSel][instIndex];
    return ref;
}

std::string opllRomVoiceName(const std::string& patchType, int patchNo) {
    const std::optional<VoicePatchType> type = stringToVoicePatchType(patchType);
    if (!type) return std::string();
    const int variantSel = opllRomVariantSel(*type);
    if (variantSel < 0 || patchNo < 1 || patchNo > 15) return std::string();
    return kOpllRomNames[variantSel][patchNo];
}

std::vector<BuiltinRhythmChip> builtinRhythmChips() {
    return {
        {VoicePatchType::OPN2, "OPNA"},
        {VoicePatchType::OPLL, "OPLL"},
    };
}

std::string builtinRhythmChipLabel(int chipSel) {
    for (const auto& c : builtinRhythmChips()) {
        if (static_cast<int>(c.chipSel) == chipSel) return c.label;
    }
    return std::string();
}

std::vector<BuiltinVoiceEntry> builtinRhythmParts(int chipSel) {
    const char* const* names = nullptr;
    size_t count = 0;
    if (chipSel == static_cast<int>(VoicePatchType::OPN2)) {
        names = kOpnaRhythmNames;
        count = sizeof(kOpnaRhythmNames) / sizeof(kOpnaRhythmNames[0]);
    } else if (chipSel == static_cast<int>(VoicePatchType::OPLL)) {
        names = kOpllRhythmNames;
        count = sizeof(kOpllRhythmNames) / sizeof(kOpllRhythmNames[0]);
    }
    std::vector<BuiltinVoiceEntry> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        BuiltinVoiceEntry e;
        e.prog = static_cast<int>(i);
        e.name = names[i];
        result.push_back(std::move(e));
    }
    return result;
}

std::string builtinRhythmPartName(int chipSel, int prog) {
    const auto parts = builtinRhythmParts(chipSel);
    if (prog < 0 || static_cast<size_t>(prog) >= parts.size()) return std::string();
    return parts[static_cast<size_t>(prog)].name;
}

} // namespace fpe
