#include "fpe/PcmBank.h"

#include <nlohmann/json.hpp>

#include "fpe/JsonUtil.h"

namespace fpe {
using json_util::getOr;

void to_json(nlohmann::json& j, const PcmBankEntry& v) {
    j = nlohmann::json{
        {"entry_no", v.entry_no},
        {"name", v.name},
        {"start_offset", v.start_offset},
        {"end_offset", v.end_offset},
        {"size", v.size},
        {"padded_size", v.padded_size},
    };
    if (v.root_note != 69) j["root_note"] = v.root_note;
}
void from_json(const nlohmann::json& j, PcmBankEntry& v) {
    v.entry_no = getOr<int>(j, "entry_no", -1);
    v.name = getOr<std::string>(j, "name", "");
    v.start_offset = getOr<uint32_t>(j, "start_offset", 0);
    v.end_offset = getOr<uint32_t>(j, "end_offset", 0);
    v.size = getOr<uint32_t>(j, "size", 0);
    v.padded_size = getOr<uint32_t>(j, "padded_size", 0);
    v.root_note = getOr<uint8_t>(j, "root_note", 69);
}

PcmBankEntry pcmEntryFromAdpcmPacker(const nlohmann::json& j, int entryNo) {
    PcmBankEntry e;
    e.entry_no = entryNo;
    e.name = getOr<std::string>(j, "name", "");
    e.start_offset = getOr<uint32_t>(j, "offset", 0);
    e.size = getOr<uint32_t>(j, "size", 0);
    e.padded_size = getOr<uint32_t>(j, "padded_size", e.size);
    e.end_offset = e.padded_size > 0 ? e.start_offset + e.padded_size - 1 : e.start_offset;
    e.root_note = getOr<uint8_t>(j, "root_note", 69);
    return e;
}

void to_json(nlohmann::json& j, const PcmSwPatchRef& v) {
    j = nlohmann::json{{"entry_no", v.entry_no}, {"sw_bank", v.sw_bank}, {"sw_prog", v.sw_prog}};
}
void from_json(const nlohmann::json& j, PcmSwPatchRef& v) {
    v.entry_no = json_util::getRequired<int>(j, "entry_no", "pcmbank swpatches[]");
    v.sw_bank = getOr<int>(j, "sw_bank", -1);
    v.sw_prog = getOr<int>(j, "sw_prog", -1);
}

const PcmBankEntry* PcmBank::findByEntryNo(int entryNo) const {
    for (const auto& e : entries) if (e.entry_no == entryNo) return &e;
    return nullptr;
}

void to_json(nlohmann::json& j, const PcmBank& v) {
    j = nlohmann::json{
        {"name", v.name},
        {"codec", v.codec},
        {"sample_rate", v.sample_rate},
        {"boundary", v.boundary},
        {"bin_file", v.bin_file},
    };
    if (!v.adpcm_json.empty()) j["adpcm_json"] = v.adpcm_json;
    if (!v.ownEntries.empty() || v.adpcm_json.empty()) j["entries"] = v.ownEntries;
    j["swpatches"] = v.swpatches;
    json_util::mergeUnknownKeys(j, v.extra);
}

void from_json(const nlohmann::json& j, PcmBank& v) {
    v.name = getOr<std::string>(j, "name", "");
    v.codec = getOr<std::string>(j, "codec", "");
    v.sample_rate = getOr<uint32_t>(j, "sample_rate", 0);
    v.boundary = getOr<uint32_t>(j, "boundary", 256);
    v.bin_file = getOr<std::string>(j, "bin_file", "");
    v.adpcm_json = getOr<std::string>(j, "adpcm_json", "");
    v.ownEntries = getOr<std::vector<PcmBankEntry>>(j, "entries", {});
    v.entries = v.ownEntries;
    v.swpatches = getOr<std::vector<PcmSwPatchRef>>(j, "swpatches", {});
    v.extra = json_util::unknownKeys(
        j, {"name", "codec", "sample_rate", "boundary", "bin_file", "adpcm_json", "entries", "swpatches"});
}

} // namespace fpe
