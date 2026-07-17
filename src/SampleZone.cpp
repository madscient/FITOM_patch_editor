#include "fpe/SampleZone.h"

#include <nlohmann/json.hpp>

#include "fpe/JsonUtil.h"

namespace fpe {
using json_util::getOr;
using json_util::getRequired;

void to_json(nlohmann::json& j, const SampleZone& v) {
    j = nlohmann::json{
        {"key_min", v.key_min}, {"key_max", v.key_max},
        {"vel_min", v.vel_min}, {"vel_max", v.vel_max},
        {"wave_index", v.wave_index}, {"root_note", v.root_note},
    };
}
void from_json(const nlohmann::json& j, SampleZone& v) {
    v.key_min = getOr<uint8_t>(j, "key_min", 0);
    v.key_max = getOr<uint8_t>(j, "key_max", 127);
    v.vel_min = getOr<uint8_t>(j, "vel_min", 0);
    v.vel_max = getOr<uint8_t>(j, "vel_max", 127);
    v.wave_index = getRequired<int>(j, "wave_index", "SampleZone");
    v.root_note = getOr<uint8_t>(j, "root_note", 60);
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
}
void from_json(const nlohmann::json& j, SampleZoneBank& v) {
    v.name = getOr<std::string>(j, "name", "");
    v.patches = getOr<std::vector<SampleZonePatch>>(j, "patches", {});
}

} // namespace fpe
