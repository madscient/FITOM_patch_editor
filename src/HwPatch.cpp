#include "fpe/HwPatch.h"

#include <nlohmann/json.hpp>

#include "fpe/JsonUtil.h"

namespace fpe {
using json_util::getOr;
using json_util::getRequired;

void to_json(nlohmann::json& j, const FmHwVoice& v) {
    j = nlohmann::json{
        {"FB", v.FB}, {"ALG", v.ALG}, {"AMS", v.AMS},
        {"PMS", v.PMS}, {"NFQ", v.NFQ}, {"FB2", v.FB2},
    };
}
void from_json(const nlohmann::json& j, FmHwVoice& v) {
    v.FB  = getOr<uint8_t>(j, "FB", 0);
    v.ALG = getOr<uint8_t>(j, "ALG", 0);
    v.AMS = getOr<uint8_t>(j, "AMS", 0);
    v.PMS = getOr<uint8_t>(j, "PMS", 0);
    v.NFQ = getOr<uint8_t>(j, "NFQ", 0);
    v.FB2 = getOr<uint8_t>(j, "FB2", 0);
}

void to_json(nlohmann::json& j, const FmHwOp& v) {
    j = nlohmann::json{
        {"AR", v.AR}, {"DR", v.DR}, {"SL", v.SL}, {"SR", v.SR}, {"RR", v.RR},
        {"TL", v.TL}, {"KSR", v.KSR}, {"KSL", v.KSL}, {"MUL", v.MUL},
        {"DT1", v.DT1}, {"DT2", v.DT2}, {"PDT", v.PDT}, {"AM", v.AM},
        {"VIB", v.VIB}, {"EGT", v.EGT}, {"WS", v.WS}, {"REV", v.REV},
        {"EGS", v.EGS}, {"DT3", v.DT3},
    };
}
void from_json(const nlohmann::json& j, FmHwOp& v) {
    v.AR  = getOr<uint8_t>(j, "AR", 0);
    v.DR  = getOr<uint8_t>(j, "DR", 0);
    v.SL  = getOr<uint8_t>(j, "SL", 0);
    v.SR  = getOr<uint8_t>(j, "SR", 0);
    v.RR  = getOr<uint8_t>(j, "RR", 0);
    v.TL  = getOr<uint8_t>(j, "TL", 0);
    v.KSR = getOr<uint8_t>(j, "KSR", 0);
    v.KSL = getOr<uint8_t>(j, "KSL", 0);
    v.MUL = getOr<uint8_t>(j, "MUL", 0);
    v.DT1 = getOr<uint8_t>(j, "DT1", 0);
    v.DT2 = getOr<uint8_t>(j, "DT2", 0);
    v.PDT = getOr<int16_t>(j, "PDT", 0);
    v.AM  = getOr<uint8_t>(j, "AM", 0);
    v.VIB = getOr<uint8_t>(j, "VIB", 0);
    v.EGT = getOr<uint8_t>(j, "EGT", 0);
    v.WS  = getOr<uint8_t>(j, "WS", 0);
    v.REV = getOr<uint8_t>(j, "REV", 0);
    v.EGS = getOr<uint8_t>(j, "EGS", 0);
    v.DT3 = getOr<uint8_t>(j, "DT3", 0);
}

void to_json(nlohmann::json& j, const FmChipExt& v) {
    j = nlohmann::json{
        {"FIX", v.FIX}, {"ALG_EXT", v.ALG_EXT}, {"HWEP", v.HWEP},
        {"rhythm_ch", v.rhythm_ch},
        {"target_voice_patch_type", static_cast<uint8_t>(v.target_voice_patch_type)},
    };
}
void from_json(const nlohmann::json& j, FmChipExt& v) {
    v.FIX     = getOr<uint8_t>(j, "FIX", 0);
    v.ALG_EXT = getOr<uint8_t>(j, "ALG_EXT", 0);
    v.HWEP    = getOr<uint16_t>(j, "HWEP", 0);
    v.rhythm_ch = getOr<uint8_t>(j, "rhythm_ch", 255);
    v.target_voice_patch_type =
        static_cast<VoicePatchType>(getOr<uint8_t>(j, "target_voice_patch_type", 0));
}

void to_json(nlohmann::json& j, const BuiltinRef& v) {
    j = nlohmann::json{{"patch_type", v.patch_type}, {"patch_no", v.patch_no}};
}
void from_json(const nlohmann::json& j, BuiltinRef& v) {
    v.patch_type = getRequired<std::string>(j, "patch_type", "BuiltinRef");
    v.patch_no   = getOr<int>(j, "patch_no", 0);
}

void to_json(nlohmann::json& j, const HwPatch& v) {
    j = nlohmann::json{
        {"prog", v.prog}, {"name", v.name},
        {"sw_bank", v.sw_bank}, {"sw_prog", v.sw_prog},
    };
    if (v.builtin) {
        j["builtin"] = *v.builtin;
    } else {
        // FB/ALG/AMS/PMS/NFQ/FB2 are flattened onto this same top-level
        // object (D-028) - FITOM_X's own config_schema/hwbank.schema.json,
        // confirmed against real production *.hwbank.json files, has no
        // "hw" wrapper at all. Nesting them under "hw" (as this code did
        // before D-028) meant from_json below could never find them in a
        // real file, silently zeroing FB/ALG/AMS/PMS/NFQ/FB2 on every load
        // and then permanently destroying them the next time anything
        // called PatchWorkspace::save() - see docs/DESIGN.md D-028.
        const nlohmann::json hwJson = v.hw;
        for (auto it = hwJson.begin(); it != hwJson.end(); ++it) j[it.key()] = it.value();
        j["ops"] = v.ops;
        j["ext"] = v.ext;
    }
}
void from_json(const nlohmann::json& j, HwPatch& v) {
    v.prog = getRequired<int>(j, "prog", "HwPatch");
    v.name = getOr<std::string>(j, "name", "");
    v.sw_bank = getOr<int>(j, "sw_bank", -1);
    v.sw_prog = getOr<int>(j, "sw_prog", -1);

    if (j.contains("builtin") && !j.at("builtin").is_null()) {
        v.builtin = j.at("builtin").get<BuiltinRef>();
        v.hw = FmHwVoice{};
        v.ops.clear();
        v.ext = FmChipExt{};
        return;
    }

    v.builtin.reset();
    // FB/ALG/AMS/PMS/NFQ/FB2 are top-level keys on this same object (see
    // to_json above, D-028) - from_json(FmHwVoice) picks each one out of
    // `j` directly, so passing the whole HwPatch-shaped object is correct
    // (unrelated keys like "prog"/"ops" are simply ignored by it).
    v.hw = j.get<FmHwVoice>();
    v.ext = getOr<FmChipExt>(j, "ext", FmChipExt{});
    if (j.contains("ops") && j.at("ops").is_array()) {
        v.ops = j.at("ops").get<std::vector<FmHwOp>>();
    } else {
        v.ops.clear();
    }
}

HwPatch* HwBank::findByProg(int prog) {
    for (auto& p : patches) if (p.prog == prog) return &p;
    return nullptr;
}
const HwPatch* HwBank::findByProg(int prog) const {
    for (auto& p : patches) if (p.prog == prog) return &p;
    return nullptr;
}
const HwPatch* HwBank::findByBuiltinRef(const std::string& patchType, int patchNo) const {
    for (auto& p : patches) {
        if (p.builtin && p.builtin->patch_type == patchType && p.builtin->patch_no == patchNo) {
            return &p;
        }
    }
    return nullptr;
}

void to_json(nlohmann::json& j, const HwBank& v) {
    j = nlohmann::json{{"name", v.name}, {"patches", v.patches}};
}
void from_json(const nlohmann::json& j, HwBank& v) {
    v.name = getOr<std::string>(j, "name", "");
    v.patches = getOr<std::vector<HwPatch>>(j, "patches", {});
}

} // namespace fpe
