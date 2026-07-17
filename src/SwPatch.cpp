#include "fpe/SwPatch.h"

#include <nlohmann/json.hpp>

#include "fpe/JsonUtil.h"

namespace fpe {
using json_util::getOr;
using json_util::getRequired;

void to_json(nlohmann::json& j, const FmSwVoice& v) {
    j = nlohmann::json{
        {"LWF", v.LWF}, {"LFS", v.LFS}, {"LFM", v.LFM}, {"LFD", v.LFD},
        {"LFR", v.LFR}, {"LFI", v.LFI}, {"depth_cents", v.depth_cents},
    };
}
void from_json(const nlohmann::json& j, FmSwVoice& v) {
    v.LWF = getOr<uint8_t>(j, "LWF", 0);
    v.LFS = getOr<uint8_t>(j, "LFS", 0);
    v.LFM = getOr<uint8_t>(j, "LFM", 0);
    v.LFD = getOr<uint8_t>(j, "LFD", 0);
    v.LFR = getOr<uint8_t>(j, "LFR", 0);
    v.LFI = getOr<uint8_t>(j, "LFI", 0);
    v.depth_cents = getOr<int16_t>(j, "depth_cents", 0);
}

void to_json(nlohmann::json& j, const FmSwOp& v) {
    j = nlohmann::json{
        {"VTL", v.VTL}, {"VAR", v.VAR}, {"VDR", v.VDR}, {"VSL", v.VSL},
        {"VSR", v.VSR}, {"VRR", v.VRR}, {"VLD", v.VLD}, {"VLR", v.VLR},
        {"SLW", v.SLW}, {"SLS", v.SLS}, {"SLM", v.SLM}, {"SLD", v.SLD},
        {"SLY", v.SLY}, {"SLR", v.SLR}, {"SLI", v.SLI},
    };
}
void from_json(const nlohmann::json& j, FmSwOp& v) {
    v.VTL = getOr<uint8_t>(j, "VTL", 0);
    v.VAR = getOr<uint8_t>(j, "VAR", 0);
    v.VDR = getOr<uint8_t>(j, "VDR", 0);
    v.VSL = getOr<uint8_t>(j, "VSL", 0);
    v.VSR = getOr<uint8_t>(j, "VSR", 0);
    v.VRR = getOr<uint8_t>(j, "VRR", 0);
    v.VLD = getOr<uint8_t>(j, "VLD", 0);
    v.VLR = getOr<uint8_t>(j, "VLR", 0);
    v.SLW = getOr<uint8_t>(j, "SLW", 0);
    v.SLS = getOr<uint8_t>(j, "SLS", 0);
    v.SLM = getOr<uint8_t>(j, "SLM", 0);
    v.SLD = getOr<uint8_t>(j, "SLD", 0);
    v.SLY = getOr<uint8_t>(j, "SLY", 0);
    v.SLR = getOr<uint8_t>(j, "SLR", 0);
    v.SLI = getOr<uint8_t>(j, "SLI", 0);
}

void to_json(nlohmann::json& j, const SwPatch& v) {
    j = nlohmann::json{
        {"prog", v.prog}, {"name", v.name}, {"sw", v.sw},
        {"ops", v.ops}, {"fine_transpose", v.fine_transpose},
    };
}
void from_json(const nlohmann::json& j, SwPatch& v) {
    v.prog = getRequired<int>(j, "prog", "SwPatch");
    v.name = getOr<std::string>(j, "name", "");
    v.sw = getOr<FmSwVoice>(j, "sw", FmSwVoice{});
    v.fine_transpose = getOr<int16_t>(j, "fine_transpose", 0);

    v.ops.fill(FmSwOp{});
    if (j.contains("ops") && j.at("ops").is_array()) {
        const auto& arr = j.at("ops");
        for (size_t i = 0; i < arr.size() && i < v.ops.size(); ++i) {
            v.ops[i] = arr[i].get<FmSwOp>();
        }
    }
}

SwPatch* SwBank::findByProg(int prog) {
    for (auto& p : patches) if (p.prog == prog) return &p;
    return nullptr;
}
const SwPatch* SwBank::findByProg(int prog) const {
    for (auto& p : patches) if (p.prog == prog) return &p;
    return nullptr;
}

void to_json(nlohmann::json& j, const SwBank& v) {
    j = nlohmann::json{{"name", v.name}, {"patches", v.patches}};
}
void from_json(const nlohmann::json& j, SwBank& v) {
    v.name = getOr<std::string>(j, "name", "");
    v.patches = getOr<std::vector<SwPatch>>(j, "patches", {});
}

} // namespace fpe
