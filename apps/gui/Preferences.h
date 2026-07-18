#pragma once
#include <filesystem>
#include <string>

// Per-user, per-machine editor settings - deliberately NOT part of
// fpe::PatchWorkspace/fpe_data (this is GUI-only runtime configuration,
// not FITOM_X profile data) and NOT under version control (see
// preferencesFilePath(): lives next to the executable, inside the
// gitignored build/ tree - same reasoning as CLAUDE.md's "don't commit
// machine-specific settings" for this repo's own multi-machine workflow;
// D-020 changed this from an OS user-config directory to alongside the
// exe, per explicit user request).
struct Preferences {
    std::string profileFolder;       // FileBrowser's initial directory when opened from the menu; empty = CWD
    bool autoLoadEnabled = false;    // load autoLoadProfilePath automatically on startup (no argv[1] given)
    std::string autoLoadProfilePath;
    int midiPortIndex = -1;          // RtMidi output port index for the preview fallback; -1 = disabled
    int midiChannel = 0;             // 0-15, used for both the FITOM_X pipe and the RtMidi fallback
};

// Resolves the preferences JSON file's path: <exe's own directory>/
// FITOM_patch_editor.preference.json (fixed filename, D-020). The exe's
// directory is found via the OS's "path to the running executable" API
// (Windows: GetModuleFileNameW) rather than the current working directory,
// so it resolves correctly even if launched with a different CWD (e.g. a
// shortcut with a different "start in" folder). Falls back to the current
// working directory if the executable's own path can't be determined
// (non-Windows - untested, see docs/STATUS.md) or on POSIX, where this
// isn't yet implemented.
std::filesystem::path preferencesFilePath();

// Reads preferencesFilePath(); returns default-constructed Preferences
// (not an error) if the file doesn't exist yet or fails to parse - this is
// "no preferences saved yet", not a load failure to surface to the user.
Preferences loadPreferences();

// Writes prefs to preferencesFilePath(), creating the parent directory if
// needed. Returns false on write failure (caller should show this as an
// error - unlike loading, a failed save is worth telling the user about).
bool savePreferences(const Preferences& prefs);
