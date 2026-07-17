#include "fpe/Profile.h"

#include "fpe/JsonUtil.h"

namespace fpe {
using json_util::getOr;
using json_util::getRequired;

void to_json(nlohmann::json& j, const HwBankRef& v) {
    j = nlohmann::json{{"group", v.group}, {"bank", v.bank}, {"file", v.file}};
    if (!v.role.empty()) j["role"] = v.role;
}
void from_json(const nlohmann::json& j, HwBankRef& v) {
    v.group = getRequired<std::string>(j, "group", "hw_banks[]");
    v.bank = getOr<int>(j, "bank", 0);
    v.file = getRequired<std::string>(j, "file", "hw_banks[]");
    v.role = getOr<std::string>(j, "role", "");
}

void to_json(nlohmann::json& j, const PatchBankRef& v) {
    j = nlohmann::json{{"bank", v.bank}, {"file", v.file}};
    if (!v.name.empty()) j["name"] = v.name;
}
void from_json(const nlohmann::json& j, PatchBankRef& v) {
    v.bank = getOr<int>(j, "bank", 0);
    v.file = getRequired<std::string>(j, "file", "patch_banks[]");
    v.name = getOr<std::string>(j, "name", "");
}

void to_json(nlohmann::json& j, const SwBankRef& v) {
    j = nlohmann::json{{"bank", v.bank}, {"file", v.file}};
    if (!v.name.empty()) j["name"] = v.name;
}
void from_json(const nlohmann::json& j, SwBankRef& v) {
    v.bank = getOr<int>(j, "bank", 0);
    v.file = getRequired<std::string>(j, "file", "sw_banks[]");
    v.name = getOr<std::string>(j, "name", "");
}

void to_json(nlohmann::json& j, const DrumBankRef& v) {
    j = nlohmann::json{{"prog", v.prog}, {"name", v.name}, {"file", v.file}};
}
void from_json(const nlohmann::json& j, DrumBankRef& v) {
    v.prog = getOr<int>(j, "prog", 0);
    v.name = getOr<std::string>(j, "name", "");
    v.file = getRequired<std::string>(j, "file", "drum_banks[]");
}

void to_json(nlohmann::json& j, const SccWaveBankRef& v) {
    j = nlohmann::json{{"bank", v.bank}, {"file", v.file}};
    if (!v.name.empty()) j["name"] = v.name;
}
void from_json(const nlohmann::json& j, SccWaveBankRef& v) {
    v.bank = getOr<int>(j, "bank", 0);
    v.file = getRequired<std::string>(j, "file", "scc_wave_banks[]");
    v.name = getOr<std::string>(j, "name", "");
}

void to_json(nlohmann::json& j, const PcmBankRef& v) {
    j = nlohmann::json{{"bank", v.bank}, {"file", v.file}};
    if (!v.name.empty()) j["name"] = v.name;
}
void from_json(const nlohmann::json& j, PcmBankRef& v) {
    v.bank = getOr<int>(j, "bank", 0);
    v.file = getRequired<std::string>(j, "file", "pcm_banks[]");
    v.name = getOr<std::string>(j, "name", "");
}

namespace {
// All bank-registry arrays live nested under "banks" on disk (confirmed
// against the real profile.schema.json - see NOTE in Profile.h); only
// "profile_name" and "banks" itself are managed at the top level.
constexpr const char* kManagedKeys[] = {"profile_name", "banks"};
bool isManagedKey(const std::string& key) {
    for (auto k : kManagedKeys) if (key == k) return true;
    return false;
}
} // namespace

void to_json(nlohmann::json& j, const Profile& v) {
    j = v.extra.is_object() ? v.extra : nlohmann::json::object();
    j["profile_name"] = v.profile_name;

    nlohmann::json banks = nlohmann::json::object();
    if (!v.hw_banks.empty()) banks["hw_banks"] = v.hw_banks;
    if (!v.patch_banks.empty()) banks["patch_banks"] = v.patch_banks;
    if (!v.sw_banks.empty()) banks["sw_banks"] = v.sw_banks;
    if (!v.drum_banks.empty()) banks["drum_banks"] = v.drum_banks;
    if (!v.scc_wave_banks.empty()) banks["scc_wave_banks"] = v.scc_wave_banks;
    if (!v.pcm_banks.empty()) banks["pcm_banks"] = v.pcm_banks;
    if (!banks.empty()) j["banks"] = banks;
}
void from_json(const nlohmann::json& j, Profile& v) {
    v.profile_name = getOr<std::string>(j, "profile_name", "");

    nlohmann::json banks = nlohmann::json::object();
    if (j.is_object() && j.contains("banks") && j.at("banks").is_object()) banks = j.at("banks");

    v.hw_banks = getOr<std::vector<HwBankRef>>(banks, "hw_banks", {});
    v.patch_banks = getOr<std::vector<PatchBankRef>>(banks, "patch_banks", {});
    v.sw_banks = getOr<std::vector<SwBankRef>>(banks, "sw_banks", {});
    v.drum_banks = getOr<std::vector<DrumBankRef>>(banks, "drum_banks", {});
    v.scc_wave_banks = getOr<std::vector<SccWaveBankRef>>(banks, "scc_wave_banks", {});
    v.pcm_banks = getOr<std::vector<PcmBankRef>>(banks, "pcm_banks", {});

    v.extra = nlohmann::json::object();
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!isManagedKey(it.key())) v.extra[it.key()] = it.value();
        }
    }
}

} // namespace fpe
