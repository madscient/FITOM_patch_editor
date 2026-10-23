// fpe_roundtrip_check <profile.json> <out-dir>
//
// Loads a real profile, saveAs()-es the whole tree into <out-dir>, and
// compares every rewritten file with its original. Reports each key whose
// non-default value disappeared or changed, and exits 1 if there was any.
// Run it against real data (e.g. FITOM_staging) after FITOM_X changes a
// schema: it catches fields this library doesn't model yet.
//
// The originals are never written: the output mirrors the original absolute
// layout under <out-dir>, so profile-relative references such as
// "../../banks/x" still land inside <out-dir>. Every destination is checked
// before anything is written, and the originals are compared byte-for-byte
// afterwards.
//
// "Default" is a heuristic (0 / false / "" / null, rhythm_ch 255, sw_* -1,
// or an object made only of those). Omitting such keys is expected - the
// hwbank writer prunes fields the chip never reads (D-057).

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fpe/PatchWorkspace.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string readBytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

json readJson(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return json();
    try {
        json j;
        in >> j;
        return j;
    } catch (...) {
        return json();
    }
}

bool isDefault(const std::string& key, const json& v) {
    if (v.is_object()) {
        for (auto it = v.begin(); it != v.end(); ++it)
            if (!isDefault(it.key(), it.value())) return false;
        return true;
    }
    return v.is_null() || (v.is_number() && v.get<double>() == 0) || (v.is_boolean() && !v.get<bool>()) ||
           (v.is_string() && v.get<std::string>().empty()) || (key == "rhythm_ch" && v == 255) ||
           ((key == "sw_bank" || key == "sw_prog") && v == -1);
}

struct Findings {
    std::map<std::string, int> lost;    // non-default value gone
    std::map<std::string, int> changed; // value differs
    int omittedDefaults = 0;
};

void compare(const json& a, const json& b, const std::string& path, Findings& f) {
    if (a.is_object()) {
        for (auto it = a.begin(); it != a.end(); ++it) {
            const std::string p = path + "." + it.key();
            if (!b.is_object() || !b.contains(it.key())) {
                if (isDefault(it.key(), it.value())) f.omittedDefaults++;
                else f.lost[p]++;
                continue;
            }
            compare(it.value(), b.at(it.key()), p, f);
        }
    } else if (a.is_array()) {
        for (size_t i = 0; i < a.size(); ++i) {
            if (!b.is_array() || i >= b.size()) {
                f.lost[path + "[]"]++;
                continue;
            }
            compare(a[i], b[i], path + "[]", f);
        }
    } else if (a != b && !(a.is_number() && b.is_number() && a.get<double>() == b.get<double>())) {
        f.changed[path]++;
    }
}

bool isInside(const fs::path& p, const fs::path& dir) {
    const fs::path np = p.lexically_normal();
    const fs::path nd = dir.lexically_normal();
    auto pi = np.begin();
    for (auto di = nd.begin(); di != nd.end(); ++di, ++pi) {
        if (di->empty()) continue; // trailing separator
        if (pi == np.end() || *pi != *di) return false;
    }
    return true;
}

