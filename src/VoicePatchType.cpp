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
    {VoicePatchType::DSG,          "DSG"},
    {VoicePatchType::SCC,          "SCC"},
    {VoicePatchType::ADPCMB_Y8950, "ADPCMB_Y8950"},
    {VoicePatchType::ADPCMB,       "ADPCMB"},
    {VoicePatchType::ADPCMA,       "ADPCMA"},
    {VoicePatchType::PCMD8,        "PCMD8"},
    {VoicePatchType::AWM,          "AWM"},
};

// Additional spellings accepted on *input* only - never produced by
// voicePatchTypeToString(). Mirrors FITOM_X core/src/Config.cpp's
// FITOMConfig::stringToVoicePatchType() one-for-one; keep the two in sync
// (a group string this table doesn't know makes PatchWorkspace drop the
// whole bank with a warning).
constexpr std::pair<const char*, VoicePatchType> kAliases[] = {
    // OPNA/OPNB are collapsed onto OPN2 in FITOM_X itself ("音色パラメータ
    // 互換性の観点でOPN2に統合済み") - they are legal profile.schema.json
    // group values, so a profile using them used to lose its banks here.
    {"OPNA",    VoicePatchType::OPN2},
    {"OPNB",    VoicePatchType::OPN2},
    {"SD-1",    VoicePatchType::SD1},
    {"MA-3",    VoicePatchType::MA3},
    {"MA-5",    VoicePatchType::MA5},
    {"MA-7",    VoicePatchType::MA7},
    {"AY8930",  VoicePatchType::EPSG},
    {"SAA1099", VoicePatchType::SAA},
    {"YM2163",  VoicePatchType::DSG},
    {"SCCP",    VoicePatchType::SCC},
    // Legacy coarse family tags, kept working for old profiles.
    {"PSG",     VoicePatchType::SSG},
    {"PCM",     VoicePatchType::ADPCMB},
};
} // namespace

bool isSampleBasedVoicePatchType(VoicePatchType t) {
    return t == VoicePatchType::AWM;
}

bool isPcmWaveformVoicePatchType(VoicePatchType t) {
    switch (t) {
        case VoicePatchType::ADPCMB_Y8950:
        case VoicePatchType::ADPCMB:
        case VoicePatchType::ADPCMA:
        case VoicePatchType::PCMD8:
            return true;
        default:
            return false;
    }
}

bool isValidHwBankTag(VoicePatchType t) {
    if (t == VoicePatchType::None) return false;
    // DSG is a recognized chip type but never a bank tag: it owns no
    // *.hwbank.json at all (BuiltinVoices.h), which is why FITOM_X's
    // config_schema/profile.schema.json leaves it out of the hw_banks[].group
    // enum even though its stringToVoicePatchType() accepts the string.
    if (t == VoicePatchType::DSG) return false;
    for (const auto& e : kTable) {
        if (e.first == t) return true;
    }
    return false;
}

bool isPsgFamilyVoicePatchType(VoicePatchType t) {
    switch (t) {
        case VoicePatchType::SSG:
        case VoicePatchType::EPSG:
        case VoicePatchType::DCSG:
        case VoicePatchType::SAA:
        case VoicePatchType::SCC:
            return true;
        default:
            return false;
    }
}

VoicePatchType hwBankLookupVoicePatchType(VoicePatchType t) {
    return isPsgFamilyVoicePatchType(t) ? VoicePatchType::SSG : t;
}

std::optional<VoicePatchType> stringToVoicePatchType(const std::string& group) {
    for (const auto& e : kTable) {
        if (group == e.second) return e.first;
    }
    for (const auto& e : kAliases) {
        if (group == e.first) return e.second;
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
