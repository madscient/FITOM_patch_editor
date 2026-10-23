#include "fpe/SccWave.h"

#include <cctype>
#include <cstdio>

#include "fpe/JsonUtil.h"

namespace fpe {
using json_util::getOr;

namespace {

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::vector<int> decodeHex(const std::string& hex) {
    std::vector<int> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int hi = hexDigit(hex[i]);
        const int lo = hexDigit(hex[i + 1]);
        if (hi < 0 || lo < 0) break;
        out.push_back(static_cast<int8_t>(static_cast<uint8_t>(hi * 16 + lo)));
    }
    return out;
}

std::string encodeHex(const std::vector<int>& data) {
    std::string out;
    char buf[3];
    for (int v : data) {
        std::snprintf(buf, sizeof(buf), "%02X", static_cast<unsigned>(static_cast<uint8_t>(v)));
        out += buf;
    }
    return out;
}

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

} // namespace

void to_json(nlohmann::json& j, const SccWave& v) {
    j = nlohmann::json{{"wave_no", v.wave_no}, {"name", v.name}};
    if (v.hexData.empty()) {
        j["data"] = v.data;
    } else {
        const std::string encoded = encodeHex(v.data);
        j["data"] = equalsIgnoreCase(encoded, v.hexData) ? v.hexData : encoded;
    }
}
void from_json(const nlohmann::json& j, SccWave& v) {
    v.wave_no = getOr<int>(j, "wave_no", -1);
    v.name = getOr<std::string>(j, "name", "");
    v.data.clear();
    v.hexData.clear();
    if (j.is_object() && j.contains("data")) {
        const auto& d = j.at("data");
        if (d.is_array()) {
            v.data = d.get<std::vector<int>>();
        } else if (d.is_string()) {
            v.hexData = d.get<std::string>();
            v.data = decodeHex(v.hexData);
        }
    }
}

SccWave* SccWaveBank::findByWaveNo(int waveNo) {
    for (auto& w : waves) if (w.wave_no == waveNo) return &w;
    return nullptr;
}
const SccWave* SccWaveBank::findByWaveNo(int waveNo) const {
    for (auto& w : waves) if (w.wave_no == waveNo) return &w;
    return nullptr;
}

void to_json(nlohmann::json& j, const SccWaveBank& v) {
    j = nlohmann::json{{"name", v.name}, {"waves", v.waves}};
    json_util::mergeUnknownKeys(j, v.extra);
}
void from_json(const nlohmann::json& j, SccWaveBank& v) {
    v.name = getOr<std::string>(j, "name", "");
    v.waves = getOr<std::vector<SccWave>>(j, "waves", {});
    v.extra = json_util::unknownKeys(j, {"name", "waves"});
}

} // namespace fpe
