#include "fpe/SampleZone.h"

#include <nlohmann/json.hpp>

#include "fpe/JsonUtil.h"

namespace fpe {
using json_util::getOr;
using json_util::getRequired;

void to_json(nlohmann::json& j, const SampleZone& v) {
    j = nlohmann::json{
        {"key_min", v.key_min},
        {"key_max", v.key_max},
        {"vel_min", v.vel_min},
        {"vel_max", v.vel_max},
        {"wave_index", v.wave_index},
        {"pitch_offset", v.pitch_offset},
        {"key_scaling", v.key_scaling},
        {"tone_attenuate", v.tone_attenuate},
        {"volume_factor", v.volume_factor},
        {"root_note", v.root_note},
        {"name", v.name},
        {"sw_bank", v.sw_bank},
        {"sw_prog", v.sw_prog},
    };
}
void from_json(const nlohmann::json& j, SampleZone& v) {
    v.key_min = getOr<uint8_t>(j, "key_min", 0);
    v.key_max = getOr<uint8_t>(j, "key_max", 127);
    v.vel_min = getOr<uint8_t>(j, "vel_min", 0);
    v.vel_max = getOr<uint8_t>(j, "vel_max", 127);
    v.wave_index = getRequired<int>(j, "wave_index", "SampleZone");
    v.pitch_offset = getOr<int16_t>(j, "pitch_offset", 0);
    v.key_scaling = getOr<uint16_t>(j, "key_scaling", 100);
    v.tone_attenuate = getOr<uint8_t>(j, "tone_attenuate", 0);
    v.volume_factor = getOr<uint8_t>(j, "volume_factor", 254);
    v.root_note = getOr<uint8_t>(j, "root_note", 69);
    v.name = getOr<std::string>(j, "name", "");
    v.sw_bank = getOr<int>(j, "sw_bank", -1);
    v.sw_prog = getOr<int>(j, "sw_prog", -1);
}

void to_json(nlohmann::json& j, const SampleZonePatch& v) {
    j = nlohmann::json{{"prog", v.prog}, {"name", v.name}, {"zones", v.zones}};
}
void from_json(const nlohmann::json& j, SampleZonePatch& v) {
    v.prog = getRequired<int>(j, "prog", "SampleZonePatch");
    v.name = getOr<std::string>(j, "name", "");
    v.zones = getOr<std::vector<SampleZone>>(j, "zones", {});
}

SampleZonePatch* SampleZoneBank::findByProg(int prog) {
    for (auto& p : patches) if (p.prog == prog) return &p;
    return nullptr;
}
const SampleZonePatch* SampleZoneBank::findByProg(int prog) const {
    for (auto& p : patches) if (p.prog == prog) return &p;
    return nullptr;
}

void to_json(nlohmann::json& j, const SampleZoneBank& v) {
    j = nlohmann::json{{"name", v.name}, {"patches", v.patches}};
    json_util::mergeUnknownKeys(j, v.extra);
}
void from_json(const nlohmann::json& j, SampleZoneBank& v) {
    v.name = getOr<std::string>(j, "name", "");
    v.patches = getOr<std::vector<SampleZonePatch>>(j, "patches", {});
    v.extra = json_util::unknownKeys(j, {"name", "patches"});
}

} // namespace fpe
