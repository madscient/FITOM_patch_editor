#include "fpe/VoicePatchType.h"

#include <array>
#include <utility>

namespace fpe {

namespace {
// Canonical (VoicePatchType, group-string) table. Order matches
// docs/patch-structure-design.md.
constexpr std::pair<VoicePatchType, const char*> kTable[] = {
    {VoicePatchType::OPN,          "OPN"},
    {VoicePatchType::OPN2,         "OPN2"},
    {VoicePatchType::OPM,          "OPM"},
    {VoicePatchType::OPZ,          "OPZ"},
    {VoicePatchType::OPZ2,         "OPZ2"},
    {VoicePatchType::OPL,          "OPL"},
    {VoicePatchType::OPL2,         "OPL2"},
    {VoicePatchType::OPL3_2,       "OPL3_2"},
    {VoicePatchType::OPL_RHY,      "OPL_RHY"},
    {VoicePatchType::OPLL,         "OPLL"},
    {VoicePatchType::OPLLP,        "OPLLP"},
    {VoicePatchType::OPLLX,        "OPLLX"},
    {VoicePatchType::VRC7,         "VRC7"},
    {VoicePatchType::OPL3,         "OPL3"},
    {VoicePatchType::SD1,          "SD1"},
    {VoicePatchType::MA3,          "MA3"},
    {VoicePatchType::MA5,          "MA5"},
    {VoicePatchType::MA7,          "MA7"},
    {VoicePatchType::SSG,          "SSG"},
    {VoicePatchType::EPSG,         "EPSG"},
    {VoicePatchType::DCSG,         "DCSG"},
    {VoicePatchType::SAA,          "SAA"},
    {VoicePatchType::SCC,          "SCC"},
    {VoicePatchType::ADPCMB_Y8950, "ADPCMB_Y8950"},
    {VoicePatchType::ADPCMB,       "ADPCMB"},
    {VoicePatchType::ADPCMA,       "ADPCMA"},
    {VoicePatchType::PCMD8,        "PCMD8"},
    {VoicePatchType::AWM,          "AWM"},
};
} // namespace

bool isSampleBasedVoicePatchType(VoicePatchType t) {
    const auto v = static_cast<uint8_t>(t);
    return v >= static_cast<uint8_t>(VoicePatchType::ADPCMB_Y8950) &&
           v <= static_cast<uint8_t>(VoicePatchType::AWM);
}

bool isValidHwBankTag(VoicePatchType t) {
    if (t == VoicePatchType::None) return false;
    for (const auto& e : kTable) {
        if (e.first == t) return true;
    }
    return false;
}

std::optional<VoicePatchType> stringToVoicePatchType(const std::string& group) {
    for (const auto& e : kTable) {
        if (group == e.second) return e.first;
    }
    return std::nullopt;
}

std::string voicePatchTypeToString(VoicePatchType t) {
    for (const auto& e : kTable) {
        if (e.first == t) return e.second;
    }
    return "?";
}

} // namespace fpe
