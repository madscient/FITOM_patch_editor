// FITOM_X Patch Editor - GUI shell.
//
// Owns the window/OpenGL context and the Dear ImGui context, and runs the
// main render loop. Implements the first slice of the patch browser UI on
// top of fpe::PatchWorkspace (see include/fpe/PatchWorkspace.h):
//
//   MainMenu -> (load profile) -> FileBrowser -> (pick *.profile.json)
//     -> load succeeds -> Outline (read-only tree of the loaded profile)
//     -> load fails    -> error popup, stays on FileBrowser
//
// If a profile path is given as argv[1], that profile is loaded up front
// and the app starts directly on Outline (as if it had just been picked
// from FileBrowser), skipping MainMenu/FileBrowser entirely. This is for
// launching from an already-running FITOM_X instance, which knows which
// profile it currently has loaded and can hand that path straight to the
// editor. On load failure, falls back to the normal MainMenu + error
// popup (see main()).
//
// Outline also has a "新規バンク作成" button (renderNewBankDialog()) that
// creates a new native/device/performance bank or drum kit - bank
// index/prog is auto-assigned (one past the current max), and the file's
// directory+suffix are auto-derived from the chosen bank type
// (buildRelativeBankFile()). Saves immediately via PatchWorkspace::save()
// so a real skeleton file appears on disk right away.
//
// Selecting a Device (HwPatch) patch from BankDetail opens a modeless
// patch editor (renderPatchEditors()/renderPatchEditor()) - several can be
// open at once, independent of AppState/the current screen. Each editor
// has per-operator envelope curves that redraw live as AR/DR/SL/RR/TL
// change, and a clickable preview keyboard (3 octaves, with CC#1
// modulation / CC#7 volume levers to its left) that plays notes through
// FITOM_X's internal MIDI pipe (MidiPipeClient, see
// docs/plugin-midi-pipe.md in the FITOM_X repo) when an instance is
// running, falling back to a real MIDI output port (RtMidi, configured via
// the "プリファレンス" dialog) otherwise - see PreviewOutput and
// docs/DESIGN.md D-018 (this superseded D-015's original "no fallback"
// decision). Field sliders use each chip
// family's confirmed register width where known (OPN/OPN2 so far - see
// getVoiceFieldRanges()/getOpFieldRanges(), D-016) and grey out fields the
// chip doesn't read; other chip families still fall back to a generic
// 0-99 range until similarly confirmed. OPN/OPN2 also show the ALG
// connection-diagram image for the current algorithm (assets/alg_diagrams,
// D-016). Native/Performance/Drum patch editors are future work.
//
// "新規プロファイル作成"/"プロファイル削除" are shown in the main menu
// but intentionally left disabled - not implemented yet. Per-parameter
// patch editing forms and the virtual MIDI controller are also still
// future work (see docs/STATUS.md).
//
// Backend: GLFW (window/input) + OpenGL3 (rendering) + GLEW (GL function
// loading). All three, plus Dear ImGui itself and nlohmann/json, are
// resolved via vcpkg (see vcpkg.json / CMakePresets.json) - there is no
// vendored/submoduled third-party source in this repository.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// GLEW must be included before any other header that may pull in the
// platform's own (older) OpenGL headers - including GLFW's.
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <nlohmann/json.hpp>

#include "BmpLoader.h"
#include "Preferences.h"
#include "PreviewOutput.h"
#include "fpe/PatchWorkspace.h"
#include "fpe/VoicePatchType.h"

namespace fs = std::filesystem;

namespace {

void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

// Dear ImGui's built-in font only covers Basic Latin; this project's UI
// text is Japanese, so without a CJK-capable font every label renders as
// tofu ("?") boxes. Try a few common system fonts; if none are found,
// fall back to the default font (tofu) rather than failing to start.
void loadFonts(ImGuiIO& io) {
    static const char* candidates[] = {
#ifdef _WIN32
        "C:\\Windows\\Fonts\\meiryo.ttc",
        "C:\\Windows\\Fonts\\YuGothM.ttc",
        "C:\\Windows\\Fonts\\msgothic.ttc",
#else
        "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJKjp-Regular.otf",
#endif
    };
    for (const char* path : candidates) {
        if (io.Fonts->AddFontFromFileTTF(path, 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese())) {
            return;
        }
    }
    std::fprintf(stderr, "warning: no CJK-capable font found; Japanese UI text will render as tofu boxes\n");
}

enum class AppState { MainMenu, FileBrowser, Outline, BankDetail };

// Which of PatchWorkspace's five browse-tree vectors a BankDetail screen is
// currently showing (see AppContext::selectedIndex).
enum class BankCategory { Native, Performance, Device, SampleZone, Pcm, Drum };

// Matches both "<name>.profile.json" (the naming convention used by
// production profiles) and a bare "profile.json" (used by fixtures/ and
// presumably valid too - it's just ".profile.json" with an empty <name>
// prefix). Shared by FileBrowserState and PathPickerState below so both
// in-app browsers agree on what counts as a profile file.
bool isProfileFileName(const std::string& name) {
    const std::string suffix = ".profile.json";
    return name == "profile.json" ||
           (name.size() > suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0);
}

// Lists *.profile.json files (and subdirectories, for navigation) in one
// directory. Re-scanned via refresh() whenever the current directory
// changes - not re-scanned every frame.
struct FileBrowserState {
    fs::path currentDir;
    std::vector<fs::path> subdirs;
    std::vector<fs::path> profileFiles;
    char pathInput[1024] = {};
    std::string listError;

    void refresh() {
        subdirs.clear();
        profileFiles.clear();
        listError.clear();
        try {
            for (const auto& entry : fs::directory_iterator(currentDir, fs::directory_options::skip_permission_denied)) {
                const auto& p = entry.path();
                std::error_code ec;
                if (entry.is_directory(ec) && !ec) {
                    subdirs.push_back(p);
                } else if (entry.is_regular_file(ec) && !ec && isProfileFileName(p.filename().string())) {
                    profileFiles.push_back(p);
                }
            }
        } catch (const fs::filesystem_error& e) {
            listError = e.what();
        }
        std::sort(subdirs.begin(), subdirs.end());
        std::sort(profileFiles.begin(), profileFiles.end());
        std::snprintf(pathInput, sizeof(pathInput), "%s", currentDir.string().c_str());
    }

    void setDir(const fs::path& dir) {
        std::error_code ec;
        fs::path canon = fs::weakly_canonical(dir, ec);
        currentDir = (ec || canon.empty()) ? dir : canon;
        refresh();
    }
};

// A small in-app directory/file browser, reusable anywhere the UI needs a
// folder or *.profile.json path text field with a trailing "参照..."
// (browse) button - this is meant to be the project-wide convention for
// every such field (see docs/DESIGN.md D-019), rather than a native OS file
// dialog (no such library is a dependency of this project - see D-006 on
// vcpkg-only deps). One instance lives on AppContext and is repointed at
// whichever text buffer is currently being browsed for (see
// openPathPicker()) - only one such picker can be open at a time, which is
// fine since it's always opened modally.
//
// IMPORTANT: renderPathPicker() must be called from *inside* the calling
// dialog's own BeginPopupModal/EndPopup block (nested, "stacked modals"
// style), never as a sibling call after that block's EndPopup() has
// already run - otherwise its OpenPopup()/BeginPopupModal() resolve in the
// wrong ID-stack context and silently fail to open, leaving the caller's
// modal stuck open-but-blocking with nothing visibly rendered. See
// renderPreferencesDialog() for the correct call site.
struct PathPickerState {
    bool open = false;
    bool pickFolder = false; // true: OK confirms currentDir itself; false: pick a *.profile.json file
    fs::path currentDir;
    std::vector<fs::path> subdirs;
    std::vector<fs::path> profileFiles; // only populated/shown when !pickFolder
    char pathInput[1024] = {};
    std::string listError;
    char* target = nullptr; // caller-owned buffer that OK/double-click writes the picked path into
    size_t targetSize = 0;

    void refresh() {
        subdirs.clear();
        profileFiles.clear();
        listError.clear();
        try {
            for (const auto& entry : fs::directory_iterator(currentDir, fs::directory_options::skip_permission_denied)) {
                const auto& p = entry.path();
                std::error_code ec;
                if (entry.is_directory(ec) && !ec) {
                    subdirs.push_back(p);
                } else if (!pickFolder && entry.is_regular_file(ec) && !ec && isProfileFileName(p.filename().string())) {
                    profileFiles.push_back(p);
                }
            }
        } catch (const fs::filesystem_error& e) {
            listError = e.what();
        }
        std::sort(subdirs.begin(), subdirs.end());
        std::sort(profileFiles.begin(), profileFiles.end());
        std::snprintf(pathInput, sizeof(pathInput), "%s", currentDir.string().c_str());
    }

