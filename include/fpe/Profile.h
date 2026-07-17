#pragma once
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// Top-level *.profile.json. Source: docs/config-design.md
// ("*.profile.json — プロファイル"), docs/patch-structure-design.md
// (hw_banks[]/drum_banks[] examples).
//
// NOTE ON CONFIDENCE: docs/config-design.md's own worked examples
// (profiles/emulator_only.profile.json, profiles/studio.profile.json) only
// show `profile_name`, `hw_plugins[]` and `midi_inputs[]`. The registry
// array names for native patch banks and performance banks are inferred by
// analogy with the confirmed `hw_banks[]` (docs/patch-structure-design.md
// "HwBank 側のタグ付けルール") and `drum_banks[]`
// (docs/patch-structure-design.md "リズムチャンネル: ドラムマップによる解決")
// arrays: `patch_banks[]` for *.patchbank.json and `sw_banks[]` for
// *.swbank.json, each keyed the same way (`bank` index + `file` path).
// Verify these two names against the real profile.schema.json before
// treating them as authoritative.

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

// Top-level profile.
struct Profile {
    std::string profile_name;

    std::vector<HwBankRef> hw_banks;
    std::vector<PatchBankRef> patch_banks;
    std::vector<SwBankRef> sw_banks;
    std::vector<DrumBankRef> drum_banks;

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
