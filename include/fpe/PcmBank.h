#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fpe/VoicePatchType.h"

// PCM/ADPCM waveform bank (*.pcmbank.json), used by the ADPCM-B/ADPCM-A/
// PCM-D8/SSGS-ADPCM device families (VoicePatchType::ADPCMB/ADPCMA/
// PCMD8/SSGS_ADPCM). Structurally unrelated to both HwPatch (no ops[]/FM parameters)
// and SampleZonePatch (no key/velocity zones) - it is a table of named
// raw-sample entries keyed by entry_no. DrumKit/HwPatch references into this
// bank (patch_bank + patch_prog for these voice_patch_types) use entry_no
// directly as patch_prog (see docs/history/DESIGN.md D-013).
//
// The entries come from two places, merged the way FITOM_X's
// PatchManager::loadPcmBankJson() does (D-063): the adpcm_packer output named
// by `adpcm_json` (entry_no = position in its entries[]), then this file's
// own entries[] on top, replacing by entry_no. FITOM_X's own writer emits
// the second form.
//
// Referenced either directly from a profile's banks.hw_banks[] entry
// (group == one of the ADPCM/PCM-D8 names) or from the top-level
// banks.pcm_banks[] array; both point at the same *.pcmbank.json shape.
//
// Per the project owner, end users never edit this bank's *content*
// directly (only DrumKit entries reference it) - there is no CRUD API for
// entries here, unlike every other bank type. It does still participate in
// PatchWorkspace::save()/saveAs() so "save as" keeps producing a
// self-contained tree: saveAs() copies the referenced adpcm_json/bin_file
// sidecar files alongside the (re-serialized) pcmbank.json itself - see
// PatchWorkspace::rebaseSourceFiles().
//
// Source: config_schema/pcmbank.schema.json, docs/manuals/hwpatch-reference.md
// section 14 (wave_index "PCM波形バンクのエントリ番号"), real
// *.pcmbank.json + adpcm_packer-output examples in FITOM_staging.

namespace fpe {

// One sample. Same fields for both sources; only the JSON shape differs.
struct PcmBankEntry {
    int entry_no = 0;
    std::string name;
    uint32_t start_offset = 0;
    uint32_t end_offset = 0;
    uint32_t size = 0;
    uint32_t padded_size = 0;
    uint8_t root_note = 69; // MIDI note the sample was authored at (FITOM_X default A4)
};

// The pcmbank.json's own entries[] shape (pcmbank.schema.json): entry_no /
// name / start_offset / end_offset / size / padded_size. root_note is not in
// the schema but FITOM_X reads it, so it is kept, written only when not 69.
void to_json(nlohmann::json& j, const PcmBankEntry& v);
void from_json(const nlohmann::json& j, PcmBankEntry& v);

// The adpcm_packer output shape (name / offset / size / padded_size /
// root_note), whose entry number is its position. end_offset is derived the
// way FITOM_X does: offset + padded_size - 1.
PcmBankEntry pcmEntryFromAdpcmPacker(const nlohmann::json& j, int entryNo);

// pcmbank.schema.json swpatches[]: ties one entry to a performance patch.
struct PcmSwPatchRef {
    int entry_no = 0;
    int sw_bank = -1; // -1 = no reference
    int sw_prog = -1;
};

void to_json(nlohmann::json& j, const PcmSwPatchRef& v);
void from_json(const nlohmann::json& j, PcmSwPatchRef& v);

struct PcmBank {
    std::string name;
    std::string codec;
    uint32_t sample_rate = 0;
    uint32_t boundary = 256;
    std::string bin_file;   // relative path to the raw sample data; informational only for this editor
    std::string adpcm_json; // relative path to a separate adpcm_packer-output JSON holding entries[]
    std::vector<PcmBankEntry> ownEntries; // this file's own entries[], exactly as on disk
    std::vector<PcmSwPatchRef> swpatches;
    nlohmann::json extra = nlohmann::json::object(); // unmodeled top-level keys, written back as-is

    // --- runtime (not part of the on-disk JSON shape) ---
    // The effective table, sorted by entry_no: adpcm_json's entries with
    // ownEntries applied on top. Filled by PatchWorkspace (it has to open
    // adpcm_json); from_json alone leaves it equal to ownEntries.
    std::vector<PcmBankEntry> entries;
    VoicePatchType voicePatchType = VoicePatchType::None;
    int bankIndex = 0;
    std::filesystem::path sourceFile;

    const PcmBankEntry* findByEntryNo(int entryNo) const;
};

// to_json writes `entries` from ownEntries (never the adpcm_json-derived
// ones, which belong to that other file), and only when there are some or
// when there is no adpcm_json to stand in for them.
void to_json(nlohmann::json& j, const PcmBank& v);
void from_json(const nlohmann::json& j, PcmBank& v);

} // namespace fpe