    void setDir(const fs::path& dir) {
        std::error_code ec;
        fs::path canon = fs::weakly_canonical(dir, ec);
        currentDir = (ec || canon.empty()) ? dir : canon;
        refresh();
    }
};

// Chip families selectable when creating a new Device (HwBank) bank.
// Deliberately excludes AWM/ADPCM-B(Y8950)/ADPCM-B/ADPCM-A/PCM-D8 (those
// need a SampleZoneBank/PcmBank instead - see fpe::isSampleBasedVoicePatchType
// / fpe::isPcmWaveformVoicePatchType, D-013) and SD1/MA3/MA5/MA7 (recognized
// by FITOM_X but no implemented chip driver, per VoicePatchType.h) - none of
// those are what "create a new hardware bank" should produce.
struct DeviceGroupOption {
    fpe::VoicePatchType type;
    const char* label;
};
constexpr DeviceGroupOption kCreatableDeviceGroups[] = {
    {fpe::VoicePatchType::OPN, "OPN"},         {fpe::VoicePatchType::OPN2, "OPN2"},
    {fpe::VoicePatchType::OPM, "OPM"},         {fpe::VoicePatchType::OPZ, "OPZ"},
    {fpe::VoicePatchType::OPZ2, "OPZ2"},       {fpe::VoicePatchType::OPL, "OPL"},
    {fpe::VoicePatchType::OPL2, "OPL2"},       {fpe::VoicePatchType::OPL3_2, "OPL3_2"},
    {fpe::VoicePatchType::OPL_RHY, "OPL_RHY"}, {fpe::VoicePatchType::OPLL, "OPLL"},
    {fpe::VoicePatchType::OPLLP, "OPLLP"},     {fpe::VoicePatchType::OPLLX, "OPLLX"},
    {fpe::VoicePatchType::VRC7, "VRC7"},       {fpe::VoicePatchType::OPL3, "OPL3"},
    {fpe::VoicePatchType::SSG, "SSG"},         {fpe::VoicePatchType::EPSG, "EPSG"},
    {fpe::VoicePatchType::DCSG, "DCSG"},       {fpe::VoicePatchType::SAA, "SAA"},
    {fpe::VoicePatchType::SCC, "SCC"},
};

// The four bank types the "新規バンク作成" dialog can produce (matches
// PatchWorkspace's createNativePatchBank/createDeviceBank/
// createPerformanceBank/createDrumKit). SampleZoneBank/PcmBank are
// deliberately not offered here - see kCreatableDeviceGroups above.
enum class NewBankType { Native, Device, Performance, Drum };

struct NewBankDialogState {
    bool open = false;
    NewBankType type = NewBankType::Native;
    char name[128] = {};
    char fileStem[128] = {}; // just the base name - postfix/extension are auto-generated (see buildRelativeBankFile())
    int deviceGroupIndex = 0; // index into kCreatableDeviceGroups, only used when type == Device
    int drumKitTypeIndex = 0; // 0 = routed, 1 = direct; only used when type == Drum
    std::string errorMessage; // validation/creation failure, shown inline in the dialog
};

// A single modeless "patch editor" window (renderPatchEditor()). Several
// can be open at once (see AppContext::openEditors) - selecting a patch
// from BankDetail opens one rather than replacing the current screen.
// Scoped to Device (HwPatch) patches only for now; Native/Performance/Drum
// editors are future work (see docs/DESIGN.md D-015).
struct PatchEditorWindow {
    int id = 0;
    bool open = true;
    size_t bankIndex = 0; // index into ws.deviceBanks()
    int prog = 0;         // HwPatch::prog within that bank
    int heldNote = -1;    // preview keyboard note currently held down, -1 if none
    int ccMod = 0;        // CC#1 lever position (0-127), local to this editor's preview channel
    int ccVolume = 100;   // CC#7 lever position (0-127), GM default volume
};

// Editable working copy shown by renderPreferencesDialog() - only written
// back to AppContext::preferences (and disk, via savePreferences()) on OK,
// so Cancel discards any in-progress edits cleanly. Populated fresh from
// the current Preferences + a live RtMidi port scan each time the dialog
// is opened (see openPreferencesDialog()).
struct PreferencesDialogState {
    bool open = false;
    char profileFolder[1024] = {};
    bool autoLoadEnabled = false;
    char autoLoadProfilePath[1024] = {};
    int midiPortIndex = -1; // -1 = "(なし)", otherwise an index into `midiPorts`
    int midiChannel = 0;
    std::vector<std::string> midiPorts; // snapshot from PreviewOutput::listRtMidiPorts() at dialog-open time
    std::string errorMessage;           // e.g. save failure, shown inline
};

struct AppContext {
    fpe::PatchWorkspace workspace;
    AppState state = AppState::MainMenu;
    FileBrowserState browser;
    std::string errorMessage; // non-empty => error popup is showing
    NewBankDialogState newBankDialog;
    std::vector<PatchEditorWindow> openEditors;
    int nextEditorId = 0;
    // One shared preview output (FITOM_X's internal MIDI pipe, falling
    // back to a regular MIDI port via RtMidi - see PreviewOutput,
    // docs/DESIGN.md D-018), reused by every open patch editor's preview
    // keyboard. The pipe only allows a single client anyway
    // (docs/plugin-midi-pipe.md in the FITOM_X repo).
    PreviewOutput previewOutput;
    Preferences preferences;
    PreferencesDialogState preferencesDialog;
    PathPickerState pathPicker; // shared browse-button popup, see PathPickerState/openPathPicker()

