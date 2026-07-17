#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "fpe/VoicePatchType.h"

// Sample-based ("SampleZonePatch") voice format used by the PCM/ADPCM
// device families (ADPCM-B/A, PCM-D8, AWM). Structurally unrelated to
// HwPatch - no FM operators, just keyzone -> waveform mapping.
// Source: docs/hwpatch-reference.md section 15, docs/voice-parameter-reference.md
// "OPL4 AWM" section.

namespace fpe {

struct SampleZone {
    uint8_t key_min = 0;
    uint8_t key_max = 127;
    uint8_t vel_min = 0;
    uint8_t vel_max = 127;
    int wave_index = 0;   // raw chip-side ROM/PCM-bank waveform index
    uint8_t root_note = 60; // reserved for future ADPCM use; unused by OPL4 AWM
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
