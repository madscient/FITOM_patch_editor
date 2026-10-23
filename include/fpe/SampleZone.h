#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fpe/VoicePatchType.h"

// Sample-based ("SampleZonePatch") voice format used by AWM only - the
// ADPCM-B/A, PCM-D8 and SSGS-ADPCM families use fpe::PcmBank instead
// (D-013). Structurally unrelated to HwPatch - no FM operators, just
// keyzone -> waveform mapping.
// Source: docs/manuals/hwpatch-reference.md section 14, docs/design/chip-notes.md
// "OPL4 AWM" section.

namespace fpe {

// Field set and defaults per FITOM_X config_schema/samplezonebank.schema.json
// (zones[] has additionalProperties:false, so this is exhaustive).
struct SampleZone {
    uint8_t key_min = 0;
    uint8_t key_max = 127;
    uint8_t vel_min = 0;
    uint8_t vel_max = 127;
    int wave_index = 0;   // raw chip-side ROM/PCM-bank waveform index
    // Per-waveform AWM calibration (ALSA yrw801.c's opl4_sound conventions).
    // Without these the ROM waveforms play at the wrong pitch/level.
    int16_t pitch_offset = 0;   // 100/128-cent units
    uint16_t key_scaling = 100; // % note tracking; 0 = fixed pitch
    uint8_t tone_attenuate = 0;
    uint8_t volume_factor = 254; // 254 = no scaling
    uint8_t root_note = 69;      // unused by AWM. The schema's default is 69, explicitly not 60
    std::string name;            // zone label; for a drum kit, each zone is one drum sound
    int sw_bank = -1;            // -1 = no performance-patch reference
    int sw_prog = -1;
};

void to_json(nlohmann::json& j, const SampleZone& v);
void from_json(const nlohmann::json& j, SampleZone& v);

struct SampleZonePatch {
    int prog = 0;
    std::string name;
    std::vector<SampleZone> zones; // searched in order; first matching zone wins
};

void to_json(nlohmann::json& j, const SampleZonePatch& v);
void from_json(const nlohmann::json& j, SampleZonePatch& v);

// A sample-zone bank (*.samplezonebank.json), registered the same way as
// a HwBank (profile.json hw_banks[] entry) but selected instead of a
// HwBank whenever the entry's VoicePatchType isSampleBasedVoicePatchType().
struct SampleZoneBank {
    std::string name;
    std::vector<SampleZonePatch> patches;
    nlohmann::json extra = nlohmann::json::object(); // unmodeled top-level keys ("_comment", ...), written back as-is

    // runtime metadata (not part of the on-disk JSON shape)
    VoicePatchType voicePatchType = VoicePatchType::None;
    int bankIndex = 0;
    std::filesystem::path sourceFile;

    SampleZonePatch* findByProg(int prog);
    const SampleZonePatch* findByProg(int prog) const;
};

void to_json(nlohmann::json& j, const SampleZoneBank& v);
void from_json(const nlohmann::json& j, SampleZoneBank& v);

} // namespace fpe
