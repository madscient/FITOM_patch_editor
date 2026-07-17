#pragma once
#include <cstdint>
#include <optional>
#include <string>

// FITOM_X patch editor - data model layer.
// See ../../README.md for scope, source references and known assumptions.

namespace fpe {

// VoicePatchType: FITOM_X's device/chip-family classification.
// It doubles as the CC#0 "direct device select" value (MIDI side) and as
// the per-bank tag stored on every HwBank (profile.json hw_banks[].group).
//
// Source: docs/patch-structure-design.md ("VoicePatchType 一覧") and
// docs/midi-message-reference.md ("直接デバイス選択値(CC#0)一覧").
// 0x38-0x3b (MA3 family) are reserved values with no implemented chip
// driver as of the source docs; included here only as named placeholders.
enum class VoicePatchType : uint8_t {
    None         = 0x00, // "normal mode" trigger (CC#0=0). Never a real HwBank tag.
    OPN          = 0x10,
    OPN2         = 0x11,
    OPM          = 0x19,
    OPZ          = 0x1a,
    OPZ2         = 0x1b,
    OPL          = 0x20,
    OPL2         = 0x21,
    OPL3_2       = 0x22,
    OPL_RHY      = 0x23, // OPL built-in rhythm channel voices (docs/terminology.md)
    OPLL         = 0x28,
    OPLLP        = 0x29,
    OPLLX        = 0x2a,
    VRC7         = 0x2b,
    OPL3         = 0x30,
    SD1          = 0x38, // reserved, unimplemented in source docs
    MA3          = 0x39, // reserved, unimplemented in source docs
    MA5          = 0x3a, // reserved, unimplemented in source docs
    MA7          = 0x3b, // reserved, unimplemented in source docs
    SSG          = 0x40,
    EPSG         = 0x41,
    DCSG         = 0x42,
    SAA          = 0x43,
    SCC          = 0x48,
    ADPCMB_Y8950 = 0x50,
    ADPCMB       = 0x51,
    ADPCMA       = 0x52,
    PCMD8        = 0x53,
    AWM          = 0x54,
    // 0x70            : built-in rhythm bank access value (CC#0), not a HwBank tag.
    // 0x78 / 0x79     : GM2 channel-role switch (CC#0), not a HwBank tag.
    // 0x71-0x77 / 0x7a-0x7f: reserved for future use.
    BuiltinRhythmBankSelector = 0x70,
    Gm2RhythmChannelSelect    = 0x78,
    Gm2MelodyChannelSelect    = 0x79,
};

// True for the sample-based (SampleZonePatch) families: ADPCM-B/A, PCM-D8, AWM.
// These use a completely different on-disk patch shape (see SampleZone.h)
// instead of the FM-operator shaped HwPatch.
bool isSampleBasedVoicePatchType(VoicePatchType t);

// True for values that can legitimately tag a HwBank (i.e. excludes None
// and the CC#0-only special values 0x70/0x78/0x79 and reserved gaps).
bool isValidHwBankTag(VoicePatchType t);

// Converts a profile.json `hw_banks[].group` string (e.g. "OPZ") to the
// matching VoicePatchType. Returns std::nullopt for unrecognized names
// (the caller should treat that as a load warning, not a hard failure,
// consistent with FITOM_X's general "soft failure" philosophy).
std::optional<VoicePatchType> stringToVoicePatchType(const std::string& group);

// Inverse of stringToVoicePatchType(). Returns "?" for values with no
// canonical group name (None, reserved specials).
std::string voicePatchTypeToString(VoicePatchType t);

} // namespace fpe
