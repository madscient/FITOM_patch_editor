#include "fpe/DrumKit.h"

#include <nlohmann/json.hpp>

#include "fpe/JsonUtil.h"

namespace fpe {
using json_util::getOr;
using json_util::getRequired;

void to_json(nlohmann::json& j, const DrumNote& v) {
    j = nlohmann::json{
        {"note", v.note}, {"name", v.name},
        {"voice_patch_type", static_cast<uint8_t>(v.voice_patch_type)},
        {"patch_bank", v.patch_bank}, {"patch_prog", v.patch_prog},
        {"play_note", v.play_note},
        {"sw_bank", v.sw_bank}, {"sw_prog", v.sw_prog},
    };
}
void from_json(const nlohmann::json& j, DrumNote& v) {
    v.note = getRequired<uint8_t>(j, "note", "DrumNote");
    v.name = getOr<std::string>(j, "name", "");
    v.voice_patch_type = static_cast<VoicePatchType>(getOr<uint8_t>(j, "voice_patch_type", 0));
    v.patch_bank = getOr<int>(j, "patch_bank", 0);
    v.patch_prog = getOr<int>(j, "patch_prog", 0);
    v.play_note = getOr<uint8_t>(j, "play_note", v.note);
    v.sw_bank = getOr<int>(j, "sw_bank", -1);
    v.sw_prog = getOr<int>(j, "sw_prog", -1);
}

std::vector<DrumNote> DrumKit::effectiveNotes() const {
    if (type == DrumKitType::Routed) return notes;

    std::vector<DrumNote> out;
    out.reserve(static_cast<size_t>(note_max) - note_min + 1);
    for (int n = note_min; n <= note_max; ++n) {
        DrumNote dn;
        dn.note = static_cast<uint8_t>(n);
        dn.name = name.empty() ? "" : (name + " " + std::to_string(n));
        dn.voice_patch_type = VoicePatchType::None; // "direct" kits are expressed as a normal-mode PatchBank/Patch reference
        dn.patch_bank = patch_bank;
        dn.patch_prog = patch_prog;
        dn.play_note = static_cast<uint8_t>(n); // passthrough: play_note == received note
        out.push_back(dn);
    }
    return out;
}

DrumNote* DrumKit::findNote(uint8_t note) {
    for (auto& n : notes) if (n.note == note) return &n;
    return nullptr;
}

void to_json(nlohmann::json& j, const DrumKit& v) {
    j["type"] = (v.type == DrumKitType::Routed) ? "routed" : "direct";
    j["name"] = v.name;
    if (v.type == DrumKitType::Routed) {
        j["notes"] = v.notes;
        if (!v.choke_groups.empty()) j["choke_groups"] = v.choke_groups;
    } else {
        j["patch_bank"] = v.patch_bank;
        j["patch_prog"] = v.patch_prog;
        j["note_min"] = v.note_min;
        j["note_max"] = v.note_max;
    }
}
void from_json(const nlohmann::json& j, DrumKit& v) {
    const std::string typeStr = getOr<std::string>(j, "type", "routed");
    v.type = (typeStr == "direct") ? DrumKitType::Direct : DrumKitType::Routed;
    v.name = getOr<std::string>(j, "name", "");

    if (v.type == DrumKitType::Direct) {
        v.patch_bank = getRequired<int>(j, "patch_bank", "DrumKit(direct)");
        v.patch_prog = getOr<int>(j, "patch_prog", 0);
        v.note_min = getOr<uint8_t>(j, "note_min", 0);
        v.note_max = getOr<uint8_t>(j, "note_max", 127);
        v.notes.clear();
        v.choke_groups.clear();
    } else {
        v.notes = getOr<std::vector<DrumNote>>(j, "notes", {});
        v.choke_groups = getOr<std::vector<std::vector<uint8_t>>>(j, "choke_groups", {});
    }
}

} // namespace fpe
