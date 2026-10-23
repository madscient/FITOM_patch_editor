#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// SCC/SCC+ waveform-table bank (*.sccwave.json), registered by a profile's
// banks.scc_wave_banks[]. An SCC HwPatch selects one of these waveforms with
// hwOp[0].WS = wave_no.
// Source: FITOM_X config_schema/sccwave.schema.json and
// PatchManager::loadSccWaveBankJson().

namespace fpe {

struct SccWave {
    int wave_no = 0;
    std::string name;
    // 32 signed samples in the schema (FITOM_X clamps each to -128..127 and
    // ignores anything past 32). Held as read, so a save writes back exactly
    // what was there.
    std::vector<int> data;
    // The schema also allows `data` as 64 hex digits (two's complement bytes).
    // When the file used that form, this holds the original string: it is
    // written back verbatim while `data` still encodes to it, and re-encoded
    // (upper case) once `data` changes.
    std::string hexData;
};

void to_json(nlohmann::json& j, const SccWave& v);
void from_json(const nlohmann::json& j, SccWave& v);

struct SccWaveBank {
    std::string name;
    std::vector<SccWave> waves;
    nlohmann::json extra = nlohmann::json::object(); // unmodeled top-level keys, written back as-is

    // runtime metadata (not part of the on-disk JSON shape)
    int bankIndex = 0;
    std::filesystem::path sourceFile;

    SccWave* findByWaveNo(int waveNo);
    const SccWave* findByWaveNo(int waveNo) const;
};

void to_json(nlohmann::json& j, const SccWaveBank& v);
void from_json(const nlohmann::json& j, SccWaveBank& v);

} // namespace fpe
