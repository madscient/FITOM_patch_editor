#include "Preferences.h"

#include <fstream>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

// to_json/from_json must be found via ADL from the same namespace as
// Preferences itself (global namespace here) - nlohmann's adl_serializer
// calls them unqualified. Wrapping them in an anonymous namespace (as
// tried initially) makes MSVC's ADL fail to find them at the get<>()/
// implicit-conversion call sites below, so they live at true global scope.
void to_json(nlohmann::json& j, const Preferences& p) {
    j = nlohmann::json{
        {"profile_folder", p.profileFolder},
        {"auto_load_enabled", p.autoLoadEnabled},
        {"auto_load_profile_path", p.autoLoadProfilePath},
        {"midi_port_index", p.midiPortIndex},
        {"midi_channel", p.midiChannel},
    };
}

void from_json(const nlohmann::json& j, Preferences& p) {
    p.profileFolder = j.value("profile_folder", std::string());
    p.autoLoadEnabled = j.value("auto_load_enabled", false);
    p.autoLoadProfilePath = j.value("auto_load_profile_path", std::string());
    p.midiPortIndex = j.value("midi_port_index", -1);
    p.midiChannel = j.value("midi_channel", 0);
}

namespace {

// The directory the running executable itself lives in - not the current
// working directory, so this resolves correctly even when launched from a
// shortcut/CLI with a different CWD (D-020).
fs::path exeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return fs::path(buf).parent_path();
    }
#endif
    return fs::path(); // not resolved (POSIX untested, or the API call failed) - caller falls back to CWD
}

} // namespace

fs::path preferencesFilePath() {
    fs::path dir = exeDir();
    if (dir.empty()) dir = fs::current_path();
    return dir / "FITOM_patch_editor.preference.json";
}

Preferences loadPreferences() {
    Preferences prefs;
    std::ifstream in(preferencesFilePath(), std::ios::binary);
    if (!in) return prefs; // not saved yet - defaults are fine
    try {
        nlohmann::json j;
        in >> j;
        prefs = j.get<Preferences>();
    } catch (const nlohmann::json::exception&) {
        return Preferences{}; // corrupt/unreadable file - fall back to defaults rather than fail startup
    }
    return prefs;
}

bool savePreferences(const Preferences& prefs) {
    const fs::path path = preferencesFilePath();
    std::error_code ec;
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    nlohmann::json j = prefs;
    out << j.dump(2) << '\n';
    return static_cast<bool>(out);
}
