#include "fpe/NativePatch.h"

#include <nlohmann/json.hpp>

#include "fpe/JsonUtil.h"

namespace fpe {
using json_util::getOr;
using json_util::getRequired;

void to_json(nlohmann::json& j, const ToneLayer& v) {
    j = nlohmann::json{
        {"voice_patch_type", static_cast<uint8_t>(v.voice_patch_type)},
        {"hw_bank", v.hw_bank}, {"hw_prog", v.hw_prog},
        {"note_range_lo", v.note_range_lo}, {"note_range_hi", v.note_range_hi},
        {"transpose", v.transpose}, {"volume_offset", v.volume_offset},
        {"pan_offset", v.pan_offset}, {"enabled", v.enabled},
    };
}
void from_json(const nlohmann::json& j, ToneLayer& v) {
    v.voice_patch_type = static_cast<VoicePatchType>(getOr<uint8_t>(j, "voice_patch_type", 0));
    v.hw_bank = getOr<int>(j, "hw_bank", 0);
    v.hw_prog = getOr<int>(j, "hw_prog", 0);
    v.note_range_lo = getOr<uint8_t>(j, "note_range_lo", 0);
    v.note_range_hi = getOr<uint8_t>(j, "note_range_hi", 127);
    v.transpose = getOr<int8_t>(j, "transpose", 0);
    v.volume_offset = getOr<int8_t>(j, "volume_offset", 0);
    v.pan_offset = getOr<int8_t>(j, "pan_offset", 0);
    v.enabled = getOr<bool>(j, "enabled", true);
}

void to_json(nlohmann::json& j, const Patch& v) {
    j = nlohmann::json{
        {"prog", v.prog}, {"name", v.name}, {"poly", v.poly},
        {"sw_bank", v.sw_bank}, {"sw_prog", v.sw_prog}, {"layers", v.layers},
    };
}
void from_json(const nlohmann::json& j, Patch& v) {
    v.prog = getRequired<int>(j, "prog", "Patch");
    v.name = getOr<std::string>(j, "name", "");
    v.poly = getOr<int>(j, "poly", 0);
    v.sw_bank = getOr<int>(j, "sw_bank", -1);
    v.sw_prog = getOr<int>(j, "sw_prog", -1);
    v.layers = getOr<std::vector<ToneLayer>>(j, "layers", {});
}

Patch* PatchBank::findByProg(int prog) {
    for (auto& p : patches) if (p.prog == prog) return &p;
    return nullptr;
}
const Patch* PatchBank::findByProg(int prog) const {
    for (auto& p : patches) if (p.prog == prog) return &p;
    return nullptr;
}

void to_json(nlohmann::json& j, const PatchBank& v) {
    j = nlohmann::json{{"name", v.name}, {"patches", v.patches}};
}
void from_json(const nlohmann::json& j, PatchBank& v) {
    v.name = getOr<std::string>(j, "name", "");
    v.patches = getOr<std::vector<Patch>>(j, "patches", {});
}

} // namespace fpe
