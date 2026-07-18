#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "fpe/VoicePatchType.h"

// HwPatch: the raw FM/PSG synthesis parameters written directly to chip
// registers (feedback, algorithm, per-operator envelope, etc).
// Source: docs/hwpatch-reference.md, docs/voice-parameter-reference.md,
// docs/patch-structure-design.md.

namespace fpe {

// Channel-level (once per HwPatch) synthesis parameters.
// Field meaning varies per chip - see docs/hwpatch-reference.md sections
// 2-13 for which fields a given chip actually reads.
struct FmHwVoice {
    uint8_t FB  = 0; // Feedback (0-7 typical)
    uint8_t ALG = 0; // Algorithm / connection (meaning is chip-specific)
    uint8_t AMS = 0; // AM sensitivity (OPM/OPZ)
    uint8_t PMS = 0; // PM sensitivity (OPM/OPZ)
    uint8_t NFQ = 0; // Noise frequency (OPM ch7 / PSG family)
    uint8_t FB2 = 0; // Second feedback (OPL3 4-op mode, second operator pair)
};

// Per-operator synthesis parameters. Up to 4 elements per HwPatch,
// depending on the chip's operator count (1=PSG family, 2=OPL/OPLL family,
// 4=OPN/OPM/OPZ/OPL3-4op family). Elements beyond what a chip uses are
// simply ignored.
struct FmHwOp {
    uint8_t AR  = 0;  // Attack Rate
    uint8_t DR  = 0;  // Decay Rate
    uint8_t SL  = 0;  // Sustain Level
    uint8_t SR  = 0;  // Sustain Rate (0 = sustain/ADSR mode, >0 = percussive mode)
    uint8_t RR  = 0;  // Release Rate
    uint8_t TL  = 0;  // Total Level
    uint8_t KSR = 0;  // Key Scale Rate
    uint8_t KSL = 0;  // Key Scale Level (OPL family)
    uint8_t MUL = 0;  // Multiple / frequency ratio
    uint8_t DT1 = 0;  // Detune 1
    uint8_t DT2 = 0;  // Detune 2 (OPM: 2 bits)
    int16_t PDT = 0;  // Pseudo DeTune: OPN FX-mode / OPL3 4op pseudo-detune offset (100/64 cent units, or 0.1Hz in OPN fixed-freq mode). JSON key "PDT" per FITOM_X's config_schema/hwbank.schema.json (D-028 - was serialized as "FXV" before, silently losing real non-zero data on every load of a file using this feature)
    uint8_t AM  = 0;  // AM enable
    uint8_t VIB = 0;  // Vibrato enable (hardware LFO routing)
    uint8_t EGT = 0;  // Envelope type / SSG-EG type, meaning is chip-specific
    uint8_t WS  = 0;  // Waveform select
    uint8_t REV = 0;  // Reverb (OPZ only)
    uint8_t EGS = 0;  // EG bias (OPZ only)
    uint8_t DT3 = 0;  // Auxiliary detune (OPZ ratio mode only)
};

// Chip-specific extension fields that don't fit the common FmHwVoice/FmHwOp
// shape. Most fields apply to exactly one chip family; see
// docs/hwpatch-reference.md for which.
struct FmChipExt {
    uint8_t  FIX = 0;      // OPN FX-mode select: 0=off/1=pseudo-detune/2=non-integer ratio/3=fixed freq. JSON key "FIX" per FITOM_X's config_schema/hwbank.schema.json (D-028 - was serialized as "DM0" before, a stale internal name that never matched the schema)
    uint8_t  ALG_EXT = 0;  // Meaning varies: OPM noise enable(bit0) / OPL3 4-op link / OPLL preset flag(bit0)
    uint16_t HWEP = 0;     // Hardware envelope period (SSG/EPSG), or SAA1099 HW envelope bit-packed fields
    uint8_t  rhythm_ch = 255;                          // 255 = unset. Target channel for OPL_RHY (docs/terminology.md).
    VoicePatchType target_voice_patch_type = VoicePatchType::None; // Shared-PSG-bank device resolution override (docs/patch-structure-design.md "PSG系共有バンク")
};

// Reference into the OPLL-family "builtin ROM voice" performance-patch
// metadata bank (profile.json hw_banks[] entry with role=="builtin_swpatch_meta").
// See docs/patch-structure-design.md "OPLL ROM音色へのパフォーマンスパッチ紐づけ".
struct BuiltinRef {
    std::string patch_type; // e.g. "OPLL" / "OPLLX" / "OPLLP" / "VRC7"
    int patch_no = 0;       // ROM preset index (1-15), 0 = silence per docs/hwpatch-reference.md
};

// One programmable device voice ("device voice patch" / hardware patch).
struct HwPatch {
    int prog = 0;
    std::string name;

    FmHwVoice hw;
    std::vector<FmHwOp> ops; // 1-4 elements depending on chip; may be empty when `builtin` is set

    FmChipExt ext;

    // -1 = no performance-patch reference (docs/hwpatch-reference.md "16").
    int sw_bank = -1;
    int sw_prog = -1;

    // Set only for entries in a role=="builtin_swpatch_meta" bank; mutually
    // exclusive with `ops` per docs/patch-structure-design.md.
    std::optional<BuiltinRef> builtin;

    bool isBuiltinRef() const { return builtin.has_value(); }
};

void to_json(nlohmann::json& j, const FmHwVoice& v);
void from_json(const nlohmann::json& j, FmHwVoice& v);
void to_json(nlohmann::json& j, const FmHwOp& v);
void from_json(const nlohmann::json& j, FmHwOp& v);
void to_json(nlohmann::json& j, const FmChipExt& v);
void from_json(const nlohmann::json& j, FmChipExt& v);
void to_json(nlohmann::json& j, const BuiltinRef& v);
void from_json(const nlohmann::json& j, BuiltinRef& v);
void to_json(nlohmann::json& j, const HwPatch& v);
void from_json(const nlohmann::json& j, HwPatch& v);

// A device patch bank (*.hwbank.json). All patches in a bank share one
// VoicePatchType tag, which is NOT stored in the bank file itself - it is
// assigned by the profile's hw_banks[].group entry at load time
// (docs/patch-structure-design.md "HwBank 側のタグ付けルール").
struct HwBank {
    std::string name;
    std::vector<HwPatch> patches;

    // --- runtime metadata, populated by PatchWorkspace, NOT part of the
    // *.hwbank.json file's own JSON shape ---
    VoicePatchType voicePatchType = VoicePatchType::None;
    int bankIndex = 0;               // profile.json hw_banks[].bank
    std::string role;                // profile.json hw_banks[].role, e.g. "builtin_swpatch_meta"
    std::filesystem::path sourceFile;

    HwPatch* findByProg(int prog);
    const HwPatch* findByProg(int prog) const;

    // Linear search used by the OPLL ROM builtin-metadata bank.
    const HwPatch* findByBuiltinRef(const std::string& patchType, int patchNo) const;
};

// Serializes/deserializes only the on-disk shape (name + patches). Runtime
// metadata fields are intentionally not part of this JSON shape.
void to_json(nlohmann::json& j, const HwBank& v);
void from_json(const nlohmann::json& j, HwBank& v);

} // namespace fpe