// Where PatchWorkspace::saveAs() will put `file` (rebaseSourceFiles() keeps
// each path relative to the profile's directory). Empty if saveAs() would
// leave it where it is - which here means "would overwrite an original".
fs::path destinationOf(const fs::path& file, const fs::path& oldRoot, const fs::path& newRoot) {
    std::error_code ec;
    const fs::path rel = fs::relative(file, oldRoot, ec);
    if (ec || rel.empty()) return {};
    return newRoot / rel;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: fpe_roundtrip_check <profile.json> <out-dir>\n");
        return 2;
    }
    std::error_code ec;
    const fs::path profilePath = fs::absolute(argv[1], ec).lexically_normal();
    const fs::path outDir = fs::absolute(argv[2], ec).lexically_normal();
    if (fs::exists(outDir) && !fs::is_empty(outDir)) {
        std::fprintf(stderr, "out-dir must be empty or absent: %s\n", outDir.string().c_str());
        return 2;
    }
    const fs::path oldRoot = profilePath.parent_path();
    const fs::path newRoot = outDir / oldRoot.relative_path();
    const fs::path newProfile = newRoot / profilePath.filename();

    fpe::PatchWorkspace ws;
    try {
        ws.load(profilePath);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "load failed: %s\n", e.what());
        return 2;
    }
    std::printf("profile: %s\nload warnings: %zu\n", profilePath.string().c_str(), ws.warnings().size());
    for (const auto& w : ws.warnings()) std::printf("  %s\n", w.c_str());

    // Every file save() might write, original -> destination.
    std::vector<std::pair<fs::path, fs::path>> files;
    files.emplace_back(profilePath, newProfile);
    auto add = [&](const fs::path& f) { files.emplace_back(f, destinationOf(f, oldRoot, newRoot)); };
    for (auto& b : ws.layeredPatchBanks()) add(b.sourceFile);
    for (auto& b : ws.performanceBanks()) add(b.sourceFile);
    for (auto& b : ws.deviceBanks()) add(b.sourceFile);
    for (auto& b : ws.sampleZoneBanks()) add(b.sourceFile);
    for (auto& b : ws.pcmBanks()) {
        add(b.sourceFile);
        for (const std::string& side : {b.adpcm_json, b.bin_file}) {
            if (side.empty() || fs::path(side).is_absolute()) continue;
            const fs::path dest = destinationOf(b.sourceFile, oldRoot, newRoot);
            files.emplace_back(b.sourceFile.parent_path() / side, dest.empty() ? fs::path() : dest.parent_path() / side);
        }
    }
    for (auto& b : ws.sccWaveBanks()) add(b.sourceFile);
    for (auto& k : ws.drumKits()) add(k.sourceFile);
    if (!ws.profile().bank_overrides.externalFile.empty())
        add(oldRoot / ws.profile().bank_overrides.externalFile);

    for (const auto& [from, to] : files) {
        if (to.empty() || !isInside(to, outDir)) {
            std::fprintf(stderr, "refusing to run: %s would not be written inside out-dir\n", from.string().c_str());
            return 2;
        }
    }

    std::map<fs::path, std::string> originalBytes;
    for (const auto& [from, to] : files) originalBytes[from] = readBytes(from);

    try {
        ws.saveAs(newProfile);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "saveAs failed: %s\n", e.what());
        return 2;
    }

    bool originalsTouched = false;
    for (const auto& [from, bytes] : originalBytes) {
        if (readBytes(from) != bytes) {
            std::fprintf(stderr, "ORIGINAL MODIFIED: %s\n", from.string().c_str());
            originalsTouched = true;
        }
    }

    Findings f;
    std::map<fs::path, bool> seen;
    for (const auto& [from, to] : files) {
        if (seen[from] || from.extension() != ".json") continue;
        seen[from] = true;
        json a = readJson(from);
        json b = readJson(to);
        // saveAs() re-points an external "banks" at the same shared file from
        // the new location (D-062); the string differs, the target must not.
        if (from == profilePath && a.contains("banks") && a["banks"].is_string() && b.contains("banks") &&
            b["banks"].is_string()) {
            std::error_code eq;
            if (fs::equivalent(oldRoot / a["banks"].get<std::string>(), newRoot / b["banks"].get<std::string>(), eq))
                b["banks"] = a["banks"];
        }
        Findings one;
        compare(a, b, "", one);
        const std::string name = from.filename().string();
        const std::string kind = name.substr(name.find('.'));
        for (auto& [k, n] : one.lost) f.lost[kind + " " + k] += n;
        for (auto& [k, n] : one.changed) f.changed[kind + " " + k] += n;
        f.omittedDefaults += one.omittedDefaults;
    }

    std::printf("files compared: %zu\nomitted default-valued keys: %d\n", seen.size(), f.omittedDefaults);
    for (auto& [k, n] : f.lost) std::printf("LOST     %7d  %s\n", n, k.c_str());
    for (auto& [k, n] : f.changed) std::printf("CHANGED  %7d  %s\n", n, k.c_str());
    if (originalsTouched) return 3;
    return (f.lost.empty() && f.changed.empty()) ? 0 : 1;
}
