#pragma once
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// Top-level *.profile.json. Source: docs/config-design.md
// ("*.profile.json — プロファイル"), docs/patch-structure-design.md
// (hw_banks[]/drum_banks[] examples).
//
// CONFIRMED (2026-07-17) against the real FITOM_X
// config_schema/profile.schema.json and a production
// unified_preset.profile.json: `patch_banks[]`/`sw_banks[]` were guessed
// correctly by name, but all six bank-registry arrays
// (hw_banks/patch_banks/sw_banks/drum_banks/scc_wave_banks/pcm_banks) live
// nested under a top-level `"banks": { ... }` object, not at the profile's
// top level as originally assumed. `scc_wave_banks[]`/`pcm_banks[]` are new
// (not previously modeled at all); their referenced file formats
// (*.sccwave.json / *.pcmbank.json) are still out of scope for this
// library (refs are preserved for round-tripping, but PatchWorkspace does
// not load their content - see docs/STATUS.md).
//
// CONFIRMED (2026-08-01) against FITOM_staging (`unified.bankset.json` /
// `emu_opl.profile.json`, added 2026-07-29) and the current
// config_schema/profile.schema.json: `"banks"` can now be either the inline
// object described above, OR a plain string naming an external JSON file
// (relative to this profile's own directory) holding that same object -
// letting several profiles share one bank registry. A second top-level key,
// `"bank_overrides"` (same string-or-object shape), lets a profile patch
// individual entries of that shared registry (matched by an array-specific
// identity key - see BanksObject/mergeBanksObjects()) without duplicating
// the whole thing. See docs/DESIGN.md D-041 for why this is modeled as
// BanksSource here (deferring the string case's file I/O to
// PatchWorkspace) rather than resolved inline during from_json.

namespace fpe {

// profile.json hw_banks[] entry. `group` names the VoicePatchType (or
// "AWM"-style sample-based family) via VoicePatchType::stringToVoicePatchType.
// `role` is optional; the only documented value is "builtin_swpatch_meta"
// (OPLL ROM-voice performance-patch metadata bank).
struct HwBankRef {
    std::string group;
    int bank = 0;
    std::string file;
    std::string role; // empty = normal bank
};
void to_json(nlohmann::json& j, const HwBankRef& v);
void from_json(const nlohmann::json& j, HwBankRef& v);

// profile.json patch_banks[] entry (*.patchbank.json registration).
// See NOTE ON CONFIDENCE above re: array name.
struct PatchBankRef {
    int bank = 0;
    std::string file;
    std::string name; // optional display-name override; may be empty
};
void to_json(nlohmann::json& j, const PatchBankRef& v);
void from_json(const nlohmann::json& j, PatchBankRef& v);

// profile.json sw_banks[] entry (*.swbank.json registration).
// See NOTE ON CONFIDENCE above re: array name.
struct SwBankRef {
    int bank = 0;
    std::string file;
    std::string name;
};
void to_json(nlohmann::json& j, const SwBankRef& v);
void from_json(const nlohmann::json& j, SwBankRef& v);

// profile.json drum_banks[] entry (confirmed: docs/patch-structure-design.md).
struct DrumBankRef {
    int prog = 0;
    std::string name;
    std::string file;
};
void to_json(nlohmann::json& j, const DrumBankRef& v);
void from_json(const nlohmann::json& j, DrumBankRef& v);

// profile.json banks.scc_wave_banks[] entry (*.sccwave.json registration).
// Confirmed against config_schema/profile.schema.json; the referenced
// *.sccwave.json content itself is not modeled by this library yet (ref is
// preserved for round-tripping only).
struct SccWaveBankRef {
    int bank = 0;
    std::string file;
    std::string name;
};
void to_json(nlohmann::json& j, const SccWaveBankRef& v);
void from_json(const nlohmann::json& j, SccWaveBankRef& v);

// profile.json banks.pcm_banks[] entry (*.pcmbank.json registration).
// Confirmed against config_schema/profile.schema.json. `group` is optional
// per the schema (empty = pre-D-038 "all PCM devices share bank 0" legacy
// behavior) but every real profile actually seen in FITOM_staging sets it
// (ADPCMB/ADPCMA/PCMD8) - PatchWorkspace::loadBanks() uses it exactly like
// HwBankRef::group, via VoicePatchType::stringToVoicePatchType(), to tag the
// resulting fpe::PcmBank so DrumNote/HwPatch source-patch references into it
// (voice_patch_type/patch_bank/patch_prog) can actually be resolved -
// PatchWorkspace::findPcmBank() matches on {voicePatchType, bankIndex}, so a
// bank left at its default VoicePatchType::None from a dropped `group` is
// unfindable by any real reference (D-038 "追記2" - this field was entirely
// missing before that fix, silently discarding `group` for every pcm_banks[]
// entry and leaving every such fpe::PcmBank's voicePatchType at None).
struct PcmBankRef {
    int bank = 0;
    std::string file;
    std::string name;
    std::string group; // empty = not set (legacy shared-bank-0 behavior) - see above
    // `chip`/`offsets_only`: confirmed (2026-08-01) against
    // config_schema/profile.schema.json and real FITOM_staging data
    // (unified.bankset.json's ADPCMB entries) - both previously entirely
    // unmodeled, silently dropped on every load+save round-trip. `chip`
    // also doubles as (part of) this entry's bank_overrides identity key
    // (bank+chip, not bank alone - see BanksObject/mergeBanksObjects()), so
    // dropping it would have made two same-`bank`-different-`chip` entries
    // indistinguishable to the merge/diff logic even if it were otherwise
    // correct.
    std::string chip;          // empty = not set
    bool offsets_only = false; // default false, see profile.schema.json
};
void to_json(nlohmann::json& j, const PcmBankRef& v);
void from_json(const nlohmann::json& j, PcmBankRef& v);

// The shape shared by profile.json's "banks" and "bank_overrides" keys
// (profile.schema.json's "banksObject" definition). `sf2_banks[]` is kept
// only as opaque JSON for round-tripping - this library has no SF2 support
// at all yet (no code anywhere references an SF2 bank; see
// profile.schema.json's sf2_banks description), so unlike the other five
// arrays it is never merged/diffed/mutated, just carried through unchanged.
struct BanksObject {
    std::vector<HwBankRef> hw_banks;
    std::vector<PatchBankRef> patch_banks;
    std::vector<SwBankRef> sw_banks;
    std::vector<DrumBankRef> drum_banks;
    std::vector<SccWaveBankRef> scc_wave_banks;
    std::vector<PcmBankRef> pcm_banks;
    nlohmann::json sf2_banks = nlohmann::json::array();

