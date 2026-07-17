#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "fpe/VoicePatchType.h"

// Drum kit ("DrumPatch" in FITOM_X source): one rhythm-channel program,
// mapping MIDI notes to instrument voices.
// Source: docs/patch-structure-design.md ("リズムチャンネル: ドラムマップ
// による解決", "チョークグループ"), docs/terminology.md ("ドラムキット").
//
// CONFIRMED (2026-07-17) against the real
// config_schema/drumkit.schema.json: the original guessed field names below
// (note/name/voice_patch_type/patch_bank/patch_prog/play_note/sw_bank/
// sw_prog) were all correct, but the schema additionally has per-note
// fine_tune/pan/gate_time (routed kits) and, for "direct" kits,
// voice_patch_type/sw_bank/sw_prog/fine_tune/pan/gate_time as single
// whole-kit values - none of which were modeled before this pass.

namespace fpe {

// One routed-kit note entry. `note` is the MIDI note number that triggers
// this instrument; `voice_patch_type`/`patch_bank`/`patch_prog` select the
// voice using the exact same normal-mode-vs-direct-mode semantics as CC#0
// (voice_patch_type==None means "normal mode": patch_bank/patch_prog index
// a PatchBank/Patch; otherwise they index a HwBank/HwProg directly).
struct DrumNote {
    uint8_t note = 0;           // MIDI trigger note (0-127)
    std::string name;           // instrument display name, e.g. "Acoustic Snare"

    VoicePatchType voice_patch_type = VoicePatchType::None;
    int patch_bank = 0;
    int patch_prog = 0;

    uint8_t play_note = 60;     // actual pitch sounded (absolute MIDI note)

    // kfs units, 1 semitone = 64 steps (see docs/terminology.md "kfs") - not cents.
    int fine_tune = 0;
    int pan = 0;                // pan offset, added to the resolved ToneLayer's pan_offset
    int gate_time = 0;          // timer ticks; 0 = stop on NoteOff

    // Per-note performance-patch override; -1 = not set (falls back to the
    // resolved HwPatch's own sw_bank/sw_prog).
    int sw_bank = -1;
    int sw_prog = -1;
};

void to_json(nlohmann::json& j, const DrumNote& v);
void from_json(const nlohmann::json& j, DrumNote& v);

enum class DrumKitType { Routed, Direct };

// One drum kit / rhythm-channel program (*.drumkit.json). Loaded per the
// profile's drum_banks[] registry entry (see Profile.h).
struct DrumKit {
    DrumKitType type = DrumKitType::Routed;
    std::string name;

    // --- "routed" fields ---
    std::vector<DrumNote> notes;
    std::vector<std::vector<uint8_t>> choke_groups; // groups of MIDI notes that mutually cut each other off

    // --- "direct" fields --- (single whole-kit values, applied to every
    // synthesized note - see effectiveNotes())
    int patch_bank = 0;
    int patch_prog = 0;
    uint8_t note_min = 0;
    uint8_t note_max = 127;
    VoicePatchType voice_patch_type = VoicePatchType::None;
    int sw_bank = -1;
    int sw_prog = -1;
    int fine_tune = 0;
    int pan = 0;
    int gate_time = 0;

    // --- runtime metadata (not part of the on-disk JSON shape) ---
    int prog = 0;              // profile.json drum_banks[].prog (Program Change value that selects this kit)
    std::filesystem::path sourceFile;

    // Convenience accessor: returns notes[] as-is for "routed" kits, or a
    // synthesized (derived, not independently editable) list for "direct"
    // kits by expanding [note_min, note_max] with play_note == note.
    // Useful so a UI can browse "ドラムノート" uniformly regardless of type.
    std::vector<DrumNote> effectiveNotes() const;

    DrumNote* findNote(uint8_t note); // routed kits only
};

void to_json(nlohmann::json& j, const DrumKit& v);
void from_json(const nlohmann::json& j, DrumKit& v);

} // namespace fpe
