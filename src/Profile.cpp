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

namespace {
constexpr const char* kManagedKeys[] = {
    "profile_name", "hw_banks", "patch_banks", "sw_banks", "drum_banks",
};
bool isManagedKey(const std::string& key) {
    for (auto k : kManagedKeys) if (key == k) return true;
    return false;
}
} // namespace

void to_json(nlohmann::json& j, const Profile& v) {
    j = v.extra.is_object() ? v.extra : nlohmann::json::object();
    j["profile_name"] = v.profile_name;
    if (!v.hw_banks.empty() || j.contains("hw_banks")) j["hw_banks"] = v.hw_banks;
    if (!v.patch_banks.empty() || j.contains("patch_banks")) j["patch_banks"] = v.patch_banks;
    if (!v.sw_banks.empty() || j.contains("sw_banks")) j["sw_banks"] = v.sw_banks;
    if (!v.drum_banks.empty() || j.contains("drum_banks")) j["drum_banks"] = v.drum_banks;
}
void from_json(const nlohmann::json& j, Profile& v) {
    v.profile_name = getOr<std::string>(j, "profile_name", "");
    v.hw_banks = getOr<std::vector<HwBankRef>>(j, "hw_banks", {});
    v.patch_banks = getOr<std::vector<PatchBankRef>>(j, "patch_banks", {});
    v.sw_banks = getOr<std::vector<SwBankRef>>(j, "sw_banks", {});
    v.drum_banks = getOr<std::vector<DrumBankRef>>(j, "drum_banks", {});

    v.extra = nlohmann::json::object();
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!isManagedKey(it.key())) v.extra[it.key()] = it.value();
        }
    }
}

} // namespace fpe