    // Selection driving the BankDetail screen - which category/index into
    // the corresponding PatchWorkspace vector. Only meaningful while
    // state == BankDetail; set together with the state transition, and
    // never touched by workspace-mutating code, so it can't go stale
    // within a single load (this GUI is still read-only, see file header).
    BankCategory selectedCategory = BankCategory::Native;
    size_t selectedIndex = 0;
};

void tryLoadProfile(AppContext& ctx, const fs::path& file) {
    fpe::PatchWorkspace newWorkspace;
    try {
        newWorkspace.load(file);
    } catch (const std::exception& e) {
        ctx.errorMessage = "読み込みに失敗しました:\n" + file.string() + "\n\n" + e.what();
        return;
    }
    ctx.workspace = std::move(newWorkspace);
    ctx.state = AppState::Outline;
}

void selectBank(AppContext& ctx, BankCategory category, size_t index) {
    ctx.selectedCategory = category;
    ctx.selectedIndex = index;
    ctx.state = AppState::BankDetail;
}

// Opens a modeless editor for the HwPatch at ws.deviceBanks()[bankIndex]'s
// `prog`, or just re-focuses (marks open again) an already-open one for the
// same patch rather than creating a duplicate window.
void openPatchEditor(AppContext& ctx, size_t bankIndex, int prog) {
    for (auto& e : ctx.openEditors) {
        if (e.bankIndex == bankIndex && e.prog == prog) {
            e.open = true;
            ImGui::SetWindowFocus((std::string("パッチ編集##editor") + std::to_string(e.id)).c_str());
            return;
        }
    }
    PatchEditorWindow w;
    w.id = ctx.nextEditorId++;
    w.bankIndex = bankIndex;
    w.prog = prog;
    ctx.openEditors.push_back(w);
}

// Auto-assigns a new bank's index/prog as one past the highest already in
// use (0 if the vector is empty), so the "新規バンク作成" dialog doesn't
// need to ask for it separately.
template <typename BankT>
int nextBankIndex(const std::vector<BankT>& banks) {
    int maxIdx = -1;
    for (const auto& b : banks) maxIdx = std::max(maxIdx, b.bankIndex);
    return maxIdx + 1;
}

int nextDeviceBankIndex(fpe::PatchWorkspace& ws, fpe::VoicePatchType group) {
    int maxIdx = -1;
    for (const auto& b : ws.deviceBanks()) {
        if (b.voicePatchType == group) maxIdx = std::max(maxIdx, b.bankIndex);
    }
    return maxIdx + 1;
}

int nextDrumProg(const std::vector<fpe::DrumKit>& kits) {
    int maxProg = -1;
    for (const auto& k : kits) maxProg = std::max(maxProg, k.prog);
    return maxProg + 1;
}

// Builds the relative `file` path a new bank will be saved to: a fixed
// per-type directory + the user-entered stem + the on-disk suffix that
// PatchWorkspace/FITOM_X expect for that bank type (matches the layout
// already used by fixtures/ and real FITOM_staging profiles).
std::string buildRelativeBankFile(const NewBankDialogState& d) {
    const std::string stem = d.fileStem;
    switch (d.type) {
        case NewBankType::Native:
            return "patches/" + stem + ".patchbank.json";
        case NewBankType::Performance:
            return "sw/" + stem + ".swbank.json";
        case NewBankType::Device:
            return std::string("banks/") + kCreatableDeviceGroups[d.deviceGroupIndex].label + "/" + stem +
                   ".hwbank.json";
        case NewBankType::Drum:
            return "drums/" + stem + ".drumkit.json";
    }
    return stem;
}

// Creates the bank via PatchWorkspace's existing CRUD API, then saves
// immediately so a real skeleton file actually appears on disk (matching
// "作成し...表示" - not left dangling in memory until some future,
// not-yet-implemented explicit Save button). On failure, leaves the
// dialog's fields untouched and sets d.errorMessage for inline display.
bool tryCreateBank(AppContext& ctx) {
    NewBankDialogState& d = ctx.newBankDialog;
    fpe::PatchWorkspace& ws = ctx.workspace;

    const std::string name = d.name;
    const std::string stem = d.fileStem;
    if (name.empty() || stem.empty()) {
        d.errorMessage = "バンク名とファイル名を入力してください。";
        return false;
    }

    const std::string relFile = buildRelativeBankFile(d);
    try {
        switch (d.type) {
            case NewBankType::Native:
                ws.createNativePatchBank(nextBankIndex(ws.nativePatchBanks()), name, relFile);
                break;
            case NewBankType::Performance:
                ws.createPerformanceBank(nextBankIndex(ws.performanceBanks()), name, relFile);
                break;
            case NewBankType::Device: {
                const fpe::VoicePatchType group = kCreatableDeviceGroups[d.deviceGroupIndex].type;
                ws.createDeviceBank(group, nextDeviceBankIndex(ws, group), name, relFile);
                break;
            }
            case NewBankType::Drum: {
                const auto kitType = d.drumKitTypeIndex == 0 ? fpe::DrumKitType::Routed : fpe::DrumKitType::Direct;
                ws.createDrumKit(nextDrumProg(ws.drumKits()), name, relFile, kitType);
                break;
            }
        }
        ws.save();
    } catch (const std::exception& e) {
        d.errorMessage = std::string("作成に失敗しました: ") + e.what();
        return false;
    }
    return true;
}

void renderNewBankDialog(AppContext& ctx) {
    NewBankDialogState& d = ctx.newBankDialog;
    if (!d.open) return;

    ImGui::OpenPopup("新規バンク作成");
    bool stayOpen = true;
    if (ImGui::BeginPopupModal("新規バンク作成", &stayOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        static const char* kTypeLabels[] = {"ネイティブパッチ", "デバイスパッチ", "パフォーマンスパッチ", "ドラムキット"};
        int typeIdx = static_cast<int>(d.type);
        if (ImGui::Combo("バンク種別", &typeIdx, kTypeLabels, IM_ARRAYSIZE(kTypeLabels))) {
            d.type = static_cast<NewBankType>(typeIdx);
        }

        ImGui::InputText("バンク名", d.name, sizeof(d.name));
        ImGui::InputText("ファイル名", d.fileStem, sizeof(d.fileStem));

        if (d.type == NewBankType::Device) {
            if (ImGui::BeginCombo("チップ系統", kCreatableDeviceGroups[d.deviceGroupIndex].label)) {
                for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(kCreatableDeviceGroups)); ++i) {
                    const bool selected = (i == d.deviceGroupIndex);
                    if (ImGui::Selectable(kCreatableDeviceGroups[i].label, selected)) d.deviceGroupIndex = i;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        if (d.type == NewBankType::Drum) {
            ImGui::RadioButton("routed", &d.drumKitTypeIndex, 0);
            ImGui::SameLine();
            ImGui::RadioButton("direct", &d.drumKitTypeIndex, 1);
        }

        if (d.fileStem[0] != '\0') {
            ImGui::TextDisabled("-> %s", buildRelativeBankFile(d).c_str());
        }

        if (!d.errorMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", d.errorMessage.c_str());
        }

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            if (tryCreateBank(ctx)) {
                d.open = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(120, 0))) {
            d.open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!stayOpen) d.open = false;
}

// Opens the shared PathPickerState pointed at `target` (a caller-owned
// fixed-size char buffer, e.g. one of PreferencesDialogState's fields).
// Starts browsing from target's current value if it looks like a usable
// existing path, else the process's current directory.
void openPathPicker(AppContext& ctx, bool pickFolder, char* target, size_t targetSize) {
    PathPickerState& p = ctx.pathPicker;
    p.pickFolder = pickFolder;
    p.target = target;
    p.targetSize = targetSize;

    fs::path start = fs::current_path();
    std::error_code ec;
    if (target[0] != '\0') {
        const fs::path candidate(target);
        if (pickFolder) {
            if (fs::is_directory(candidate, ec) && !ec) start = candidate;
        } else if (candidate.has_parent_path() && fs::is_directory(candidate.parent_path(), ec) && !ec) {
            start = candidate.parent_path();
        }
    }
    p.setDir(start);
    p.open = true;
}

void renderPathPicker(AppContext& ctx) {
    PathPickerState& p = ctx.pathPicker;
    if (!p.open) return;

    const char* title = p.pickFolder ? "フォルダを選択" : "プロファイルファイルを選択";
    ImGui::OpenPopup(title);
    bool stayOpen = true;
    if (ImGui::BeginPopupModal(title, &stayOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(p.pickFolder ? "フォルダをダブルクリックで移動し、「このフォルダを選択」で確定します。"
                                             : "*.profile.json をダブルクリックして選択します。");
        ImGui::Separator();

        ImGui::TextUnformatted("パス:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(480);
        const bool enterPressed = ImGui::InputText("##pathpickerinput", p.pathInput, sizeof(p.pathInput),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("移動") || enterPressed) {
            const fs::path dir(p.pathInput);
            std::error_code ec;
            if (fs::is_directory(dir, ec) && !ec) {
                p.setDir(dir);
            } else {
                p.listError = "フォルダが見つかりません: " + dir.string();
            }
        }

        if (!p.listError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", p.listError.c_str());
        }

        ImGui::Separator();
        ImGui::BeginChild("pathpickerlist", ImVec2(600, 300), true);

        if (p.currentDir.has_parent_path() && p.currentDir != p.currentDir.parent_path()) {
            if (ImGui::Selectable("../ (上へ)", false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                p.setDir(p.currentDir.parent_path());
            }
        }
        for (const auto& d : p.subdirs) {
            const std::string label = "[D] " + d.filename().string();
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                p.setDir(d);
            }
        }
        if (!p.pickFolder) {
            for (const auto& f : p.profileFiles) {
                const std::string label = f.filename().string();
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    std::snprintf(p.target, p.targetSize, "%s", f.string().c_str());
                    p.open = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            if (p.subdirs.empty() && p.profileFiles.empty()) {
                ImGui::TextDisabled("(このフォルダに *.profile.json はありません)");
            }
        } else if (p.subdirs.empty()) {
            ImGui::TextDisabled("(サブフォルダはありません)");
        }

        ImGui::EndChild();
        ImGui::Separator();

        if (p.pickFolder) {
            if (ImGui::Button("このフォルダを選択", ImVec2(160, 0))) {
                std::snprintf(p.target, p.targetSize, "%s", p.currentDir.string().c_str());
                p.open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("キャンセル", ImVec2(120, 0))) {
            p.open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!stayOpen) p.open = false;
}

// Populates the dialog's editable working copy from the current
// Preferences + a fresh RtMidi port scan, then opens it. The port list is
// re-scanned each time (not cached across the app's lifetime) since ports
// can appear/disappear (USB MIDI interfaces, loopMIDI, etc) while the app
// is running.
void openPreferencesDialog(AppContext& ctx) {
    PreferencesDialogState& d = ctx.preferencesDialog;
    std::snprintf(d.profileFolder, sizeof(d.profileFolder), "%s", ctx.preferences.profileFolder.c_str());
    d.autoLoadEnabled = ctx.preferences.autoLoadEnabled;
    std::snprintf(d.autoLoadProfilePath, sizeof(d.autoLoadProfilePath), "%s",
                  ctx.preferences.autoLoadProfilePath.c_str());
    d.midiPorts = ctx.previewOutput.listRtMidiPorts();
    d.midiPortIndex = ctx.preferences.midiPortIndex;
    if (d.midiPortIndex >= static_cast<int>(d.midiPorts.size())) {
        d.midiPortIndex = -1; // the previously-saved port is gone (device unplugged, etc)
    }
    d.midiChannel = ctx.preferences.midiChannel;
    d.errorMessage.clear();
    d.open = true;
}

void renderPreferencesDialog(AppContext& ctx) {
    PreferencesDialogState& d = ctx.preferencesDialog;
    if (!d.open) return;

    ImGui::OpenPopup("プリファレンス");
    bool stayOpen = true;
    if (ImGui::BeginPopupModal("プリファレンス", &stayOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Folder/file path fields get a trailing "参照..." browse button
        // that opens the shared in-app PathPickerState (see D-019) instead
        // of relying purely on manual typing.
        ImGui::TextUnformatted("優先プロファイルフォルダ");
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("##profileFolderInput", d.profileFolder, sizeof(d.profileFolder));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("「プロファイル読み込み」を開いたときの初期フォルダ");
        }
        ImGui::SameLine();
        if (ImGui::Button("参照...##browseProfileFolder")) {
            openPathPicker(ctx, /*pickFolder=*/true, d.profileFolder, sizeof(d.profileFolder));
        }

        ImGui::Checkbox("起動時にプロファイルを自動読み込み", &d.autoLoadEnabled);
        if (!d.autoLoadEnabled) ImGui::BeginDisabled();
        ImGui::TextUnformatted("自動読み込みプロファイルパス");
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("##autoLoadProfilePathInput", d.autoLoadProfilePath, sizeof(d.autoLoadProfilePath));
        ImGui::SameLine();
        if (ImGui::Button("参照...##browseAutoLoadProfilePath")) {
            openPathPicker(ctx, /*pickFolder=*/false, d.autoLoadProfilePath, sizeof(d.autoLoadProfilePath));
        }
        if (!d.autoLoadEnabled) ImGui::EndDisabled();
        ImGui::TextDisabled("(コマンドライン引数でプロファイルを指定した場合はこの設定より優先されます)");

        ImGui::Separator();
        const char* currentPortLabel = (d.midiPortIndex < 0 || d.midiPortIndex >= static_cast<int>(d.midiPorts.size()))
                                            ? "(なし)"
                                            : d.midiPorts[static_cast<size_t>(d.midiPortIndex)].c_str();
        if (ImGui::BeginCombo("出力MIDIポート", currentPortLabel)) {
            const bool noneSelected = d.midiPortIndex < 0;
            if (ImGui::Selectable("(なし)", noneSelected)) d.midiPortIndex = -1;
            if (noneSelected) ImGui::SetItemDefaultFocus();
            for (int i = 0; i < static_cast<int>(d.midiPorts.size()); ++i) {
                const bool selected = (i == d.midiPortIndex);
                if (ImGui::Selectable(d.midiPorts[static_cast<size_t>(i)].c_str(), selected)) d.midiPortIndex = i;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("FITOM_Xの内部パイプが見つからない場合の試聴用フォールバック出力先");
        }
        if (!ctx.previewOutput.rtMidiAvailable()) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "このビルドではMIDI出力が利用できません。");
        }

        ImGui::SetNextItemWidth(150);
        ImGui::SliderInt("出力MIDI CH", &d.midiChannel, 0, 15);

        if (!d.errorMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", d.errorMessage.c_str());
        }

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ctx.preferences.profileFolder = d.profileFolder;
            ctx.preferences.autoLoadEnabled = d.autoLoadEnabled;
            ctx.preferences.autoLoadProfilePath = d.autoLoadProfilePath;
            ctx.preferences.midiPortIndex = d.midiPortIndex;
            ctx.preferences.midiChannel = std::clamp(d.midiChannel, 0, 15);
            if (savePreferences(ctx.preferences)) {
                ctx.previewOutput.configureRtMidiPort(ctx.preferences.midiPortIndex);
                d.open = false;
                ImGui::CloseCurrentPopup();
            } else {
                d.errorMessage = "設定の保存に失敗しました:\n" + preferencesFilePath().string();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(120, 0))) {
            d.open = false;
            ImGui::CloseCurrentPopup();
        }

        // Nested modal (Dear ImGui's "stacked modals" pattern): the picker's
        // own OpenPopup()/BeginPopupModal() must be called from inside this
        // modal's Begin/EndPopupModal block, not as a sibling call after
        // EndPopup() - otherwise the picker's popup ID resolves in the
        // wrong ID-stack context and BeginPopupModal fails silently,
        // leaving a dangling "プリファレンス" modal that swallows input but
        // renders nothing (this shipped once as a real bug - the picker was
        // rendered from a separate top-level main() call - see D-019 fix).
        renderPathPicker(ctx);

        ImGui::EndPopup();
    }
    if (!stayOpen) d.open = false;
}

void renderMainMenu(AppContext& ctx) {
    ImGui::TextUnformatted("FITOM_X Patch Editor");
    ImGui::Separator();
    ImGui::Spacing();

    const ImVec2 buttonSize(260, 0);

    if (ImGui::Button("プロファイル読み込み", buttonSize)) {
        fs::path startDir = fs::current_path();
        if (!ctx.preferences.profileFolder.empty()) {
            std::error_code ec;
            if (fs::is_directory(ctx.preferences.profileFolder, ec) && !ec) startDir = ctx.preferences.profileFolder;
        }
        ctx.browser.setDir(startDir);
        ctx.state = AppState::FileBrowser;
    }

    ImGui::BeginDisabled();
    ImGui::Button("新規プロファイル作成", buttonSize);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("未実装 (次回以降)");

    ImGui::BeginDisabled();
    ImGui::Button("プロファイル削除", buttonSize);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("未実装 (次回以降)");

    if (ImGui::Button("プリファレンス", buttonSize)) {
        openPreferencesDialog(ctx);
    }
}

void renderFileBrowser(AppContext& ctx) {
    FileBrowserState& b = ctx.browser;

    ImGui::TextUnformatted("プロファイル読み込み - *.profile.json を選択 (ダブルクリック)");
    ImGui::Separator();

    ImGui::TextUnformatted("パス:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-80);
    const bool enterPressed = ImGui::InputText("##pathinput", b.pathInput, sizeof(b.pathInput),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("移動") || enterPressed) {
        fs::path p(b.pathInput);
        std::error_code ec;
        if (fs::is_directory(p, ec) && !ec) {
            b.setDir(p);
        } else {
            ctx.errorMessage = "フォルダが見つかりません:\n" + p.string();
        }
    }

    if (!b.listError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", b.listError.c_str());
    }

    ImGui::Separator();

    ImGui::BeginChild("filelist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);

    if (b.currentDir.has_parent_path() && b.currentDir != b.currentDir.parent_path()) {
        if (ImGui::Selectable("../ (上へ)", false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            b.setDir(b.currentDir.parent_path());
        }
    }

    for (const auto& d : b.subdirs) {
        const std::string label = "[D] " + d.filename().string();
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            b.setDir(d);
        }
    }

    for (const auto& f : b.profileFiles) {
        const std::string label = f.filename().string();
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            tryLoadProfile(ctx, f);
        }
    }

    if (b.subdirs.empty() && b.profileFiles.empty()) {
        ImGui::TextDisabled("(このフォルダに *.profile.json はありません)");
    }

    ImGui::EndChild();

    if (ImGui::Button("戻る (メニュー)")) {
        ctx.state = AppState::MainMenu;
    }
}

void renderToneLayer(int index, const fpe::ToneLayer& layer) {
    const std::string vt = fpe::voicePatchTypeToString(layer.voice_patch_type);
    ImGui::BulletText("ToneLayer %d: %s hw_bank=%d hw_prog=%d note=[%d-%d] transpose=%d pan=%d%s",
                       index, vt.c_str(), layer.hw_bank, layer.hw_prog, layer.note_range_lo,
                       layer.note_range_hi, layer.transpose, layer.pan_offset,
                       layer.enabled ? "" : " (disabled)");
}

// --- Patch editor (Device/HwPatch only for now - see D-015) --------------

// Per-field valid range for a given chip family, used to size sliders and
// grey out fields the chip doesn't actually read (D-016). `used=false`
// fields are still shown (disabled) rather than hidden, so the form's
// layout doesn't jump around when switching between patches of different
// chip families.
struct FieldRange {
    int minV = 0;
    int maxV = 99;
    bool used = true;
};
struct HwVoiceFieldRanges {
    FieldRange FB, ALG, AMS, PMS, NFQ, FB2;
};
struct HwOpFieldRanges {
    FieldRange AR, DR, SL, SR, RR, TL, KSR, KSL, MUL, DT1, DT2, FXV, AM, VIB, EGT, WS, REV, EGS, DT3;
};

// OPN(YM2203)/OPN2 family register widths, confirmed against FITOM_X's
// actual register-write masks in core/src/OPN_new.cpp (FB&7, ALG&7,
// DT1&7, MUL&0xF, TL&0x7F, AR/DR/SR&0x1F, KSR&3, SL/RR&0xF, EGT&0xF) and
// docs/voice-parameter-reference.md's OPN section (fields not listed
// there - DT2/KSL/FXV/AM/VIB/WS/REV/EGS/DT3/AMS/PMS/NFQ/FB2 - are unused
// by OPN and marked used=false).
HwVoiceFieldRanges opnVoiceRanges() {
    HwVoiceFieldRanges r;
    r.FB = {0, 7, true};
    r.ALG = {0, 7, true};
    r.AMS = {0, 0, false};
    r.PMS = {0, 0, false};
    r.NFQ = {0, 0, false};
    r.FB2 = {0, 0, false};
    return r;
}
HwOpFieldRanges opnOpRanges() {
    HwOpFieldRanges r;
    r.AR = {0, 31, true};
    r.DR = {0, 31, true};
    r.SL = {0, 15, true};
    r.SR = {0, 31, true};
    r.RR = {0, 15, true};
    r.TL = {0, 127, true};
    r.KSR = {0, 3, true};
    r.KSL = {0, 0, false};
    r.MUL = {0, 15, true};
    r.DT1 = {0, 7, true};
    r.DT2 = {0, 0, false};
    r.FXV = {0, 0, false};
    r.AM = {0, 0, false};
    r.VIB = {0, 0, false};
    r.EGT = {0, 15, true};
    r.WS = {0, 0, false};
    r.REV = {0, 0, false};
    r.EGS = {0, 0, false};
    r.DT3 = {0, 0, false};
    return r;
}

// OPL(YM3526)/OPL2(YM3812)/OPL3_2(YMF262 2op residual) family - confirmed
// against core/src/OPL_new.cpp's actual register-write masks (FB&7,
// ALG&1, AR/DR&0x1F, SL&0xF, RR read as a plain 4bit value, SR&0x1F
// (shifted into the same 4bit RR register when >0 - see
// docs/voice-parameter-reference.md's OPL section for the SR/RR/EGT
// conversion table), KSL as a 2bit field packed into TL's register,
// MUL&0xF, TL truncated to 6bit on the wire via tl6()=v>>1 but the field
// itself stays the usual 7bit/0-127 range like every other chip) and
// docs/voice-parameter-reference.md (DT1/DT2/EGT explicitly called out as
// "無関係" for this family - SR/RR cover the same ground EGT would - and
// REV/EGS/DT3 are OPZ-only). WS differs per chip: OPL has no waveform
// register at all (always sine), OPL2 is 2bit (0-3), OPL3_2 (the real
// OPL3 chip's 2op mode) is 3bit (0-7) - see D-021.
HwVoiceFieldRanges oplVoiceRanges() {
    HwVoiceFieldRanges r;
    r.FB = {0, 7, true};
    r.ALG = {0, 1, true};
    r.AMS = {0, 0, false};
    r.PMS = {0, 0, false};
    r.NFQ = {0, 0, false};
    r.FB2 = {0, 0, false};
    return r;
}
HwOpFieldRanges oplOpRanges(int wsMax) {
    HwOpFieldRanges r;
    r.AR = {0, 31, true};
    r.DR = {0, 31, true};
    r.SL = {0, 15, true};
    r.SR = {0, 31, true};
    r.RR = {0, 15, true};
    r.TL = {0, 127, true};
    r.KSR = {0, 1, true};
    r.KSL = {0, 3, true};
    r.MUL = {0, 15, true};
    r.DT1 = {0, 0, false};
    r.DT2 = {0, 0, false};
    r.FXV = {0, 0, false};
    r.AM = {0, 1, true};
    r.VIB = {0, 1, true};
    r.EGT = {0, 0, false};
    r.WS = (wsMax > 0) ? FieldRange{0, wsMax, true} : FieldRange{0, 0, false};
    r.REV = {0, 0, false};
    r.EGS = {0, 0, false};
    r.DT3 = {0, 0, false};
    return r;
}

// OPLL family (YM2413/YM2420/YMF281B/YM2423-X and the OPLLP/OPLLX/VRC7
// variants - docs/voice-parameter-reference.md groups these as sharing
// identical field semantics, confirmed here by core/src/OPLL_new.cpp:
// COPLLP/COPLLX/CVRC7/COPLL2 all derive from COPLL without overriding
// updateVoice, so they share the same register masks). Same 2op envelope
// widths as OPL/OPL2/OPL3_2 (AR/DR/SL/SR/RR/KSR/KSL/MUL/AM/VIB/TL - the
// doc's "ops[1].TL only" note describes the carrier's perceived loudness,
// but op[0]'s TL is still written to hardware (register 0x02) and affects
// modulation depth, so both stay used=true here). ALG is NOT a connection
// selector for OPLL like it is for OPL/OPL2/OPL3_2 - hw.ALG is instead a
// 4bit ROM preset instrument number (only meaningful when
// ext.ALG_EXT bit0=1; ignored/0 for user tones), so it's plain-edited
// here (no connection-diagram image - see isOplAlgFamily()). WS is a
// genuine but narrower field than the rest of the OPL family: 1 bit per
// operator (core/src/OPLL_new.cpp: `(WS&1)<<3`/`(WS&1)<<4`), not
// mentioned in the doc's OPLL field table at all - confirmed by reading
// the actual register-write code since the doc has a gap here.
HwVoiceFieldRanges opllVoiceRanges() {
    HwVoiceFieldRanges r;
    r.FB = {0, 7, true};
    r.ALG = {0, 15, true};
    r.AMS = {0, 0, false};
    r.PMS = {0, 0, false};
    r.NFQ = {0, 0, false};
    r.FB2 = {0, 0, false};
    return r;
}
HwOpFieldRanges opllOpRanges() {
    HwOpFieldRanges r = oplOpRanges(1); // same envelope widths as OPL family, WS is 1bit (0-1)
    return r;
}

// Generic wide-open fallback for chip families whose exact register widths
// haven't been confirmed against FITOM_X's source yet (D-016 tracks which
// ones still need this) - every field shown and editable, 0-99 (or the
// FXV field's full int16 range), so nothing is artificially blocked before
// its real range is known.
HwVoiceFieldRanges genericVoiceRanges() {
    HwVoiceFieldRanges r;
    r.FB = {0, 99, true};
    r.ALG = {0, 99, true};
    r.AMS = {0, 99, true};
    r.PMS = {0, 99, true};
    r.NFQ = {0, 99, true};
    r.FB2 = {0, 99, true};
    return r;
}
HwOpFieldRanges genericOpRanges() {
    HwOpFieldRanges r;
    const FieldRange w{0, 99, true};
    r.AR = w;
    r.DR = w;
    r.SL = w;
    r.SR = w;
    r.RR = w;
    r.TL = w;
    r.KSR = w;
    r.KSL = w;
    r.MUL = w;
    r.DT1 = w;
    r.DT2 = w;
    r.FXV = {-32768, 32767, true};
    r.AM = w;
    r.VIB = w;
    r.EGT = w;
    r.WS = w;
    r.REV = w;
    r.EGS = w;
    r.DT3 = w;
    return r;
}

// OPLL and its ROM-preset-table siblings (OPLLP/OPLLX/VRC7) share identical
// register semantics from FITOM_X's perspective - see opllVoiceRanges()/
// opllOpRanges() above.
bool isOpllFamily(fpe::VoicePatchType t) {
    return t == fpe::VoicePatchType::OPLL || t == fpe::VoicePatchType::OPLLP ||
           t == fpe::VoicePatchType::OPLLX || t == fpe::VoicePatchType::VRC7;
}

HwVoiceFieldRanges getVoiceFieldRanges(fpe::VoicePatchType t) {
    if (t == fpe::VoicePatchType::OPN || t == fpe::VoicePatchType::OPN2) return opnVoiceRanges();
    if (t == fpe::VoicePatchType::OPL || t == fpe::VoicePatchType::OPL2 ||
        t == fpe::VoicePatchType::OPL3_2) {
        return oplVoiceRanges();
    }
    if (isOpllFamily(t)) return opllVoiceRanges();
    return genericVoiceRanges();
}
HwOpFieldRanges getOpFieldRanges(fpe::VoicePatchType t) {
    if (t == fpe::VoicePatchType::OPN || t == fpe::VoicePatchType::OPN2) return opnOpRanges();
    if (t == fpe::VoicePatchType::OPL) return oplOpRanges(0);        // no WS register at all - always sine
    if (t == fpe::VoicePatchType::OPL2) return oplOpRanges(3);       // 2bit WS (0-3)
    if (t == fpe::VoicePatchType::OPL3_2) return oplOpRanges(7);     // 3bit WS (0-7)
    if (isOpllFamily(t)) return opllOpRanges();
    return genericOpRanges();
}

// Locates assets/ (currently just alg_diagrams/*.bmp) by searching upward
// from the process's current working directory for a known marker file -
// same approach tests/smoke_test.cpp uses for fixtures/, so a normal
// double-click launch (CWD = the exe's own directory, where CMakeLists.txt
// copies assets/ post-build) finds it with no compile-time path baked in.
fs::path assetsDir() {
    static fs::path cached;
    static bool resolved = false;
    if (resolved) return cached;
    resolved = true;
    fs::path p = fs::current_path();
    for (;;) {
        if (fs::exists(p / "assets" / "alg_diagrams" / "opn_al0.bmp")) {
            cached = p / "assets";
            return cached;
        }
        if (!p.has_parent_path() || p == p.parent_path()) break;
        p = p.parent_path();
    }
    cached = fs::path("assets"); // not found - callers just get load failures (missing-asset, not a crash)
    return cached;
}

// Shared by every get*Texture() cache below: loads path as a 24bit BMP and
// uploads it as a GL texture. Returns id=0 (still cached, so a missing/bad
// asset only fails once per run, not every frame) if the file is missing
// or fails to parse.
struct CachedTex {
    GLuint id = 0;
    int width = 0, height = 0;
};
CachedTex loadTexture(const fs::path& path) {
    CachedTex entry;
    BmpImage img;
    if (loadBmp24(path.string(), img)) {
        glGenTextures(1, &entry.id);
        glBindTexture(GL_TEXTURE_2D, entry.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
        entry.width = img.width;
        entry.height = img.height;
    }
    return entry;
}

// Lazily loads+uploads assets/alg_diagrams/opn_al<alg>.bmp as a GL texture,
// caching by ALG value (0-7) so repeated frames don't re-read the file.
// Returns 0 (and caches that too, to avoid retrying every frame) if the
// asset is missing or fails to parse.
GLuint getOpnAlgTexture(int alg, int& outWidth, int& outHeight) {
    static std::unordered_map<int, CachedTex> cache;
    auto it = cache.find(alg);
    if (it == cache.end()) {
        it = cache.emplace(alg, loadTexture(assetsDir() / "alg_diagrams" / ("opn_al" + std::to_string(alg) + ".bmp")))
                 .first;
    }
    outWidth = it->second.width;
    outHeight = it->second.height;
    return it->second.id;
}

// Same idea as getOpnAlgTexture() but for OPL/OPL2/OPL3_2's 1bit ALG
// (0=series FM, 1=parallel/AM - assets/alg_diagrams/opl_alg<0-1>.bmp,
// regenerated from the real opl_al0.bmp/opl_al1.bmp reference images'
// topology, D-021). OPLL doesn't use this - see isOplAlgFamily().
GLuint getOplAlgTexture(int alg, int& outWidth, int& outHeight) {
    static std::unordered_map<int, CachedTex> cache;
    auto it = cache.find(alg);
    if (it == cache.end()) {
        it = cache.emplace(alg, loadTexture(assetsDir() / "alg_diagrams" / ("opl_alg" + std::to_string(alg) + ".bmp")))
                 .first;
    }
    outWidth = it->second.width;
    outHeight = it->second.height;
    return it->second.id;
}

// Lazily loads+uploads assets/waveforms/ws<n>.bmp (n=0-7) as a GL texture -
// the OPL family's WS (waveform select) field, shown the same way ALG is
// (image + flanking spin buttons, the value burned into the image's own
// top-left corner) rather than a plain number, per D-021. Curves were
// plotted from real cached values in the reference spreadsheet
// (E:\...\material\waveform.xlsx Sheet1, columns B-I = WS0-WS7 vs.
// degree 0-359), not hand-drawn approximations.
GLuint getWsTexture(int ws, int& outWidth, int& outHeight) {
    static std::unordered_map<int, CachedTex> cache;
    auto it = cache.find(ws);
    if (it == cache.end()) {
        it = cache.emplace(ws, loadTexture(assetsDir() / "waveforms" / ("ws" + std::to_string(ws) + ".bmp"))).first;
    }
    outWidth = it->second.width;
    outHeight = it->second.height;
    return it->second.id;
}

// Builds the JSON payload for docs/plugin-midi-pipe.md section 5.2's
// HwPatch override SysEx. Deliberately NOT the same shape as
// fpe::to_json(HwPatch) (which nests hw.FB/ALG/etc under an "hw" key, to
// match this project's own *.hwbank.json on-disk format) - the wire
// protocol's example (`{"FB":5,"ALG":3,"ops":[...]}`, midi-message-reference.md
// section 8.1) has FB/ALG/etc as top-level keys instead, so this flattens
// FmHwVoice's fields up one level. `ext` isn't shown in the doc's minimal
// example; included here nested (matching this project's own field naming)
// on the assumption it follows the same "same key names as the bank file"
// rule the doc states for everything else - unconfirmed against a real
// FITOM_X build, see D-015.
nlohmann::json buildHwPatchOverrideJson(const fpe::HwPatch& patch) {
    nlohmann::json j;
    j["FB"] = patch.hw.FB;
    j["ALG"] = patch.hw.ALG;
    j["AMS"] = patch.hw.AMS;
    j["PMS"] = patch.hw.PMS;
    j["NFQ"] = patch.hw.NFQ;
    j["FB2"] = patch.hw.FB2;
    j["ops"] = patch.ops;
    j["ext"] = patch.ext;
    return j;
}

// Live visual aid only - NOT a sample-accurate emulation of any specific
// chip's envelope generator (that's FITOM_X's job at runtime). Assumptions,
// since none of these are pinned down by an explicit range/direction in
// FmHwOp's own field comments: higher AR/DR/RR = faster (shorter visual
// ramp); TL is an attenuation (0 = loudest, 99 = silent - common Yamaha FM
// convention), so peak height = 99-TL; SL is an absolute sustain height on
// the same 0-99 scale (taller = louder), not a further attenuation of the
// peak. If SR ("Sustain Rate (0 = sustain/ADSR mode, >0 = percussive
// mode)" per FmHwOp's own comment) is nonzero, the envelope doesn't hold a
// flat sustain - it keeps decaying toward 0 at a rate derived from SR
// instead, same as a real percussive (non-looping) voice would.
void renderEnvelopeCurve(const fpe::FmHwOp& op) {
    const ImVec2 size(200.0f, 70.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(20, 20, 20, 255));
    draw->AddRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(120, 120, 120, 255));

    auto rateToSegWidth = [](uint8_t rate, float weight) {
        const float norm = 1.0f - std::min<float>(rate, 99) / 99.0f; // slower rate -> wider (longer) segment
        return std::max(0.03f, norm) * weight;
    };

    const float peak = 1.0f - std::min<float>(op.TL, 99) / 99.0f;
    const float sustain = std::min<float>(op.SL, 99) / 99.0f;
    const bool percussive = op.SR > 0;

    const float attackW = rateToSegWidth(op.AR, size.x * 0.30f);
    const float decayW = rateToSegWidth(op.DR, size.x * 0.25f);
    const float sustainW = percussive ? rateToSegWidth(op.SR, size.x * 0.20f) : size.x * 0.20f;
    const float releaseW = rateToSegWidth(op.RR, size.x * 0.25f);

    const float baseY = p0.y + size.y - 2.0f;
    const float topY = p0.y + 2.0f;
    auto yFor = [&](float level) { return baseY - level * (baseY - topY); };

    const ImVec2 pStart(p0.x, baseY);
    const ImVec2 pPeak(pStart.x + attackW, yFor(peak));
    const ImVec2 pDecayEnd(pPeak.x + decayW, yFor(sustain));
    const ImVec2 pSustainEnd(pDecayEnd.x + sustainW, percussive ? yFor(0.0f) : pDecayEnd.y);
    const ImVec2 pReleaseEnd(pSustainEnd.x + releaseW, yFor(0.0f));

    const ImU32 lineCol = IM_COL32(64, 224, 208, 255);
    const ImU32 fillCol = IM_COL32(64, 224, 208, 60);
    auto fillSegment = [&](ImVec2 a, ImVec2 b) {
        draw->AddQuadFilled(ImVec2(a.x, baseY), a, b, ImVec2(b.x, baseY), fillCol);
        draw->AddLine(a, b, lineCol, 2.0f);
    };
    fillSegment(pStart, pPeak);
    fillSegment(pPeak, pDecayEnd);
    fillSegment(pDecayEnd, pSustainEnd);
    fillSegment(pSustainEnd, pReleaseEnd);

    ImGui::Dummy(size);
}

struct KeyboardResult {
    int pressedNote = -1;
    int releasedNote = -1;
};

// A clickable preview keyboard: `whiteKeyCount` white keys starting at MIDI
// note `baseNote` (which must be a C), standard piano layout (black keys
// after white-key positions 0,1,3,4,5 within each 7-white-key octave, i.e.
// after C/D/F/G/A, not after E/B). `whiteHeight` is caller-supplied (rather
// than a local constant) so it can be kept in sync with whatever's placed
// beside it via SameLine() (the Mod/Vol CC levers - see renderPatchEditor(),
// which passes the same kLeverHeight both places) instead of two literals
// that could silently drift apart. Uses IsItemActivated()/
// IsItemDeactivated() for press/release rather than manual hit-testing, so
// it naturally supports click-and-hold (release fires even if the mouse
// drifts off the key first, matching ImGui's own button semantics).
KeyboardResult renderPreviewKeyboard(int baseNote, int whiteKeyCount, float whiteHeight) {
    KeyboardResult result;
    static const int kSemisInOctave[] = {0, 2, 4, 5, 7, 9, 11};
    static const bool kHasBlackAfter[] = {true, true, false, true, true, true, false};
    const float whiteW = 22.0f, whiteH = whiteHeight, blackW = 14.0f, blackH = whiteHeight * 0.63f;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    for (int i = 0; i < whiteKeyCount; ++i) {
        const int octave = i / 7, idx = i % 7;
        const int note = baseNote + octave * 12 + kSemisInOctave[idx];
        const ImVec2 pos(origin.x + i * whiteW, origin.y);
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushID(note);
        ImGui::InvisibleButton("whitekey", ImVec2(whiteW - 1, whiteH));
        const bool active = ImGui::IsItemActive();
        draw->AddRectFilled(pos, ImVec2(pos.x + whiteW - 1, pos.y + whiteH),
                             active ? IM_COL32(200, 200, 220, 255) : IM_COL32(240, 240, 240, 255));
        draw->AddRect(pos, ImVec2(pos.x + whiteW - 1, pos.y + whiteH), IM_COL32(40, 40, 40, 255));
        if (ImGui::IsItemActivated()) result.pressedNote = note;
        if (ImGui::IsItemDeactivated()) result.releasedNote = note;
        ImGui::PopID();
    }
    for (int i = 0; i < whiteKeyCount - 1; ++i) {
        const int idx = i % 7;
        if (!kHasBlackAfter[idx]) continue;
        const int octave = i / 7;
        const int note = baseNote + octave * 12 + kSemisInOctave[idx] + 1;
        const ImVec2 pos(origin.x + (i + 1) * whiteW - blackW / 2.0f, origin.y);
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushID(note + 1000);
        ImGui::InvisibleButton("blackkey", ImVec2(blackW, blackH));
        const bool active = ImGui::IsItemActive();
        draw->AddRectFilled(pos, ImVec2(pos.x + blackW, pos.y + blackH),
                             active ? IM_COL32(90, 90, 100, 255) : IM_COL32(15, 15, 15, 255));
        if (ImGui::IsItemActivated()) result.pressedNote = note;
        if (ImGui::IsItemDeactivated()) result.releasedNote = note;
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + whiteH + 4.0f));
    ImGui::Dummy(ImVec2(0, 0)); // register an item at the new cursor pos - see Dear ImGui's own
                                // "SetCursorScreenPos to extend window boundaries" debug warning
    return result;
}

void sliderU8(const char* label, uint8_t& field, int minV, int maxV) {
    int v = field;
    if (ImGui::SliderInt(label, &v, minV, maxV)) field = static_cast<uint8_t>(std::clamp(v, minV, maxV));
}
void inputU8(const char* label, uint8_t& field, int minV = 0, int maxV = 255) {
    int v = field;
    if (ImGui::InputInt(label, &v)) field = static_cast<uint8_t>(std::clamp(v, minV, maxV));
}
void inputI16(const char* label, int16_t& field, int minV = -32768, int maxV = 32767) {
    int v = field;
    if (ImGui::InputInt(label, &v)) field = static_cast<int16_t>(std::clamp(v, minV, maxV));
}

// *Ranged wrappers grey out (but still show, to keep the layout stable
// across chip families) fields the current VoicePatchType doesn't
// actually read, and use its confirmed register width for the rest -
// see FieldRange/getVoiceFieldRanges()/getOpFieldRanges() (D-016).
void sliderU8Ranged(const char* label, uint8_t& field, const FieldRange& range) {
    if (!range.used) ImGui::BeginDisabled();
    sliderU8(label, field, range.minV, range.maxV);
    if (!range.used) ImGui::EndDisabled();
}
void inputU8Ranged(const char* label, uint8_t& field, const FieldRange& range) {
    if (!range.used) ImGui::BeginDisabled();
    inputU8(label, field, range.minV, range.maxV);
    if (!range.used) ImGui::EndDisabled();
}
void inputI16Ranged(const char* label, int16_t& field, const FieldRange& range) {
    if (!range.used) ImGui::BeginDisabled();
    inputI16(label, field, range.minV, range.maxV);
    if (!range.used) ImGui::EndDisabled();
}

// Chip families whose WS field has a real waveform-select image
// (assets/waveforms/ws<0-7>.bmp, D-021) to show instead of a plain number -
// OPL/OPL2/OPL3_2 (2/3bit WS) and the OPLL family (1bit WS, confirmed from
// core/src/OPLL_new.cpp - see opllOpRanges()). Deliberately broader than
// isOplAlgFamily() below (which excludes OPLL, since OPLL's ALG isn't a
// connection selector) - WS's meaning (waveform shape) is consistent across
// all of these, only its bit width differs (already reflected in
// ranges.WS via getOpFieldRanges()).
bool isOplWsImageFamily(fpe::VoicePatchType t) {
    return t == fpe::VoicePatchType::OPL || t == fpe::VoicePatchType::OPL2 ||
           t == fpe::VoicePatchType::OPL3_2 || isOpllFamily(t);
}

// Renders a value as an image (with the value burned into its own
// top-left corner, per D-017's ALG convention) flanked by spin buttons,
// falling back to a plain "◀ label n ▶" spinner when no texture is
// available (chip family not in scope yet, or the asset failed to load).
// Shared by ALG's channel-parameter band and WS's per-operator band
// (D-021) - `getTexture` abstracts over which asset folder/filename
// pattern to use (opl_alg<n>.bmp vs ws<n>.bmp).
void renderImageSpinner(const char* idSuffix, const char* label, uint8_t& value, const FieldRange& range,
                         float displayW, const std::function<GLuint(int, int&, int&)>& getTexture) {
    if (!range.used) ImGui::BeginDisabled();
    int v = value;
    int texW = 0, texH = 0;
    const GLuint tex = getTexture(std::clamp(v, range.minV, range.maxV), texW, texH);
    ImGui::PushButtonRepeat(true);

    if (tex != 0 && texW > 0 && texH > 0) {
        const float displayH = displayW * static_cast<float>(texH) / static_cast<float>(texW);
        const float rowTopY = ImGui::GetCursorPosY();
        const float buttonCenterY = rowTopY + (displayH - ImGui::GetFrameHeight()) * 0.5f;

        ImGui::SetCursorPosY(buttonCenterY);
        ImGui::PushID((std::string("minus") + idSuffix).c_str());
        if (ImGui::ArrowButton("##minus", ImGuiDir_Left) && v > range.minV) value = static_cast<uint8_t>(v - 1);
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::SetCursorPosY(rowTopY);
        ImGui::Image(static_cast<ImTextureID>(tex), ImVec2(displayW, displayH));
        ImGui::SameLine();
        ImGui::SetCursorPosY(buttonCenterY);
        ImGui::PushID((std::string("plus") + idSuffix).c_str());
        if (ImGui::ArrowButton("##plus", ImGuiDir_Right) && v < range.maxV) value = static_cast<uint8_t>(v + 1);
        ImGui::PopID();
    } else {
        ImGui::PushID((std::string("minus") + idSuffix).c_str());
        if (ImGui::ArrowButton("##minus", ImGuiDir_Left) && v > range.minV) value = static_cast<uint8_t>(v - 1);
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::Text("%s %d", label, v);
        ImGui::SameLine();
        ImGui::PushID((std::string("plus") + idSuffix).c_str());
        if (ImGui::ArrowButton("##plus", ImGuiDir_Right) && v < range.maxV) value = static_cast<uint8_t>(v + 1);
        ImGui::PopID();
    }

    ImGui::PopButtonRepeat();
    if (!range.used) ImGui::EndDisabled();
}

void renderHwOpEditor(int index, fpe::FmHwOp& op, const HwOpFieldRanges& ranges, fpe::VoicePatchType groupType) {
    ImGui::PushID(index);
    ImGui::BeginChild("op", ImVec2(230, 330), true);
    ImGui::Text("OP %d", index + 1);
    ImGui::Separator();
    renderEnvelopeCurve(op);
    sliderU8Ranged("AR", op.AR, ranges.AR);
    sliderU8Ranged("DR", op.DR, ranges.DR);
    sliderU8Ranged("SL", op.SL, ranges.SL);
    sliderU8Ranged("SR", op.SR, ranges.SR);
    sliderU8Ranged("RR", op.RR, ranges.RR);
    sliderU8Ranged("TL", op.TL, ranges.TL);

    // WS (waveform select) is elevated out of the "詳細" fold-out into its
    // own visible image+spinner control (like ALG's channel-band control -
    // D-017), rather than a plain number buried in the details tree, per
    // the explicit "OPパネルにWS設定を追加する" request (D-021).
    if (isOplWsImageFamily(groupType)) {
        renderImageSpinner("ws", "WS", op.WS, ranges.WS, 100.0f,
                            [](int v, int& w, int& h) { return getWsTexture(v, w, h); });
    } else {
        inputU8Ranged("WS", op.WS, ranges.WS);
    }

    if (ImGui::TreeNode("詳細")) {
        inputU8Ranged("KSR", op.KSR, ranges.KSR);
        inputU8Ranged("KSL", op.KSL, ranges.KSL);
        inputU8Ranged("MUL", op.MUL, ranges.MUL);
        inputU8Ranged("DT1", op.DT1, ranges.DT1);
        inputU8Ranged("DT2", op.DT2, ranges.DT2);
        inputI16Ranged("FXV", op.FXV, ranges.FXV);
        inputU8Ranged("AM", op.AM, ranges.AM);
        inputU8Ranged("VIB", op.VIB, ranges.VIB);
        inputU8Ranged("EGT", op.EGT, ranges.EGT);
        inputU8Ranged("REV", op.REV, ranges.REV);
        inputU8Ranged("EGS", op.EGS, ranges.EGS);
        inputU8Ranged("DT3", op.DT3, ranges.DT3);
        ImGui::TreePop();
    }
    ImGui::EndChild();
    ImGui::PopID();
}

// Renders one modeless patch-editor window's content (called from within
// an already-open ImGui::Begin(), see renderPatchEditors()). Scoped to
// Device (HwPatch) patches only for now - see D-015.
void renderPatchEditor(AppContext& ctx, PatchEditorWindow& editor) {
    fpe::PatchWorkspace& ws = ctx.workspace;
    auto& banks = ws.deviceBanks();
    if (editor.bankIndex >= banks.size()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "このバンクは既に存在しません。");
        return;
    }
    auto& bank = banks[editor.bankIndex];
    fpe::HwPatch* patch = bank.findByProg(editor.prog);
    if (!patch) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "このパッチは既に存在しません。");
        return;
    }

    const std::string groupStr = fpe::voicePatchTypeToString(bank.voicePatchType);
    ImGui::Text("[%s bank %d prog %d]", groupStr.c_str(), bank.bankIndex, patch->prog);

    char nameBuf[256];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", patch->name.c_str());
    if (ImGui::InputText("名前", nameBuf, sizeof(nameBuf))) patch->name = nameBuf;

    if (patch->isBuiltinRef()) {
        ImGui::TextWrapped(
            "内蔵ROM音色への参照(builtin)のため、ops[]による編集はできません(patch_type=%s, patch_no=%d)。",
            patch->builtin->patch_type.c_str(), patch->builtin->patch_no);
        return;
    }

    int swBank = patch->sw_bank, swProg = patch->sw_prog;
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("sw_bank", &swBank)) patch->sw_bank = swBank;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("sw_prog", &swProg)) patch->sw_prog = swProg;

    const HwVoiceFieldRanges voiceRanges = getVoiceFieldRanges(bank.voicePatchType);
    const HwOpFieldRanges opRanges = getOpFieldRanges(bank.voicePatchType);

    ImGui::Separator();
    ImGui::Text("チャンネルパラメータ");

    // ALG is shown as its own input here - the connection-diagram image
    // (OPN/OPN2: assets/alg_diagrams/opn_al<0-7>.bmp, D-016/D-017; OPL/
    // OPL2/OPL3_2: opl_alg<0-1>.bmp, D-021 - OPLL is excluded, see
    // isOplAlgFamily()) has the current ALG value burned into its own
    // top-left corner (so the image itself represents the setting, not a
    // separate "ALG n" text widget), flanked left/right by spin buttons -
    // at the left edge of this band, rather than a slider elsewhere.
    ImGui::BeginGroup();
    {
        const bool isOpnFamily =
            bank.voicePatchType == fpe::VoicePatchType::OPN || bank.voicePatchType == fpe::VoicePatchType::OPN2;
        const bool isOplAlgFamily = bank.voicePatchType == fpe::VoicePatchType::OPL ||
                                     bank.voicePatchType == fpe::VoicePatchType::OPL2 ||
                                     bank.voicePatchType == fpe::VoicePatchType::OPL3_2;
        if (isOpnFamily) {
            renderImageSpinner("alg", "ALG", patch->hw.ALG, voiceRanges.ALG, 150.0f,
                                [](int v, int& w, int& h) { return getOpnAlgTexture(v, w, h); });
        } else if (isOplAlgFamily) {
            renderImageSpinner("alg", "ALG", patch->hw.ALG, voiceRanges.ALG, 150.0f,
                                [](int v, int& w, int& h) { return getOplAlgTexture(v, w, h); });
        } else {
            renderImageSpinner("alg", "ALG", patch->hw.ALG, voiceRanges.ALG, 150.0f,
                                [](int, int& w, int& h) {
                                    w = h = 0;
                                    return static_cast<GLuint>(0);
                                });
        }
    }
    ImGui::EndGroup();
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(150);
    sliderU8Ranged("FB", patch->hw.FB, voiceRanges.FB);
    ImGui::SetNextItemWidth(150);
    sliderU8Ranged("AMS", patch->hw.AMS, voiceRanges.AMS);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    sliderU8Ranged("PMS", patch->hw.PMS, voiceRanges.PMS);
    ImGui::SetNextItemWidth(150);
    sliderU8Ranged("NFQ", patch->hw.NFQ, voiceRanges.NFQ);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    sliderU8Ranged("FB2", patch->hw.FB2, voiceRanges.FB2);
    ImGui::EndGroup();

    ImGui::Separator();
    for (size_t i = 0; i < patch->ops.size(); ++i) {
        renderHwOpEditor(static_cast<int>(i), patch->ops[i], opRanges, bank.voicePatchType);
        if (i + 1 < patch->ops.size()) ImGui::SameLine();
    }

    ImGui::Separator();
    const PreviewOutput::ActiveBackend backend = ctx.previewOutput.ensureReady();
    const bool connected = backend != PreviewOutput::ActiveBackend::None;
    const char* statusText = backend == PreviewOutput::ActiveBackend::FitomXPipe ? "FITOM_Xに接続済み"
                              : backend == PreviewOutput::ActiveBackend::RtMidi   ? "MIDI出力(フォールバック)で試聴中"
                                                                                  : "未接続(オフライン、プリファレンスでMIDI出力を設定できます)";
    ImGui::TextColored(connected ? ImVec4(0.4f, 1.0f, 0.6f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "試聴: %s",
                        statusText);
    const uint8_t previewChannel = static_cast<uint8_t>(std::clamp(ctx.preferences.midiChannel, 0, 15));

    // CC#1 (modulation) / CC#7 (volume) levers to the left of the preview
    // keyboard, matching the reference editors' "pitch/mod" lever layout.
    // The slider is the FIRST thing in each group (label goes below, not
    // above) specifically so its top and height (kLeverHeight, same value
    // renderPreviewKeyboard() uses for its white keys) line up exactly
    // with the keyboard called right after via SameLine() - putting a
    // label above the slider would push it down relative to the keyboard,
    // which has no such label.
    constexpr float kLeverHeight = 70.0f;
    ImGui::BeginGroup();
    int mod = editor.ccMod;
    if (ImGui::VSliderInt("##mod", ImVec2(24, kLeverHeight), &mod, 0, 127)) {
        editor.ccMod = std::clamp(mod, 0, 127);
        if (connected) ctx.previewOutput.sendControlChange(previewChannel, 1, static_cast<uint8_t>(editor.ccMod));
    }
    ImGui::TextUnformatted("Mod");
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    int vol = editor.ccVolume;
    if (ImGui::VSliderInt("##vol", ImVec2(24, kLeverHeight), &vol, 0, 127)) {
        editor.ccVolume = std::clamp(vol, 0, 127);
        if (connected) ctx.previewOutput.sendControlChange(previewChannel, 7, static_cast<uint8_t>(editor.ccVolume));
    }
    ImGui::TextUnformatted("Vol");
    ImGui::EndGroup();
    ImGui::SameLine();

    KeyboardResult kb = renderPreviewKeyboard(48, 22, kLeverHeight); // 3 octaves, C3-C6
    if (kb.pressedNote >= 0 && connected) {
        ctx.previewOutput.selectDevice(previewChannel, static_cast<uint8_t>(bank.voicePatchType),
                                        static_cast<uint8_t>(bank.bankIndex), static_cast<uint8_t>(patch->prog));
        ctx.previewOutput.sendHwPatchOverride(previewChannel, buildHwPatchOverrideJson(*patch).dump());
        ctx.previewOutput.noteOn(previewChannel, static_cast<uint8_t>(kb.pressedNote), 100);
        editor.heldNote = kb.pressedNote;
    }
    if (kb.releasedNote >= 0 && editor.heldNote == kb.releasedNote) {
        if (connected) ctx.previewOutput.noteOff(previewChannel, static_cast<uint8_t>(kb.releasedNote), 0);
        editor.heldNote = -1;
    }
}

// Iterates every open patch editor and renders each as its own modeless
// ImGui window (independent titlebar/close-button/position, per the
// "モードレスで複数開くことができる" requirement) - not tied to
// AppState, so these stay open regardless of which main screen is active.
// Fixed initial size, wide enough for the largest operator count in
// practice (4, for OPN/OPM/OPZ/OPL3 4op mode) - chip families with fewer
// operators (PSG=1, OPL2/OPLL=2) just leave the right side empty rather
// than the window resizing per chip (per the project owner - simpler and
// more predictable than the previous per-patch dynamic width).
constexpr ImVec2 kPatchEditorInitialSize(1100.0f, 900.0f);

void renderPatchEditors(AppContext& ctx) {
    for (auto& editor : ctx.openEditors) {
        if (!editor.open) continue;
        const std::string title = "パッチ編集##editor" + std::to_string(editor.id);
        ImGui::SetNextWindowSize(kPatchEditorInitialSize, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title.c_str(), &editor.open)) {
            renderPatchEditor(ctx, editor);
        }
        ImGui::End();
    }
    ctx.openEditors.erase(
        std::remove_if(ctx.openEditors.begin(), ctx.openEditors.end(), [](const PatchEditorWindow& e) {
            return !e.open;
        }),
        ctx.openEditors.end());
}

// Outline only lists banks/kits (name, index, patch/note count) - drilling
// into individual patches happens on a separate BankDetail screen, reached
// by clicking a bank/kit here (see selectBank()/renderBankDetail()).
void renderOutline(AppContext& ctx) {
    fpe::PatchWorkspace& ws = ctx.workspace;

    ImGui::Text("プロファイル: %s", ws.profile().profile_name.c_str());
    ImGui::SameLine();
    if (ImGui::Button("閉じる")) {
        ctx.workspace = fpe::PatchWorkspace{};
        ctx.state = AppState::MainMenu;
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("新規バンク作成")) {
        ctx.newBankDialog = NewBankDialogState{};
        ctx.newBankDialog.open = true;
    }
    ImGui::Separator();

    if (!ws.warnings().empty()) {
        if (ImGui::TreeNode("warnings", "警告 (%zu件)", ws.warnings().size())) {
            for (const auto& w : ws.warnings()) ImGui::TextWrapped("%s", w.c_str());
            ImGui::TreePop();
        }
        ImGui::Separator();
    }

    ImGui::BeginChild("outline", ImVec2(0, 0), true);

    if (ImGui::TreeNode("native", "ネイティブパッチバンク (%zu)", ws.nativePatchBanks().size())) {
        auto& banks = ws.nativePatchBanks();
        for (size_t i = 0; i < banks.size(); ++i) {
            const auto& bank = banks[i];
            std::string label = "[bank " + std::to_string(bank.bankIndex) + "] " + bank.name +
                                 " (" + std::to_string(bank.patches.size()) + " patches)";
            if (ImGui::Selectable(label.c_str())) selectBank(ctx, BankCategory::Native, i);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("perf", "パフォーマンスバンク (%zu)", ws.performanceBanks().size())) {
        auto& banks = ws.performanceBanks();
        for (size_t i = 0; i < banks.size(); ++i) {
            const auto& bank = banks[i];
            std::string label = "[bank " + std::to_string(bank.bankIndex) + "] " + bank.name +
                                 " (" + std::to_string(bank.patches.size()) + " patches)";
            if (ImGui::Selectable(label.c_str())) selectBank(ctx, BankCategory::Performance, i);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("device", "デバイスパッチバンク (%zu)", ws.deviceBanks().size())) {
        auto& banks = ws.deviceBanks();
        for (size_t i = 0; i < banks.size(); ++i) {
            const auto& bank = banks[i];
            const std::string groupStr = fpe::voicePatchTypeToString(bank.voicePatchType);
            std::string label = "[" + groupStr + " bank " + std::to_string(bank.bankIndex) + "] " + bank.name +
                                 " (" + std::to_string(bank.patches.size()) + " patches)";
            if (ImGui::Selectable(label.c_str())) selectBank(ctx, BankCategory::Device, i);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("samplezone", "サンプルゾーンバンク (%zu)", ws.sampleZoneBanks().size())) {
        auto& banks = ws.sampleZoneBanks();
        for (size_t i = 0; i < banks.size(); ++i) {
            const auto& bank = banks[i];
            const std::string groupStr = fpe::voicePatchTypeToString(bank.voicePatchType);
            std::string label = "[" + groupStr + " bank " + std::to_string(bank.bankIndex) + "] " + bank.name +
                                 " (" + std::to_string(bank.patches.size()) + " patches)";
            if (ImGui::Selectable(label.c_str())) selectBank(ctx, BankCategory::SampleZone, i);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("pcm", "PCM波形バンク (%zu)", ws.pcmBanks().size())) {
        auto& banks = ws.pcmBanks();
        for (size_t i = 0; i < banks.size(); ++i) {
            const auto& bank = banks[i];
            const std::string groupStr = fpe::voicePatchTypeToString(bank.voicePatchType);
            std::string label = "[" + groupStr + " bank " + std::to_string(bank.bankIndex) + "] " + bank.name +
                                 " (" + std::to_string(bank.entries.size()) + " patches)";
            if (ImGui::Selectable(label.c_str())) selectBank(ctx, BankCategory::Pcm, i);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("drum", "ドラムキットマップ (%zu)", ws.drumKits().size())) {
        auto& kits = ws.drumKits();
        for (size_t i = 0; i < kits.size(); ++i) {
            const auto& kit = kits[i];
            const char* typeStr = (kit.type == fpe::DrumKitType::Routed) ? "routed" : "direct";
            std::string label = "[prog " + std::to_string(kit.prog) + "] " + kit.name + " (" + typeStr + ", " +
                                 std::to_string(kit.notes.size()) + " notes)";
            if (ImGui::Selectable(label.c_str())) selectBank(ctx, BankCategory::Drum, i);
        }
        ImGui::TreePop();
    }

    ImGui::EndChild();
}

// The patch/note list for the single bank or drum kit selected in
// renderOutline(). Deliberately shallow (name/prog/basic refs only, plus
// ToneLayer for native patches since that's the bank's own on-disk shape) -
// per-parameter editing is future work (see docs/STATUS.md).
void renderBankDetail(AppContext& ctx) {
    fpe::PatchWorkspace& ws = ctx.workspace;

    if (ImGui::Button("戻る (アウトライン)")) {
        ctx.state = AppState::Outline;
        return;
    }
    ImGui::Separator();

    ImGui::BeginChild("bankdetail", ImVec2(0, 0), true);

    switch (ctx.selectedCategory) {
        case BankCategory::Native: {
            auto& banks = ws.nativePatchBanks();
            if (ctx.selectedIndex >= banks.size()) break;
            auto& bank = banks[ctx.selectedIndex];
            ImGui::Text("ネイティブパッチバンク [bank %d] %s", bank.bankIndex, bank.name.c_str());
            ImGui::Separator();
            for (auto& patch : bank.patches) {
                ImGui::PushID(&patch);
                if (ImGui::TreeNode("patch", "[prog %d] %s (%zu layers)", patch.prog, patch.name.c_str(),
                                     patch.layers.size())) {
                    for (size_t i = 0; i < patch.layers.size(); ++i) {
                        renderToneLayer(static_cast<int>(i), patch.layers[i]);
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            break;
        }
        case BankCategory::Performance: {
            auto& banks = ws.performanceBanks();
            if (ctx.selectedIndex >= banks.size()) break;
            auto& bank = banks[ctx.selectedIndex];
            ImGui::Text("パフォーマンスバンク [bank %d] %s", bank.bankIndex, bank.name.c_str());
            ImGui::Separator();
            for (auto& patch : bank.patches) {
                ImGui::BulletText("[prog %d] %s", patch.prog, patch.name.c_str());
            }
            break;
        }
        case BankCategory::Device: {
            auto& banks = ws.deviceBanks();
            if (ctx.selectedIndex >= banks.size()) break;
            auto& bank = banks[ctx.selectedIndex];
            const std::string groupStr = fpe::voicePatchTypeToString(bank.voicePatchType);
            ImGui::Text("デバイスパッチバンク [%s bank %d] %s", groupStr.c_str(), bank.bankIndex, bank.name.c_str());
            ImGui::Separator();
            for (auto& patch : bank.patches) {
                std::string label = "[prog " + std::to_string(patch.prog) + "] " + patch.name;
                if (patch.sw_bank >= 0 && patch.sw_prog >= 0) {
                    label += " (sw_bank=" + std::to_string(patch.sw_bank) +
                             " sw_prog=" + std::to_string(patch.sw_prog);
                    auto* sw = ws.resolvePerformancePatch(patch.sw_bank, patch.sw_prog);
                    label += sw ? (" -> " + sw->name + ")") : std::string(" -> ?)");
                }
                if (ImGui::Selectable(label.c_str())) openPatchEditor(ctx, ctx.selectedIndex, patch.prog);
            }
            break;
        }
        case BankCategory::SampleZone: {
            auto& banks = ws.sampleZoneBanks();
            if (ctx.selectedIndex >= banks.size()) break;
            auto& bank = banks[ctx.selectedIndex];
            const std::string groupStr = fpe::voicePatchTypeToString(bank.voicePatchType);
            ImGui::Text("サンプルゾーンバンク [%s bank %d] %s", groupStr.c_str(), bank.bankIndex, bank.name.c_str());
            ImGui::Separator();
            for (auto& patch : bank.patches) {
                ImGui::BulletText("[prog %d] %s (%zu zones)", patch.prog, patch.name.c_str(), patch.zones.size());
            }
            break;
        }
        case BankCategory::Pcm: {
            auto& banks = ws.pcmBanks();
            if (ctx.selectedIndex >= banks.size()) break;
            auto& bank = banks[ctx.selectedIndex];
            const std::string groupStr = fpe::voicePatchTypeToString(bank.voicePatchType);
            ImGui::Text("PCM波形バンク [%s bank %d] %s", groupStr.c_str(), bank.bankIndex, bank.name.c_str());
            ImGui::Separator();
            for (size_t i = 0; i < bank.entries.size(); ++i) {
                const auto& e = bank.entries[i];
                ImGui::BulletText("[prog %zu] %s (root_note=%d, size=%u bytes)", i, e.name.c_str(), e.root_note,
                                   e.size);
            }
            break;
        }
        case BankCategory::Drum: {
            auto& kits = ws.drumKits();
            if (ctx.selectedIndex >= kits.size()) break;
            auto& kit = kits[ctx.selectedIndex];
            const char* typeStr = (kit.type == fpe::DrumKitType::Routed) ? "routed" : "direct";
            ImGui::Text("ドラムキット [prog %d] %s (%s)", kit.prog, kit.name.c_str(), typeStr);
            ImGui::Separator();
            if (kit.type == fpe::DrumKitType::Routed) {
                for (auto& note : kit.notes) {
                    ImGui::BulletText("note %d: %s -> play_note %d", note.note, note.name.c_str(), note.play_note);
                }
            } else {
                ImGui::BulletText("note %d-%d -> patch_bank=%d patch_prog=%d", kit.note_min, kit.note_max,
                                   kit.patch_bank, kit.patch_prog);
            }
            break;
        }
    }

    ImGui::EndChild();
}

void renderErrorPopup(AppContext& ctx) {
    if (ctx.errorMessage.empty()) return;

    ImGui::OpenPopup("読み込みエラー");
    bool open = true;
    if (ImGui::BeginPopupModal("読み込みエラー", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", ctx.errorMessage.c_str());
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ctx.errorMessage.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open) ctx.errorMessage.clear();
}

} // namespace

int main(int argc, char** argv) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit() failed\n");
        return 1;
    }

    // OpenGL 3.0 + GLSL 130: the same baseline Dear ImGui's own GLFW+OpenGL3
    // example uses, for maximum portability across platforms/drivers.
    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "FITOM_X Patch Editor", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow() failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::fprintf(stderr, "glewInit() failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    loadFonts(io);

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    AppContext ctx;

    // Preferences are loaded before anything else so both the RtMidi port
    // and the auto-load decision below can use them. A CLI-given profile
    // path (argv[1]) overrides preferences.autoLoadProfilePath for this run
    // only - ctx.preferences itself is never written back from argv, so the
    // saved preference file is untouched regardless of how this run was
    // launched (see D-018).
    ctx.preferences = loadPreferences();
    ctx.previewOutput.configureRtMidiPort(ctx.preferences.midiPortIndex);

    // argv[1], if given, is the path to the profile that should already be
    // "open" on startup (see file-level comment above). Loading doesn't
    // touch the GL/ImGui state, so it's safe to do before the render loop
    // starts; tryLoadProfile() already handles success (-> Outline) and
    // failure (errorMessage set, state stays MainMenu) uniformly with the
    // FileBrowser pick path.
    if (argc > 1) {
        tryLoadProfile(ctx, fs::path(argv[1]));
    } else if (ctx.preferences.autoLoadEnabled && !ctx.preferences.autoLoadProfilePath.empty()) {
        tryLoadProfile(ctx, fs::path(ctx.preferences.autoLoadProfilePath));
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
        ImGui::Begin("FITOM_X Patch Editor");

        switch (ctx.state) {
            case AppState::MainMenu:
                renderMainMenu(ctx);
                break;
            case AppState::FileBrowser:
                renderFileBrowser(ctx);
                break;
            case AppState::Outline:
                renderOutline(ctx);
                break;
            case AppState::BankDetail:
                renderBankDetail(ctx);
                break;
        }
        renderPatchEditors(ctx);
        renderNewBankDialog(ctx);
        renderPreferencesDialog(ctx); // also renders the shared path-picker modal, nested inside its own popup (see D-019)
        renderErrorPopup(ctx);

        ImGui::End();

        ImGui::Render();
        int displayW = 0, displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
