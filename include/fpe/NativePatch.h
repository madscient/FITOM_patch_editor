#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "fpe/VoicePatchType.h"

// Native patch: the "normal mode" (CC#0=0) multi-layer patch format.
// Source: docs/native-patch-reference.md, docs/patch-structure-design.md.

namespace fpe {

// One sounding layer within a native Patch. Up to 4 layers (indices 0-3,
// the user-facing "ToneLayer 0..3") may be stacked; overlapping note
// ranges sound simultaneously (layered, not exclusive).
struct ToneLayer {
    VoicePatchType voice_patch_type = VoicePatchType::None; // which chip family this layer targets
    int hw_bank = 0; // HwBank index (profile.json hw_banks[].bank) for this voice_patch_type
    int hw_prog = 0; // HwPatch program number within that bank

    uint8_t note_range_lo = 0;
    uint8_t note_range_hi = 127;

    int8_t transpose = 0;      // semitones, -48..+48
    int8_t volume_offset = 0;  // -64..+63
    int8_t pan_offset = 0;     // -64..+63 (-64=left, 0=center, +63=right)

    bool enabled = true;
};

void to_json(nlohmann::json& j, const ToneLayer& v);
void from_json(const nlohmann::json& j, ToneLayer& v);

// One native patch ("program"), 1-4 ToneLayers.
struct Patch {
    int prog = 0;
    std::string name;
    int poly = 0; // 0 = auto (derived from the device channel count)

    // -1 = no performance-patch reference (falls back to per-HwPatch sw_bank/sw_prog).
    int sw_bank = -1;
    int sw_prog = -1;

    std::vector<ToneLayer> layers; // index 0 = primary layer ("ToneLayer 0")
};

void to_json(nlohmann::json& j, const Patch& v);
void from_json(const nlohmann::json& j, Patch& v);

// A native patch bank (*.patchbank.json), selected via CC#32 in normal mode.
struct PatchBank {
    std::string name;
    std::vector<Patch> patches;

    // runtime metadata (not part of the on-disk JSON shape)
    int bankIndex = 0;
    std::filesystem::path sourceFile;

    Patch* findByProg(int prog);
    const Patch* findByProg(int prog) const;
};

void to_json(nlohmann::json& j, const PatchBank& v);
void from_json(const nlohmann::json& j, PatchBank& v);

} // namespace fpe
