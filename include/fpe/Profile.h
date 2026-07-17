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
// Confirmed against config_schema/profile.schema.json; the referenced
// *.pcmbank.json content itself is not modeled by this library yet (ref is
// preserved for round-tripping only).
struct PcmBankRef {
    int bank = 0;
    std::string file;
    std::string name;
};
void to_json(nlohmann::json& j, const PcmBankRef& v);
void from_json(const nlohmann::json& j, PcmBankRef& v);

// Top-level profile.
struct Profile {
    std::string profile_name;

    // All six live under the profile's "banks" object on disk (see
    // to_json/from_json in Profile.cpp) - the members here are flattened
    // for convenient access.
    std::vector<HwBankRef> hw_banks;
    std::vector<PatchBankRef> patch_banks;
    std::vector<SwBankRef> sw_banks;
    std::vector<DrumBankRef> drum_banks;
    std::vector<SccWaveBankRef> scc_wave_banks;
    std::vector<PcmBankRef> pcm_banks;

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
