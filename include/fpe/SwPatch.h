#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

// SwPatch ("performance patch"): software-side expression parameters
// (vibrato, tremolo, velocity sensitivity, fine transpose) that apply
// regardless of which chip is sounding the note.
// Source: docs/swpatch-reference.md.

namespace fpe {

// Channel-level vibrato (pitch LFO) settings.
struct FmSwVoice {
    uint8_t LWF = 0;  // Waveform: 0=up-saw/1=square/2=triangle/3=S&H/4=down-saw/5=delta/6=sine
    uint8_t LFS = 0;  // 0/1: reset phase on note-on
    uint8_t LFM = 0;  // Mode: 0=loop/1=one-shot hold/2=one-shot to zero
    uint8_t LFD = 0;  // Delay (20ms steps)
    uint8_t LFR = 0;  // Rate. 0 = disabled (falls back to CC#1-driven vibrato depth)
    uint8_t LFI = 0;  // Fade-in speed
    int16_t depth_cents = 0; // -1200..+1200
};

// Per-operator velocity sensitivity + tremolo (amplitude LFO) settings.
// A SwPatch always carries exactly 4 of these (ops[0..3] map to the
// corresponding HwPatch operator, regardless of how many the chip uses).
struct FmSwOp {
    // Velocity sensitivity (applies to carrier operators only)
    uint8_t VTL = 0; // vel -> TL (volume)
    uint8_t VAR = 0; // vel -> Attack Rate
    uint8_t VDR = 0; // vel -> Decay Rate
    uint8_t VSL = 0; // vel -> Sustain Level
    uint8_t VSR = 0; // vel -> Sustain Rate
    uint8_t VRR = 0; // vel -> Release Rate
    uint8_t VLD = 0; // reserved, currently unused
    uint8_t VLR = 0; // reserved, currently unused

    // Tremolo (per-operator amplitude LFO)
    uint8_t SLW = 0; // waveform (same choices as LWF)
    uint8_t SLS = 0; // 0/1: reset phase on note-on
    uint8_t SLM = 0; // mode (same choices as LFM)
    uint8_t SLD = 0; // depth: 0-63 positive, 64-127 negative (-64..-1)
    uint8_t SLY = 0; // delay (20ms steps)
    uint8_t SLR = 0; // rate (same semantics as LFR)
    uint8_t SLI = 0; // fade-in speed
};

struct SwPatch {
    int prog = 0;
    std::string name;
    FmSwVoice sw;
    std::array<FmSwOp, 4> ops{}; // always 4 elements per docs/swpatch-reference.md
    int16_t fine_transpose = 0;  // -1200..+1200 cents, adds to ToneLayer::transpose
};

void to_json(nlohmann::json& j, const FmSwVoice& v);
void from_json(const nlohmann::json& j, FmSwVoice& v);
void to_json(nlohmann::json& j, const FmSwOp& v);
void from_json(const nlohmann::json& j, FmSwOp& v);
void to_json(nlohmann::json& j, const SwPatch& v);
void from_json(const nlohmann::json& j, SwPatch& v);

// A performance-patch bank (*.swbank.json). Unlike HwBank, SwBank has no
// VoicePatchType tag - it's a single flat number space shared by all chip
// families (docs/midi-implementation-status.md).
struct SwBank {
    std::string name;
    std::vector<SwPatch> patches;

    // runtime metadata (not part of the on-disk JSON shape)
    int bankIndex = 0;
    std::filesystem::path sourceFile;

    SwPatch* findByProg(int prog);
    const SwPatch* findByProg(int prog) const;
};

void to_json(nlohmann::json& j, const SwBank& v);
void from_json(const nlohmann::json& j, SwBank& v);

} // namespace fpe
