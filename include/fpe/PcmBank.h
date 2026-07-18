#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "fpe/VoicePatchType.h"

// PCM/ADPCM waveform bank (*.pcmbank.json), used by the ADPCM-B/ADPCM-A/
// PCM-D8 device families (VoicePatchType::ADPCMB_Y8950/ADPCMB/ADPCMA/
// PCMD8). Structurally unrelated to both HwPatch (no ops[]/FM parameters)
// and SampleZonePatch (no key/velocity zones) - it is just a flat, ordered
// list of named raw-sample entries produced by the external adpcm_packer
// tool, referenced purely by 0-based array index (no "prog" field of its
// own). DrumKit/HwPatch references into this bank (patch_bank + patch_prog
// for these voice_patch_types) use that index directly as patch_prog -
// confirmed against real FITOM_staging data (see docs/DESIGN.md D-013).
//
// Referenced either directly from a profile's banks.hw_banks[] entry
// (group == one of the four ADPCM/PCM-D8 names) or from the top-level
// banks.pcm_banks[] array; both point at the same *.pcmbank.json shape.
//
// Per the project owner, end users never edit this bank's *content*
// directly (only DrumKit entries reference it by index) - there is no CRUD
// API for entries[] here, unlike every other bank type. It does still
// participate in PatchWorkspace::save()/saveAs() so "save as" keeps
// producing a self-contained tree: saveAs() copies the referenced
// adpcm_json/bin_file sidecar files alongside the (re-serialized)
// pcmbank.json itself - see PatchWorkspace::rebaseSourceFiles().
//
// Source: docs/hwpatch-reference.md section 14 (ops[0].WS "PCM波形バンク内
// のエントリ番号"), real *.pcmbank.json + adpcm_packer-output examples in
// FITOM_staging, and FITOM_X's PatchManager::loadPcmBankJson() (entries[]
// embedded directly in the pcmbank.json take precedence over following the
// adpcm_json reference - PatchWorkspace::loadBanks() mirrors that).

namespace fpe {

struct PcmBankEntry {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t padded_size = 0;
    uint8_t root_note = 69; // adpcm_packer default (A4); MIDI note the sample was authored at
};

void to_json(nlohmann::json& j, const PcmBankEntry& v);
void from_json(const nlohmann::json& j, PcmBankEntry& v);

// Reflects only what a single *.pcmbank.json file's own JSON directly
// contains. `entries` here may be empty even for a bank that does have
// entries - see the file-level comment above; PatchWorkspace::loadBanks()
// is what follows `adpcm_json` to fill entries in when needed.
struct PcmBank {
    std::string name;
    std::string codec;
    uint32_t sample_rate = 0;
    uint32_t boundary = 256;
    std::string bin_file;   // relative path to the raw sample data; informational only for this editor
    std::string adpcm_json; // relative path to a separate adpcm_packer-output JSON holding entries[]
    std::vector<PcmBankEntry> entries;

    // runtime metadata (not part of the on-disk JSON shape)
    VoicePatchType voicePatchType = VoicePatchType::None;
    int bankIndex = 0;
    std::filesystem::path sourceFile;

    const PcmBankEntry* findByIndex(size_t index) const;
};

// from_json reads only this file's own directly-visible fields (no
// adpcm_json following - that requires opening a second file, so it lives
// in PatchWorkspace.cpp). to_json writes `entries` only when `adpcm_json`
// is empty (i.e. entries were embedded inline to begin with); otherwise it
// omits `entries` and keeps the `adpcm_json` reference, matching the
// on-disk shape entries were actually read from.
void to_json(nlohmann::json& j, const PcmBank& v);
void from_json(const nlohmann::json& j, PcmBank& v);

} // namespace fpe