    bool empty() const {
        return hw_banks.empty() && patch_banks.empty() && sw_banks.empty() && drum_banks.empty() &&
               scc_wave_banks.empty() && pcm_banks.empty() && (!sf2_banks.is_array() || sf2_banks.empty());
    }
};
void to_json(nlohmann::json& j, const BanksObject& v);
void from_json(const nlohmann::json& j, BanksObject& v);

// How profile.json's "banks" or "bank_overrides" key looked on disk:
// absent (`present == false`), a string path to an external file
// (`externalFile` non-empty), or an inline banksObject. Profile::from_json/
// to_json is pure JSON<->struct with no file I/O (same philosophy as the
// rest of this library - see JsonUtil.h), so for the external-file case
// `data` is left empty here; PatchWorkspace is responsible for actually
// reading (PatchWorkspace::load(), via resolveBanksSource()) and - for
// bank_overrides only, never for banks - writing it back
// (PatchWorkspace::save(), via syncBanksSourceForSave()). See
// docs/DESIGN.md D-041.
struct BanksSource {
    bool present = false;
    std::string externalFile; // non-empty <=> the on-disk value was a string
    BanksObject data;
};
void to_json(nlohmann::json& j, const BanksSource& v);
void from_json(const nlohmann::json& j, BanksSource& v);

// Top-level profile.
struct Profile {
    std::string profile_name;

    // The *effective* registry: "banks" merged with "bank_overrides" (see
    // mergeBanksObjects()). All existing CRUD in PatchWorkspace
    // (createDeviceBank() etc) reads/mutates these directly, exactly as
    // before banks/bank_overrides existed - only PatchWorkspace::load()
    // (which populates these from banks/bank_overrides after resolving any
    // external file) and save() (which diffs them back against `banks` to
    // reconstruct `bank_overrides` without ever rewriting a shared external
    // `banks` file) need to know about the split. See docs/DESIGN.md D-041.
    std::vector<HwBankRef> hw_banks;
    std::vector<PatchBankRef> patch_banks;
    std::vector<SwBankRef> sw_banks;
    std::vector<DrumBankRef> drum_banks;
    std::vector<SccWaveBankRef> scc_wave_banks;
    std::vector<PcmBankRef> pcm_banks;

    // "banks" / "bank_overrides" exactly as loaded (or, after
    // PatchWorkspace::save()'s syncBanksSourceForSave(), as about to be
    // written) - see BanksSource's comment. Untouched by ordinary CRUD.
    BanksSource banks;
    BanksSource bank_overrides;

    // Everything else in the file (hw_plugins[], midi_inputs[],
    // midi_outputs[], midi_backend, psg_fallback_chip, devices[], ...) is
    // out of scope for this editor's data model. It is preserved verbatim
    // here and merged back in on save, so editing patches never drops
    // fields this library doesn't understand.
    nlohmann::json extra = nlohmann::json::object();
};
void to_json(nlohmann::json& j, const Profile& v);
void from_json(const nlohmann::json& j, Profile& v);

} // namespace fpe
