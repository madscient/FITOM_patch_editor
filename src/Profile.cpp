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
    if (!v.group.empty()) j["group"] = v.group;
    if (!v.chip.empty()) j["chip"] = v.chip;
    if (v.offsets_only) j["offsets_only"] = v.offsets_only;
}
void from_json(const nlohmann::json& j, PcmBankRef& v) {
    v.bank = getOr<int>(j, "bank", 0);
    v.file = getRequired<std::string>(j, "file", "pcm_banks[]");
    v.name = getOr<std::string>(j, "name", "");
    v.group = getOr<std::string>(j, "group", "");
    v.chip = getOr<std::string>(j, "chip", "");
    v.offsets_only = getOr<bool>(j, "offsets_only", false);
}

void to_json(nlohmann::json& j, const BanksObject& v) {
    j = nlohmann::json::object();
    if (!v.hw_banks.empty()) j["hw_banks"] = v.hw_banks;
    if (!v.patch_banks.empty()) j["patch_banks"] = v.patch_banks;
    if (!v.sw_banks.empty()) j["sw_banks"] = v.sw_banks;
    if (!v.drum_banks.empty()) j["drum_banks"] = v.drum_banks;
    if (!v.scc_wave_banks.empty()) j["scc_wave_banks"] = v.scc_wave_banks;
    if (!v.pcm_banks.empty()) j["pcm_banks"] = v.pcm_banks;
    if (v.sf2_banks.is_array() && !v.sf2_banks.empty()) j["sf2_banks"] = v.sf2_banks;
}
void from_json(const nlohmann::json& j, BanksObject& v) {
    v.hw_banks = getOr<std::vector<HwBankRef>>(j, "hw_banks", {});
    v.patch_banks = getOr<std::vector<PatchBankRef>>(j, "patch_banks", {});
    v.sw_banks = getOr<std::vector<SwBankRef>>(j, "sw_banks", {});
    v.drum_banks = getOr<std::vector<DrumBankRef>>(j, "drum_banks", {});
    v.scc_wave_banks = getOr<std::vector<SccWaveBankRef>>(j, "scc_wave_banks", {});
    v.pcm_banks = getOr<std::vector<PcmBankRef>>(j, "pcm_banks", {});
    v.sf2_banks = getOr<nlohmann::json>(j, "sf2_banks", nlohmann::json::array());
}

// See BanksSource's comment in Profile.h re: why the string case leaves
// `data` empty here rather than loading the file itself.
void to_json(nlohmann::json& j, const BanksSource& v) {
    if (!v.externalFile.empty()) {
        j = v.externalFile;
    } else {
        j = v.data;
    }
}
void from_json(const nlohmann::json& j, BanksSource& v) {
    v.present = true;
    if (j.is_string()) {
        v.externalFile = j.get<std::string>();
    } else if (j.is_object()) {
        v.data = j.get<BanksObject>();
    }
    // Anything else doesn't match the schema's oneOf[string, banksObject] -
    // leave both externalFile/data empty, same effective result as absent.
}

namespace {
// All bank-registry arrays live nested under "banks"/"bank_overrides" on
// disk (confirmed against the real profile.schema.json - see NOTE in
// Profile.h); only "profile_name" and these two are managed at the top
// level.
constexpr const char* kManagedKeys[] = {"profile_name", "banks", "bank_overrides"};
bool isManagedKey(const std::string& key) {
    for (auto k : kManagedKeys) if (key == k) return true;
    return false;
}
} // namespace

void to_json(nlohmann::json& j, const Profile& v) {
    j = v.extra.is_object() ? v.extra : nlohmann::json::object();
    j["profile_name"] = v.profile_name;

    // A present-but-nothing-to-say BanksSource (inline and empty) is
    // omitted entirely, matching this key's pre-D-041 behavior of omitting
    // "banks" altogether for a profile with no banks at all. An external
    // reference is always written (the string alone is meaningful even if
    // PatchWorkspace never resolved it, e.g. a load warning).
    auto writeIfMeaningful = [&](const char* key, const BanksSource& src) {
        if (!src.present) return;
        if (src.externalFile.empty() && src.data.empty()) return;
        j[key] = src;
    };
    writeIfMeaningful("banks", v.banks);
    writeIfMeaningful("bank_overrides", v.bank_overrides);
}
void from_json(const nlohmann::json& j, Profile& v) {
    v.profile_name = getOr<std::string>(j, "profile_name", "");

    v.banks = BanksSource{};
    v.bank_overrides = BanksSource{};
    if (j.is_object() && j.contains("banks")) v.banks = j.at("banks").get<BanksSource>();
    if (j.is_object() && j.contains("bank_overrides")) v.bank_overrides = j.at("bank_overrides").get<BanksSource>();

    // The effective (merged) registry is filled in by PatchWorkspace::load()
    // after resolving any external banks/bank_overrides file - see
    // Profile.h's comment on these members. Left empty here.

    v.extra = nlohmann::json::object();
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!isManagedKey(it.key())) v.extra[it.key()] = it.value();
        }
    }
}

} // namespace fpe
