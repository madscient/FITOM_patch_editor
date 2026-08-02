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
// If a profile path is given as argv[1] (and only that one argument),
// that profile is loaded up front and the app starts directly on Outline
// (as if it had just been picked from FileBrowser), skipping
// MainMenu/FileBrowser entirely. This is for launching from an
// already-running FITOM_X instance, which knows which profile it
// currently has loaded and can hand that path straight to the editor. On
// load failure, falls back to the normal MainMenu + error popup (see
// main()).
//
// Kiosk mode (D-026; argument format extended in D-039, Performance/Drum
// kinds added D-040): `fitom_patch_editor_gui.exe <profile.json> <kind>
// <bank-file> <prog>` (exactly 4 arguments) skips MainMenu/Outline/
// BankDetail entirely and opens a single, full-viewport editor for that
// patch - no menu ever shown, and closing that one editor window exits the
// whole process. `kind` is "device" (bank-file = *.hwbank.json), "layered"
// (bank-file = *.patchbank.json), "performance" (bank-file = *.swbank.json),
// or "drum" (bank-file = *.drumkit.json) - parseKioskKind() selects which
// editor screen actually opens (renderPatchEditor()/
// renderLayeredPatchEditor()/renderPerformancePatchEditor()/
// renderDrumKitDetail() respectively), so e.g. a MIDI channel that's playing
// a layered patch opens the layered patch editor instead of reusing the
// Device screen regardless of what's actually selected. "pcmbank"/
// "samplezonebank" are reserved kind keywords for future work
// (kioskKindImplemented()) but not yet backed by an editor screen. Also
// meant for launching from FITOM_X, but for the narrower "jump straight into
// editing this one patch" case rather than "open this profile". A bad kiosk
// invocation (missing profile, unknown/not-yet-implemented kind, bank/prog
// not found) fails fast via stderr + a nonzero exit code before any window
// is created - see main().
//
// Outline also has a "新規バンク作成" button (renderNewBankDialog()) that
// creates a new layered/device/performance bank or drum kit - bank
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
// D-016).
//
// Selecting a Layered patch from BankDetail opens a separate modeless editor
// (renderLayeredPatchEditors()/renderLayeredPatchEditor(), D-036) for its
// name/poly/ToneLayer stack. Each ToneLayer's hw_bank/hw_prog device-voice
// reference is shown as a resolved name label with a clickable picker
// (renderHwPatchPicker(), HW/device patches only, mirroring the sw_bank/
// sw_prog treatment in renderPatchEditor(), D-034) rather than raw integer
// fields, plus a trailing "編集" button that opens that HwPatch's own
// modeless editor (reused as-is - no separate "modal" editor was built for
// this).
//
// Selecting a Performance patch from BankDetail opens a third modeless
// editor (renderPerformancePatchEditors()/renderPerformancePatchEditor())
// for a fpe::SwPatch's name/fine_transpose, channel vibrato (FmSwVoice), and
// each operator's velocity-sensitivity/tremolo (FmSwOp x4). LWF/SLW (LFO
// waveform) are shown as an image+spinner (assets/waveforms/lfo<0-6>.png -
// placeholders with only the numeric index burned in, a human will draw the
// actual waveform shapes later), LFM/SLM (LFO mode) as a symbol dropdown,
// everything else as a plain slider (exact ranges unconfirmed against any
// FITOM_X doc, unlike HwPatch's FieldRange tables). No realtime preview -
// a SwPatch has no synthesis parameters of its own to sound.
//
// Drum kits (BankCategory::Drum) get a two-level drill-down instead of one
// modeless editor (D-038): renderBankDetail() itself becomes the "ドラム
// ノート選択画面" for a "routed" kit - all 128 MIDI notes 0-127, including
// unassigned ones, each with 複製/削除 buttons - and selecting/creating a
// note opens a fourth modeless editor
// (renderDrumNoteEditors()/renderDrumNoteEditor()) for that one
// fpe::DrumNote's name, source patch (a picker spanning both the layered-
// patch and device-voice-patch trees, since a DrumNote's source has the same
// CC#0 "normal mode vs direct mode" duality as ToneLayer/Patch's own
// references), play_note (by name or by an on-screen keyboard - deliberately
// not a raw number field, per the project owner's request), fine_tune/pan/
// gate_time, and its own sw_bank/sw_prog override. Unlike the Layered/
// Performance editors, this one DOES support realtime preview (a press-
// and-hold "試聴" button) since a DrumNote's source patch + play_note fully
// determine what it would sound like on their own. "direct" kits have no
// discrete note list (DrumKit.h's effectiveNotes() - the whole kit is one
// passthrough range), so they stay on renderBankDetail() and get a much
// smaller inline edit (source-patch picker + note range) instead of the
// note-list/editor drill-down.
//
// "新規プロファイル作成"/"プロファイル削除" are shown in the main menu
// but intentionally left disabled - not implemented yet. The virtual MIDI
// controller is also still future work (see docs/STATUS.md).
//
// Backend: GLFW (window/input) + OpenGL3 (rendering) + GLEW (GL function
// loading). All three, plus Dear ImGui itself and nlohmann/json, are
// resolved via vcpkg (see vcpkg.json / CMakePresets.json) - there is no
// vendored/submoduled third-party source in this repository.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// GLEW must be included before any other header that may pull in the
// platform's own (older) OpenGL headers - including GLFW's.
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX // windows.h's min/max macros would break every std::min/std::max/std::clamp call below
#include <windows.h> // MessageBoxW()/MultiByteToWideChar() only - see showFatalErrorBox(), D-029
#endif

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <nlohmann/json.hpp>

#include "ImageLoader.h"
#include "Preferences.h"
#include "PreviewOutput.h"
#include "fpe/BuiltinVoices.h"
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
enum class BankCategory { Layered, Performance, Device, SampleZone, Pcm, Drum };

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

// Scientific pitch notation (MIDI note 60 = "C4", 69 = "A4" = 440Hz) - used
// by the drum-note editor's play_note field (renderDrumNoteEditor()) so it
// can be picked by name ("C4"/"A3") rather than a bare 0-127 number, per the
// project owner's explicit request. This octave numbering is a convention
// choice (some hardware/DAWs instead call note 60 "C3") - not derived from
// any FITOM_X doc, since play_note is just a raw MIDI note number on the
// wire either way and the label is purely this editor's own UI sugar.
std::string midiNoteName(int note) {
    static const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (note < 0 || note > 127) return "?";
    const int octave = note / 12 - 1;
    return std::string(kNames[note % 12]) + std::to_string(octave);
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
// PatchWorkspace's createLayeredPatchBank/createDeviceBank/
// createPerformanceBank/createDrumKit). SampleZoneBank/PcmBank are
// deliberately not offered here - see kCreatableDeviceGroups above.
enum class NewBankType { Layered, Device, Performance, Drum };

struct NewBankDialogState {
    bool open = false;
    NewBankType type = NewBankType::Layered;
    char name[128] = {};
    char fileStem[128] = {}; // just the base name - postfix/extension are auto-generated (see buildRelativeBankFile())
    int deviceGroupIndex = 0; // index into kCreatableDeviceGroups, only used when type == Device
    int drumKitTypeIndex = 0; // 0 = routed, 1 = direct; only used when type == Drum
    std::string errorMessage; // validation/creation failure, shown inline in the dialog
};

// A single modeless "patch editor" window (renderPatchEditor()). Several
// can be open at once (see AppContext::openEditors) - selecting a patch
// from BankDetail opens one rather than replacing the current screen.
// Scoped to Device (HwPatch) patches only; Layered patches get their own
// separate LayeredPatchEditorWindow/renderLayeredPatchEditor() (D-036) since
// they're a reference-only structure (ToneLayers pointing at HwPatches),
// not a synthesis-parameter form. Performance/Drum editors are still future
// work (see docs/DESIGN.md D-015).
struct PatchEditorWindow {
    int id = 0;
    bool open = true;
    size_t bankIndex = 0; // index into ws.deviceBanks()
    int prog = 0;         // HwPatch::prog within that bank
    int heldNote = -1;    // preview keyboard note currently held down, -1 if none
    int ccMod = 0;        // CC#1 lever position (0-127), local to this editor's preview channel
    int ccVolume = 100;   // CC#7 lever position (0-127), GM default volume

    // D-027: realtime diff-only SysEx streaming + explicit "登録" (save)
    // semantics. `lastSent` tracks whatever HwPatch state FITOM_X was last
    // actually told about (via a live diff or the full send on close), so
    // each frame's diff only needs to describe what changed since then -
    // not the whole patch (docs/manuals/midi-message-reference.md 8.1
    // confirms the wire protocol only needs "the parameters you want to
    // override"). `registered` tracks the last-known-persisted-to-disk
    // state (set at open time and again whenever "登録" is pressed) - used
    // to resend the FULL patch on close, so any live-only edits that were
    // never registered don't leave FITOM_X's active preview diverged from
    // what's actually on disk. Both start uninitialized (`initialized`
    // false) since they need the real loaded patch, not a
    // default-constructed one - renderPatchEditor() populates them from
    // the patch the first time it actually renders this editor.
    fpe::HwPatch lastSent;
    fpe::HwPatch registered;
    bool initialized = false;
    bool deviceSelected = false; // selectDevice() sent at least once this editor's lifetime
};

// Navigation level within a patch-picker popup - mirrors FITOM_X本体's own
// PatchPickerDialog (apps/fitom_gui/PatchPickerDialog.h/.cpp in the FITOM_X
// repo): Category(チップファミリー、任意) -> Bank -> Program, one level shown
// per frame with a "上へ" button to go back, instead of one giant
// always-expanded tree. Requested explicitly ("FITOM_X本体と同じ構造に
// してほしい") because a real profile's banks can number in the dozens
// (device banks especially, spread across a dozen+ chip families - see
// ../FITOM_staging/banks/), and a flat always-open tree became hard to
// browse. Not every picker uses all three levels - SwPatchPickerState below
// has no chip-family axis (performance banks aren't chip-tagged in FITOM_X
// either - PatchPickerDialog itself has no equivalent picker for them) so it
// only ever uses Bank/Program.
enum class PatchPickerLevel { Category, Bank, Program };

// Shared "SW (performance) patch picker" popup for a `sw_bank`/`sw_prog`
// reference - opened either from renderPatchEditor() (a HwPatch's own
// sw_bank/sw_prog) or renderLayeredPatchEditor() (a layered fpe::Patch's own
// sw_bank/sw_prog, D-036) when the user clicks the resolved bank/patch-name
// label instead of typing raw numbers (per the project owner's request:
// patch editors in general should show this reference as a name, with a
// clickable label opening a picker). Both reference kinds share the exact
// same `{bank, prog}` shape and the same picker UI (list every performance
// bank/patch, select one), so one shared state/render function handles both
// rather than duplicating the tree-list code - only which underlying
// int fields end up written differs, chosen via `target`.
//
// Like PathPickerState, only one instance is needed (only one modal can be
// open at a time) - repointed at whichever patch is being edited via
// openSwPatchPicker() (Device) / openLayeredSwPatchPicker() (Layered). Stores
// indices rather than a raw pointer to the target patch, matching
// PatchEditorWindow's own "store indices, re-derive the real reference
// every frame" convention (D-012/D-015) - safer than holding a pointer
// across frames if the underlying vector were ever reallocated while the
// picker is open.
// D-038 adds a third target: a DrumNote's own sw_bank/sw_prog override
// (include/fpe/DrumKit.h - falls back to the resolved source patch's own
// sw_bank/sw_prog when unset, same "-1 = not set" convention as
// HwPatch/Patch). Reuses this shared picker/state rather than a fourth
// bespoke one, same reasoning as adding Layered alongside Device (D-036).
enum class SwPatchPickerTarget { Device, Layered, DrumNote };

struct SwPatchPickerState {
    bool open = false;
    SwPatchPickerTarget target = SwPatchPickerTarget::Device;
    size_t deviceBankIndex = 0; // Target::Device: index into ws.deviceBanks() of the HwPatch being edited
    int devicePatchProg = 0;    // Target::Device: HwPatch::prog within that bank
    size_t layeredBankIndex = 0; // Target::Layered: index into ws.layeredPatchBanks() of the Patch being edited
    int layeredPatchProg = 0;    // Target::Layered: Patch::prog within that bank
    size_t drumKitIndex = 0;    // Target::DrumNote: index into ws.drumKits()
    uint8_t drumNote = 0;       // Target::DrumNote: DrumNote::note within that kit (routed kits only)

    // Drill-down navigation state (see PatchPickerLevel) - Bank/Program only,
    // no Category level (performance banks have no chip-family axis).
    PatchPickerLevel level = PatchPickerLevel::Bank;
    int bank = 0; // chosen SwBank::bankIndex
};

// A single modeless "layered patch editor" window
// (renderLayeredPatchEditor()). Edits a layered fpe::Patch (its name/poly and
// its ToneLayer stack) - distinct from PatchEditorWindow, which edits the
// underlying per-chip fpe::HwPatch a ToneLayer references. Several can be
// open at once, same "store indices, re-derive the real Patch& every frame"
// convention as PatchEditorWindow (D-012/D-015).
struct LayeredPatchEditorWindow {
    int id = 0;
    bool open = true;
    size_t bankIndex = 0; // index into ws.layeredPatchBanks()
    int prog = 0;         // Patch::prog within that bank
};

// A single modeless "performance patch editor" window
// (renderPerformancePatchEditor()). Edits a fpe::SwPatch (vibrato/tremolo/
// velocity-sensitivity/fine-transpose expression parameters, see
// include/fpe/SwPatch.h) - same "store indices, re-derive the real SwPatch&
// every frame" convention as PatchEditorWindow/LayeredPatchEditorWindow
// (D-012/D-015/D-036). No realtime preview/SysEx streaming (like
// LayeredPatchEditorWindow, and for the same reason - a SwPatch has no
// synthesis parameters of its own to sound; it only ever applies on top of
// whichever HwPatch references it via sw_bank/sw_prog, and that HwPatch
// already has its own preview-capable editor).
struct PerformancePatchEditorWindow {
    int id = 0;
    bool open = true;
    size_t bankIndex = 0; // index into ws.performanceBanks()
    int prog = 0;         // SwPatch::prog within that bank
};

// A single modeless "drum note editor" window (renderDrumNoteEditor(),
// D-038). Edits one fpe::DrumNote within a "routed" fpe::DrumKit - "direct"
// kits have no discrete per-note list to open one of (see DrumKit.h's
// effectiveNotes() comment: a direct kit's notes are synthesized, not
// independently editable), so those are instead edited inline in
// renderBankDetail() itself, not through this window. Same "store indices,
// re-derive the real DrumNote& every frame" convention as the other patch
// editor windows (D-012/D-015/D-036/D-037) - `note` is DrumNote::note (the
// stable MIDI-trigger-note key), not a vector index, since kit.notes[] can
// be reordered/resized by other UI (duplicate/delete) while this is open.
struct DrumNoteEditorWindow {
    int id = 0;
    bool open = true;
    size_t kitIndex = 0; // index into ws.drumKits()
    uint8_t note = 0;    // DrumNote::note within that kit
    int heldPreviewNote = -1; // play_note currently sounding via the "試聴" button, -1 if none
};

// Shared "HW (device voice) patch picker" popup for a ToneLayer's
// hw_bank/hw_prog reference - opened from renderToneLayerEditor() when the
// user clicks the resolved bank/patch-name label, mirroring how
// SwPatchPickerState (D-034) handles HwPatch's own sw_bank/sw_prog. Also
// writes voice_patch_type on selection (the picked HwBank's own chip-family
// tag) since a ToneLayer's hw_bank/hw_prog only make sense together with the
// chip family they belong to - picking a patch is really picking "one of
// this bank's patches", not just a bare number pair.
//
// Stores {layeredBankIndex, layeredPatchProg, layerIndex} rather than a raw
// fpe::ToneLayer*, matching PatchEditorWindow/SwPatchPickerState's own
// "store indices, re-derive every frame" convention - safer if the
// underlying vectors are ever reallocated while the picker is open.
struct HwPatchPickerState {
    bool open = false;
    size_t layeredBankIndex = 0; // index into ws.layeredPatchBanks()
    int layeredPatchProg = 0;    // Patch::prog within that bank
    int layerIndex = 0;         // index into Patch::layers

    // Drill-down navigation state (see PatchPickerLevel). No "レイヤード"
    // category here (unlike DrumSourcePatchPickerState) - a ToneLayer's
    // hw_bank/hw_prog can only ever reference a device voice patch.
    PatchPickerLevel level = PatchPickerLevel::Category;
    fpe::VoicePatchType category = fpe::VoicePatchType::None; // chosen chip family
    int bank = 0; // chosen HwBank::bankIndex within that category
};

// "ソースパッチ" picker for a DrumNote's (or, when isDirect, a whole "direct"
// DrumKit's) voice_patch_type/patch_bank/patch_prog triple - D-038. Unlike
// HwPatchPickerState (ToneLayer, HW/device patches only) or SwPatchPickerState
// (a performance-patch override), this reference has the same dual
// "normal mode vs direct mode" semantics as CC#0 itself (DrumKit.h): when
// voice_patch_type == None, patch_bank/patch_prog index a layered
// PatchBank/Patch; otherwise they index a HwBank/HwPatch directly. So the
// picker lists both trees (layered patches and device voice patches) in one
// popup rather than picking one HW bank kind up front, matching what CC#0
// actually lets a real DrumNote reference.
struct DrumSourcePatchPickerState {
    bool open = false;
    size_t kitIndex = 0; // index into ws.drumKits()
    bool isDirect = false; // true: target is the DrumKit's own fields (kit.patch_bank etc); false: target is a DrumNote
    uint8_t note = 0;     // when !isDirect: DrumNote::note within that kit

    // Drill-down navigation state (see PatchPickerLevel). category==None IS
    // a real, meaningful Category-level choice here ("レイヤード"), not an
    // "unset" sentinel like HwPatchPickerState's - this exactly mirrors
    // FITOM_X本体's PatchPickerDialog's own kCategories[0] ("レイヤード",
    // CC#0=0).
    PatchPickerLevel level = PatchPickerLevel::Program;
    fpe::VoicePatchType category = fpe::VoicePatchType::None;
    int bank = 0;
};

// On-screen keyboard popup for a DrumNote's play_note field (D-038),
// alongside the by-name dropdown in renderDrumNoteEditor() - per the project
// owner's request that play_note be settable either by note name or by
// clicking a keyboard, not by typing a raw number. `baseNote` is the
// picker's own scroll position (which octave range is currently shown),
// independent of the note being edited, so paging through octaves doesn't
// fight with the value being picked.
//
// D-045 widened the keyboard to 5 octaves and changed the click semantics:
// a single click no longer commits play_note immediately - it previews the
// clicked pitch (through the note's current source patch, reusing D-044's
// one-shot list-preview mechanism) and records it as `selectedNote`, a
// pending choice that only actually gets written to play_note by a double
// click on a key or the new "OK" button - so a single click can be used
// purely to audition pitches without any risk of accidentally changing the
// value.
struct DrumNoteKeyboardPickerState {
    bool open = false;
    size_t kitIndex = 0;  // index into ws.drumKits()
    uint8_t note = 0;     // DrumNote::note within that kit (the note being edited, routed kits only)
    int baseNote = 36;    // C2 - leftmost key currently shown, always a C
    int selectedNote = -1; // pending choice (D-045) - -1 only if never initialized; see openDrumNoteKeyboardPicker()
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

// Kiosk-only "the whole drum kit is the top-level screen" state (D-040).
// Unlike the other three kiosk kinds, this doesn't reuse a pre-existing
// modeless-editor-window struct (PatchEditorWindow/LayeredPatchEditorWindow/
// PerformancePatchEditorWindow) - BankDetail's Drum case never had one of its
// own, since a DrumKit's routed-note-list/direct-inline content was rendered
// straight into renderBankDetail() itself rather than through a separate
// window (see renderDrumKitDetail(), factored out of that switch case so
// kiosk mode can reuse the exact same content). No `prog` field either: a
// *.drumkit.json file already is one whole DrumKit (unlike HwBank/PatchBank/
// SwBank, which hold several patches keyed by prog within one file). Note
// that the file alone is NOT enough to pick a kiosk target, though - since
// D-041 (shared "banks" + "bank_overrides"), the exact same *.drumkit.json
// can legitimately be registered at two different prog numbers at once (the
// shared base registers it at its usual prog, and a profile's
// bank_overrides additionally/separately maps it onto a different prog for
// that one profile) - findDrumKitIndexByFile() takes both file and prog.
struct KioskDrumKitWindow {
    bool open = true;
    size_t kitIndex = 0; // index into ws.drumKits()
};

// One-shot preview state for a single click on a drum-note list row
// (renderDrumKitDetail(), D-044) - distinct from the note editor's own
// press-and-hold "試聴" button (D-038), since a plain click has no
// "release" event to key off of; instead this auto-stops after
// kDrumNoteListPreviewDuration via updateDrumNoteListPreview(), called once
// per frame regardless of screen/kiosk-vs-normal (main()'s render loop) so
// a note can't keep sounding after the user navigates away or the app
// otherwise stops polling this specific screen.
struct DrumNoteListPreviewState {
    bool active = false;
    uint8_t channel = 0;     // channel noteOn() was actually sent on, for noteOff() to match
    uint8_t channelNote = 0; // DrumNote::play_note actually sent
    double startTime = 0.0;  // ImGui::GetTime() at noteOn() time
};

struct AppContext {
    fpe::PatchWorkspace workspace;
    AppState state = AppState::MainMenu;
    FileBrowserState browser;
    std::string errorMessage; // non-empty => error popup is showing
    NewBankDialogState newBankDialog;
    std::vector<PatchEditorWindow> openEditors;
    int nextEditorId = 0;
    std::vector<LayeredPatchEditorWindow> openLayeredEditors;
    int nextLayeredEditorId = 0;
    std::vector<PerformancePatchEditorWindow> openPerformanceEditors;
    int nextPerformanceEditorId = 0;
    std::vector<DrumNoteEditorWindow> openDrumNoteEditors;
    int nextDrumNoteEditorId = 0;
    // One shared preview output (FITOM_X's internal MIDI pipe, falling
    // back to a regular MIDI port via RtMidi - see PreviewOutput,
    // docs/DESIGN.md D-018), reused by every open patch editor's preview
    // keyboard. The pipe only allows a single client anyway
    // (docs/plugin-midi-pipe.md in the FITOM_X repo).
    PreviewOutput previewOutput;
    Preferences preferences;
    PreferencesDialogState preferencesDialog;
    PathPickerState pathPicker; // shared browse-button popup, see PathPickerState/openPathPicker()
    SwPatchPickerState swPatchPicker; // shared SW-patch picker, see SwPatchPickerState/openSwPatchPicker()
    HwPatchPickerState hwPatchPicker; // shared HW-patch picker (for ToneLayer refs), see HwPatchPickerState/openHwPatchPicker()
    DrumSourcePatchPickerState drumSourcePatchPicker; // shared drum-note source-patch picker, see openDrumSourcePatchPicker()
    DrumNoteKeyboardPickerState drumNoteKeyboardPicker; // shared drum-note play_note keyboard picker, see openDrumNoteKeyboardPicker()
    DrumNoteListPreviewState drumNoteListPreview; // one-shot single-click preview, see startDrumNoteListPreview() (D-044)

    // Selection driving the BankDetail screen - which category/index into
    // the corresponding PatchWorkspace vector. Only meaningful while
    // state == BankDetail; set together with the state transition, and
    // never touched by workspace-mutating code, so it can't go stale
    // within a single load (this GUI is still read-only, see file header).
    BankCategory selectedCategory = BankCategory::Layered;
    size_t selectedIndex = 0;

    // Kiosk mode (D-026, kind argument added D-039, Performance/Drum kinds
    // added D-040): launched with a profile + kind + bank file + prog CLI
    // quadruple instead of just a profile path. When true, the main loop
    // skips MainMenu/Outline/BankDetail/dialogs entirely and renders only
    // the one editor matching kioskKind, full-viewport - see main()'s render
    // loop. Closing it (its own title-bar X) exits the whole process
    // immediately, matching "パッチ編集を終了したらそのまま終了する" -
    // there is no menu to fall back to in this mode.
    // kioskKind selects which of the four dedicated editor slots below is
    // actually live (only one is ever populated per run); restricted to
    // Device/Layered/Performance/Drum (the kinds a CLI caller can currently
    // name and get an actual screen for - see parseKioskKind()/
    // kioskKindImplemented()) even though BankCategory itself also has
    // Pcm/SampleZone, reserved as kind keywords but not wired to a screen
    // yet since those patch types have no edit form at all (D-040).
    bool kioskMode = false;
    BankCategory kioskKind = BankCategory::Device;
    PatchEditorWindow kioskEditor;
    LayeredPatchEditorWindow kioskLayeredEditor;
    PerformancePatchEditorWindow kioskPerformanceEditor;
    KioskDrumKitWindow kioskDrumEditor;
};

// A single click (drum-note list row, D-044; or a key in
// renderDrumNoteKeyboardPicker(), D-045) lasts one frame - there is no
// "release" to key a noteOff off of the way the note editor's press-and-hold
// "試聴" button does (D-038) - so this fixed duration stands in for that,
// short enough to feel like a quick preview blip rather than a sustained
// note.
constexpr double kDrumNoteListPreviewDuration = 0.4;

// Sends noteOff for whatever's currently previewing via
// startDrumNoteListPreview(), if anything - safe to call unconditionally.
// Used both to auto-stop after kDrumNoteListPreviewDuration and to cut a
// still-sounding preview short when a new one starts (D-044) - previews
// never overlap/stack.
void stopDrumNoteListPreview(AppContext& ctx) {
    DrumNoteListPreviewState& p = ctx.drumNoteListPreview;
    if (!p.active) return;
    ctx.previewOutput.noteOff(p.channel, p.channelNote, 0);
    p.active = false;
}

// Sends a DrumNote's own performance-patch override, if it has one, via the
// SwPatch override SysEx (docs/manuals/midi-message-reference.md 8.1) - D-046.
//
// Investigated against the real FITOM_X source (core/src/PatchManager.cpp,
// core/src/MidiCh.cpp) after a report that drum preview didn't match real
// rhythm-track playback: a plain CC#0/32/PC direct-device-select (what
// selectDevice() sends) DOES automatically resolve and apply the selected
// HwPatch's own default sw_bank/sw_prog (PatchManager::resolveDirect()) - so
// that part was already correct. But a DrumNote's own sw_bank/sw_prog is a
// drum-map-specific OVERRIDE of that default, and it is only ever consulted
// by CRhythmCh::resolveNote() - a rhythm-channel-specific code path that a
// plain direct-device-select on an ordinary channel (which is what every
// preview in this editor uses, so unsaved in-memory edits stay audible
// without round-tripping through FITOM_X's own loaded profile) never
// reaches. So the override has to be pushed explicitly, exactly like this
// editor already does for HwPatch parameters (buildHwPatchOverrideJson()/
// sendHwPatchOverride(), D-027) - `fpe::to_json(SwPatch)`'s shape already
// matches the wire format's own example verbatim (`{"sw":{...},"ops":[...],
// "fine_transpose":...}`, extra keys like "prog"/"name" are simply ignored
// per the doc), so no separate flattening builder is needed here the way
// HwPatch's was. Must be sent before the note-on it's meant to affect - the
// doc states a SwPatch override "以後のノートオンから反映されます" (applies
// starting from the next note-on, not retroactively to an already-sounding
// note). No-op if the note has no override (sw_bank/sw_prog == -1) - in that
// case the resolved HwPatch's own default sw_bank/sw_prog is already in
// effect from the selectDevice() call, so there's nothing to send.
void sendDrumNoteSwPatchOverride(AppContext& ctx, uint8_t channel, const fpe::DrumNote& note) {
    if (note.sw_bank < 0 || note.sw_prog < 0) return;
    const fpe::SwPatch* swPatch = ctx.workspace.resolvePerformancePatch(note.sw_bank, note.sw_prog);
    if (swPatch) ctx.previewOutput.sendSwPatchOverride(channel, nlohmann::json(*swPatch).dump());
}

// Builds the sub-cmd=0x07 JSON payload (docs/manuals/midi-message-
// reference.md 8.1.1, D-049) for persisting one whole DrumKit's current
// notes into a running FITOM_X's own in-memory DrumPatch. Uses
// kit.effectiveNotes() rather than kit.notes directly so both "routed" and
// "direct" kits go through the same code path - for a "direct" kit this
// materializes the note_min..note_max passthrough range into individual
// note entries client-side, exactly like renderBankDetail()'s own read-only
// display already does (DrumKit.h's own comment on effectiveNotes()).
//
// Every note gets an explicit "enabled":true added on top of whatever
// fpe::to_json(DrumNote) already produces (which otherwise matches the wire
// schema's own key names exactly - voice_patch_type/patch_bank/patch_prog/
// play_note/fine_tune/pan/gate_time/sw_bank/sw_prog; extra "note" is simply
// ignored). This is NOT one of drumkit.schema.json's own fields - it's
// purely a runtime concept of FITOM_X's own fixed 128-slot DrumNote array
// (core/include/fitom/DrumData.h: `enabled` defaults to false, and
// DrumPatch::getNote() - what CRhythmCh::noteOn() calls - returns nullptr
// for a disabled slot), so a newly-added note this editor persists here
// would otherwise silently never sound despite having correct data
// everywhere else.
nlohmann::json buildDrumKitOverrideJson(const fpe::DrumKit& kit) {
    nlohmann::json notesObj = nlohmann::json::object();
    for (const auto& note : kit.effectiveNotes()) {
        nlohmann::json nj = note;
        nj["enabled"] = true;
        notesObj[std::to_string(note.note)] = nj;
    }
    nlohmann::json j;
    j["name"] = kit.name;
    j["choke_groups"] = kit.choke_groups;
    j["notes"] = notesObj;
    return j;
}

// One-shot preview triggered by a single click - either a drum-note list row
// (D-044, renderDrumKitDetail()) or a key in the play_note keyboard picker
// (D-045, renderDrumNoteKeyboardPicker()) - selects `note`'s source patch and
// sounds `note.play_note`, auto-stopping after kDrumNoteListPreviewDuration
// (see updateDrumNoteListPreview()) rather than requiring press-and-hold,
// since a plain click has no "release" event. No-op if no preview backend is
// currently available (offline).
void startDrumNoteListPreview(AppContext& ctx, const fpe::DrumNote& note) {
    stopDrumNoteListPreview(ctx);
    if (ctx.previewOutput.ensureReady() == PreviewOutput::ActiveBackend::None) return;
    const uint8_t ch = ctx.previewOutput.activeChannel(ctx.preferences.midiChannel);
    ctx.previewOutput.selectDevice(ch, static_cast<uint8_t>(note.voice_patch_type),
                                    static_cast<uint8_t>(note.patch_bank), static_cast<uint8_t>(note.patch_prog));
    sendDrumNoteSwPatchOverride(ctx, ch, note); // D-046 - must precede noteOn(), see comment above
    ctx.previewOutput.noteOn(ch, note.play_note, 100);
    DrumNoteListPreviewState& p = ctx.drumNoteListPreview;
    p.active = true;
    p.channel = ch;
    p.channelNote = note.play_note;
    p.startTime = ImGui::GetTime();
}

// Called once per frame regardless of screen/kiosk-vs-normal (main()'s
// render loop, D-044) - auto-stops a single-click preview once its fixed
// duration has elapsed. A no-op most frames (active is usually false).
void updateDrumNoteListPreview(AppContext& ctx) {
    const DrumNoteListPreviewState& p = ctx.drumNoteListPreview;
    if (p.active && ImGui::GetTime() - p.startTime >= kDrumNoteListPreviewDuration) {
        stopDrumNoteListPreview(ctx);
    }
}

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

// Kiosk mode (D-026) identifies its target bank by file path rather than by
// bank index, since the CLI caller (FITOM_X) knows which *.hwbank.json it's
// pointing at but not this editor's internal vector indices. Matches via
// fs::equivalent() (not string/path equality) so a relative CLI argument,
// a differently-cased path, or a path through a symlink all still match
// the same on-disk file the profile resolved to.
std::optional<size_t> findDeviceBankIndexByFile(fpe::PatchWorkspace& ws, const fs::path& hwbankFile) {
    std::error_code ec;
    for (size_t i = 0; i < ws.deviceBanks().size(); ++i) {
        if (fs::equivalent(ws.deviceBanks()[i].sourceFile, hwbankFile, ec) && !ec) {
            return i;
        }
    }
    return std::nullopt;
}

// Same idea as findDeviceBankIndexByFile(), but for a layered patch bank
// (*.patchbank.json) - added for kiosk mode's "layered" kind (D-039), which
// needs to locate the PatchBank a CLI-given file path refers to the same way
// the pre-existing "device" kind locates a HwBank.
std::optional<size_t> findLayeredBankIndexByFile(fpe::PatchWorkspace& ws, const fs::path& patchbankFile) {
    std::error_code ec;
    for (size_t i = 0; i < ws.layeredPatchBanks().size(); ++i) {
        if (fs::equivalent(ws.layeredPatchBanks()[i].sourceFile, patchbankFile, ec) && !ec) {
            return i;
        }
    }
    return std::nullopt;
}

// Same idea again, for a performance-patch bank (*.swbank.json) - kiosk
// mode's "performance" kind (D-040).
std::optional<size_t> findPerformanceBankIndexByFile(fpe::PatchWorkspace& ws, const fs::path& swbankFile) {
    std::error_code ec;
    for (size_t i = 0; i < ws.performanceBanks().size(); ++i) {
        if (fs::equivalent(ws.performanceBanks()[i].sourceFile, swbankFile, ec) && !ec) {
            return i;
        }
    }
    return std::nullopt;
}

// Same idea again, for a drum kit (*.drumkit.json) - kiosk mode's "drum"
// kind (D-040). Unlike the three above, there's no further "patch within
// this bank" step (there is no findByProg()-shaped lookup - see
// KioskDrumKitWindow's comment): one *.drumkit.json is already one whole
// DrumKit. But `prog` still has to be part of the match itself, not just a
// post-hoc sanity check against a stale caller as originally assumed
// (D-040) - D-041's "banks"/"bank_overrides" mechanism lets the very same
// file legitimately back two different prog slots at once within one
// profile's effective registry (e.g. FITOM_staging's
// opl_builtin_rhythm.drumkit.json, registered at its usual prog 13 by the
// shared unified.bankset.json AND separately mapped onto prog 0 by
// emu_opl.profile.json's own bank_overrides - both entries coexist in
// ws.drumKits()). Matching by file alone would nondeterministically grab
// whichever of the two happens to come first in ws.drumKits(), independent
// of which prog the caller actually asked for.
std::optional<size_t> findDrumKitIndexByFile(fpe::PatchWorkspace& ws, const fs::path& drumkitFile, int prog) {
    std::error_code ec;
    for (size_t i = 0; i < ws.drumKits().size(); ++i) {
        if (ws.drumKits()[i].prog == prog && fs::equivalent(ws.drumKits()[i].sourceFile, drumkitFile, ec) && !ec) {
            return i;
        }
    }
    return std::nullopt;
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

// Opens a modeless editor for the layered fpe::Patch at
// ws.layeredPatchBanks()[bankIndex]'s `prog`, or re-focuses an already-open
// one for the same patch. Mirrors openPatchEditor() for the Device (HwPatch)
// case.
void openLayeredPatchEditor(AppContext& ctx, size_t bankIndex, int prog) {
    for (auto& e : ctx.openLayeredEditors) {
        if (e.bankIndex == bankIndex && e.prog == prog) {
            e.open = true;
            ImGui::SetWindowFocus((std::string("レイヤードパッチ編集##layerededitor") + std::to_string(e.id)).c_str());
            return;
        }
    }
    LayeredPatchEditorWindow w;
    w.id = ctx.nextLayeredEditorId++;
    w.bankIndex = bankIndex;
    w.prog = prog;
    ctx.openLayeredEditors.push_back(w);
}

// Opens a modeless editor for the SwPatch at ws.performanceBanks()[bankIndex]'s
// `prog`, or re-focuses an already-open one for the same patch. Mirrors
// openPatchEditor()/openLayeredPatchEditor() for the Performance case.
void openPerformancePatchEditor(AppContext& ctx, size_t bankIndex, int prog) {
    for (auto& e : ctx.openPerformanceEditors) {
        if (e.bankIndex == bankIndex && e.prog == prog) {
            e.open = true;
            ImGui::SetWindowFocus((std::string("パフォーマンスパッチ編集##perfeditor") + std::to_string(e.id)).c_str());
            return;
        }
    }
    PerformancePatchEditorWindow w;
    w.id = ctx.nextPerformanceEditorId++;
    w.bankIndex = bankIndex;
    w.prog = prog;
    ctx.openPerformanceEditors.push_back(w);
}

// Opens a modeless editor for the DrumNote at ws.drumKits()[kitIndex] whose
// DrumNote::note == `note`, or re-focuses an already-open one for the same
// note. Mirrors openPatchEditor()/openLayeredPatchEditor()/
// openPerformancePatchEditor() for the drum-note case (D-038). Assumes the
// note already exists (renderBankDetail()'s "作成" button upserts a default
// DrumNote before calling this for a previously-unassigned note) - unlike
// those three, there's no separate "create" entry point here.
void openDrumNoteEditor(AppContext& ctx, size_t kitIndex, uint8_t note) {
    for (auto& e : ctx.openDrumNoteEditors) {
        if (e.kitIndex == kitIndex && e.note == note) {
            e.open = true;
            ImGui::SetWindowFocus((std::string("ドラムノート編集##drumnoteeditor") + std::to_string(e.id)).c_str());
            return;
        }
    }
    DrumNoteEditorWindow w;
    w.id = ctx.nextDrumNoteEditorId++;
    w.kitIndex = kitIndex;
    w.note = note;
    ctx.openDrumNoteEditors.push_back(w);
}

// A ToneLayer's hw_bank is HwBank::bankIndex (profile.json
// hw_banks[].bank), not a vector index into ws.deviceBanks() - resolving or
// opening the referenced HwPatch (from the picker or the "編集" button)
// needs a linear search keyed on {voice_patch_type, hw_bank}, mirroring
// findDeviceBankIndexByFile()'s "search by a stable key, not by vector
// position" approach.
std::optional<size_t> findDeviceBankVectorIndex(fpe::PatchWorkspace& ws, fpe::VoicePatchType type, int bankIndex) {
    auto& banks = ws.deviceBanks();
    for (size_t i = 0; i < banks.size(); ++i) {
        if (banks[i].voicePatchType == type && banks[i].bankIndex == bankIndex) return i;
    }
    return std::nullopt;
}

// Same idea as findDeviceBankVectorIndex(), but for sw_bank (SwBank::bankIndex,
// profile.json banks.sw_banks[].bank) - needed to open a
// PerformancePatchEditorWindow (openPerformancePatchEditor(), which takes a
// vector index into ws.performanceBanks()) from a resolved sw_bank/sw_prog
// reference's trailing "編集" button, mirroring ToneLayer's hw_bank "編集"
// button (renderToneLayerEditor()).
std::optional<size_t> findPerformanceBankVectorIndex(fpe::PatchWorkspace& ws, int bankIndex) {
    auto& banks = ws.performanceBanks();
    for (size_t i = 0; i < banks.size(); ++i) {
        if (banks[i].bankIndex == bankIndex) return i;
    }
    return std::nullopt;
}

// Same idea again, but for a layered PatchBank (PatchBank::bankIndex) -
// needed because a DrumNote's "normal mode" source-patch reference
// (voice_patch_type == None) indexes patch_bank/patch_prog into a layered
// PatchBank/Patch exactly like ToneLayer's sw_bank/hw_bank do (D-038, see
// describeDrumSourcePatch()/openDrumSourcePatchEditor() below).
std::optional<size_t> findLayeredPatchBankVectorIndex(fpe::PatchWorkspace& ws, int bankIndex) {
    auto& banks = ws.layeredPatchBanks();
    for (size_t i = 0; i < banks.size(); ++i) {
        if (banks[i].bankIndex == bankIndex) return i;
    }
    return std::nullopt;
}

// Display name for a CC#0 category value. fpe::voicePatchTypeToString()
// returns "?" for the built-in rhythm selector (0x70) because it is not a
// legal HwBank tag (VoicePatchType.h) - but it IS a legal
// voice_patch_type for a DrumNote (FITOM_X config_schema/drumkit.schema.json)
// and FITOM_X本体's own patch picker lists it as its own category
// ("内蔵リズム(OPNA/OPLL)", apps/fitom_gui/PatchPickerDialog.cpp's
// kCategories), so supply that label here rather than showing "?". D-050.
std::string deviceCategoryLabel(fpe::VoicePatchType type) {
    if (type == fpe::VoicePatchType::BuiltinRhythmBankSelector) return "内蔵リズム(OPNA/OPLL)";
    return fpe::voicePatchTypeToString(type);
}

// Resolves a {voice_patch_type, bank, prog} device (HW) reference into the
// pair of names shown for it, covering the two FITOM_X families that have no
// *.hwbank.json behind them at all and therefore can never be found by
// searching ws.deviceBanks() (fpe/BuiltinVoices.h, D-050):
//   - built-in rhythm (voice_patch_type 0x70): `bank` selects the chip
//     (OPN2=OPNA / OPLL=OPLL), `prog` the rhythm part
//   - OPLL-family ROM voices (bank 0 of OPLL/OPLLP/OPLLX/VRC7)
// Anything else resolves the ordinary way, through deviceBanks().
struct HwRefNames {
    std::string bankName;  // empty = unresolved
    std::string patchName; // empty = unresolved
    bool builtin = false;  // came from a synthesized registry, not from deviceBanks()
    bool editable = false; // resolved to a real HwPatch => its editor can be opened
};
HwRefNames resolveHwRefNames(fpe::PatchWorkspace& ws, fpe::VoicePatchType type, int bank, int prog) {
    HwRefNames r;
    if (type == fpe::VoicePatchType::BuiltinRhythmBankSelector) {
        r.builtin = true;
        r.bankName = fpe::builtinRhythmChipLabel(bank);
        r.patchName = fpe::builtinRhythmPartName(bank, prog);
        return r;
    }
    if (fpe::isOpllRomVoiceRef(type, bank)) {
        r.builtin = true;
        r.bankName = fpe::opllRomBankName();
        // The ROM voice name comes from `prog` itself, NOT from the
        // referencing category - FITOM_X's resolveOpllRomVoice() re-decodes
        // the chip variant out of the program number's upper 3 bits and
        // ignores CC#0 entirely, and real data relies on it (staging's
        // gm_layered_opll.patchbank.json has voice_patch_type=OPLL layers
        // pointing at hw_prog=35 = OPLLP's "Piano"). When the two disagree,
        // prefix the variant that will actually sound so the mismatch is
        // visible instead of silently misleading.
        const fpe::OpllRomVoiceRef rom = fpe::opllRomVoiceByProg(prog);
        if (rom.valid) {
            r.patchName = (rom.variant == type)
                              ? rom.name
                              : ("[" + fpe::voicePatchTypeToString(rom.variant) + "] " + rom.name);
        }
        return r;
    }
    auto idx = findDeviceBankVectorIndex(ws, type, bank);
    const fpe::HwBank* hwBank = idx ? &ws.deviceBanks()[*idx] : nullptr;
    const fpe::HwPatch* hwPatch = hwBank ? hwBank->findByProg(prog) : nullptr;
    if (hwBank) r.bankName = hwBank->name;
    if (hwPatch) {
        r.patchName = hwPatch->name;
        r.editable = true;
    }
    return r;
}

// "(N/A)" fallback shared by every resolved-reference label below.
std::string orNA(const std::string& s) { return s.empty() ? std::string("(N/A)") : s; }

// The four OPLL-family categories always have selectable content, because
// their bank 0 ROM voices are synthesized inside FITOM_X rather than loaded
// from a *.hwbank.json (fpe/BuiltinVoices.h). A picker that derives its
// category list purely from the loaded banks would therefore drop them
// entirely for a profile that registers no OPLL bank at all - and even for
// one that does (staging registers OPLL banks 1-4, never 0), the ROM voices
// would still be unreachable. FITOM_X本体's own picker sidesteps this by
// hardcoding the whole category list (PatchPickerDialog.cpp's kCategories);
// this editor keeps its data-driven list and just tops it up. D-050.
void addOpllRomCategories(std::vector<fpe::VoicePatchType>& categories) {
    for (fpe::VoicePatchType c : {fpe::VoicePatchType::OPLL, fpe::VoicePatchType::OPLLP,
                                  fpe::VoicePatchType::OPLLX, fpe::VoicePatchType::VRC7}) {
        if (std::find(categories.begin(), categories.end(), c) == categories.end()) categories.push_back(c);
    }
}

std::vector<fpe::VoicePatchType> collectDeviceCategories(const std::vector<fpe::HwBank>& banks) {
    std::vector<fpe::VoicePatchType> categories;
    for (const auto& bank : banks) {
        if (std::find(categories.begin(), categories.end(), bank.voicePatchType) == categories.end())
            categories.push_back(bank.voicePatchType);
    }
    addOpllRomCategories(categories);
    std::sort(categories.begin(), categories.end());
    return categories;
}

// Resolves and formats a DrumNote's (or a "direct" DrumKit's own)
// voice_patch_type/patch_bank/patch_prog triple for display - D-038. Shared
// by renderBankDetail()'s note list and renderDrumNoteEditor(), since both
// need the exact same "normal mode vs direct mode" resolution (see
// DrumSourcePatchPickerState's comment). Real drum kits commonly reference
// AWM/ADPCM sample banks (e.g. FITOM_staging's OPL4AWM YRW801 drum bank),
// not just ordinary chip HwBanks - those are separate PatchWorkspace vectors
// with their own shapes (fpe::PcmBank/fpe::SampleZoneBank, D-011/D-013), so
// they need their own resolution branch each, same as
// renderBankDetail()'s existing Pcm/SampleZone cases handle them separately
// from Device.
std::string describeDrumSourcePatch(fpe::PatchWorkspace& ws, fpe::VoicePatchType type, int patchBank, int patchProg) {
    if (type == fpe::VoicePatchType::BuiltinRhythmBankSelector) {
        // 内蔵リズム音源(D-050): patch_bank は「バンク番号」ではなく対象
        // チップのVoicePatchType、patch_prog はそのチップ内の楽器(=デバイス
        // チャンネル)番号。FITOM_staging の opna_builtin.drumkit.json /
        // opll_rhythm.drumkit.json が実際にこの形で使っている。
        const HwRefNames names = resolveHwRefNames(ws, type, patchBank, patchProg);
        return "内蔵リズム " + std::to_string(patchBank) + "/" + std::to_string(patchProg) + " : " +
               orNA(names.bankName) + " / " + orNA(names.patchName);
    }
    if (type == fpe::VoicePatchType::None) {
        fpe::PatchBank* bank = ws.findLayeredPatchBank(patchBank);
        const fpe::Patch* patch = bank ? bank->findByProg(patchProg) : nullptr;
        return "レイヤード " + std::to_string(patchBank) + "/" + std::to_string(patchProg) + " : " +
               (bank ? bank->name : std::string("(N/A)")) + " / " + (patch ? patch->name : std::string("(N/A)"));
    }
    if (fpe::isPcmWaveformVoicePatchType(type)) {
        // patch_prog is a plain 0-based entries[] array index for this
        // family, not a `prog` field (PcmBank.h) - findByIndex(), not
        // findByProg().
        fpe::PcmBank* bank = ws.findPcmBank(type, patchBank);
        const fpe::PcmBankEntry* entry =
            (bank && patchProg >= 0) ? bank->findByIndex(static_cast<size_t>(patchProg)) : nullptr;
        return "PCM " + fpe::voicePatchTypeToString(type) + " " + std::to_string(patchBank) + "/" +
               std::to_string(patchProg) + " : " + (bank ? bank->name : std::string("(N/A)")) + " / " +
               (entry ? entry->name : std::string("(N/A)"));
    }
    if (fpe::isSampleBasedVoicePatchType(type)) {
        fpe::SampleZoneBank* bank = ws.findSampleZoneBank(type, patchBank);
        const fpe::SampleZonePatch* patch = bank ? bank->findByProg(patchProg) : nullptr;
        return "サンプルゾーン " + fpe::voicePatchTypeToString(type) + " " + std::to_string(patchBank) + "/" +
               std::to_string(patchProg) + " : " + (bank ? bank->name : std::string("(N/A)")) + " / " +
               (patch ? patch->name : std::string("(N/A)"));
    }
    // Ordinary device voice patches - plus OPLL-family ROM voices (bank 0),
    // which resolveHwRefNames() synthesizes since they have no *.hwbank.json
    // to be found in (D-050).
    const HwRefNames names = resolveHwRefNames(ws, type, patchBank, patchProg);
    return "デバイス " + fpe::voicePatchTypeToString(type) + " " + std::to_string(patchBank) + "/" +
           std::to_string(patchProg) + " : " + orNA(names.bankName) + " / " + orNA(names.patchName);
}

// True when describeDrumSourcePatch() would resolve to something openable
// via openDrumSourcePatchEditor() - used to grey out the drum-note editor's
// "編集" button, since PCM waveform entries and AWM sample zones have no
// editor of their own to open (see openDrumSourcePatchEditor()'s comment).
// The two built-in families (D-050) have no editable JSON patch behind them
// at all - a built-in rhythm part is a fixed hardware instrument, and an
// OPLL ROM voice is synthesized inside FITOM_X (its only editable aspect,
// the performance patch bound to it, lives in the separate
// role=="builtin_swpatch_meta" bank) - so they're excluded here too, which
// is why this needs `bank` and not just `type`.
bool drumSourcePatchHasEditor(fpe::VoicePatchType type, int bank) {
    if (type == fpe::VoicePatchType::BuiltinRhythmBankSelector) return false;
    if (fpe::isOpllRomVoiceRef(type, bank)) return false;
    return !fpe::isPcmWaveformVoicePatchType(type) && !fpe::isSampleBasedVoicePatchType(type);
}

// Opens the referenced source patch's own editor (layered or device,
// whichever voice_patch_type indicates) - the drum-note editor's "編集"
// button next to its source-patch label, mirroring ToneLayer's hw_bank
// "編集" button (renderToneLayerEditor()). No-op if the reference doesn't
// resolve to anything currently loaded, or if it's a PCM waveform entry/AWM
// sample zone - neither has an edit form anywhere in this editor (PcmBank.h:
// "end users never edit this bank's *content* directly"; SampleZonePatch
// likewise only ever shown read-only in renderBankDetail()'s own
// SampleZone case) - callers should check drumSourcePatchHasEditor() first
// to grey the button out instead of leaving it a silent no-op.
void openDrumSourcePatchEditor(AppContext& ctx, fpe::VoicePatchType type, int patchBank, int patchProg) {
    if (type == fpe::VoicePatchType::None) {
        auto idx = findLayeredPatchBankVectorIndex(ctx.workspace, patchBank);
        if (idx) openLayeredPatchEditor(ctx, *idx, patchProg);
    } else if (drumSourcePatchHasEditor(type, patchBank)) {
        auto idx = findDeviceBankVectorIndex(ctx.workspace, type, patchBank);
        if (idx) openPatchEditor(ctx, *idx, patchProg);
    }
}

// Finds the smallest unused MIDI note (0-127) strictly after `fromNote`,
// wrapping around to 0 if none is found above it - used by the "複製" button
// in renderBankDetail()'s drum-note list (D-038) to auto-assign the copy's
// note number without asking the user, matching this codebase's existing
// "one past the current max" auto-numbering convention for new banks
// (nextBankIndex()/nextDeviceBankIndex()/nextDrumProg() above), adapted here
// to "next free slot" since note numbers are a fixed 0-127 range rather than
// an open-ended counter. Returns -1 if every one of the 128 notes is already
// assigned.
int nextFreeDrumNote(fpe::DrumKit& kit, uint8_t fromNote) {
    for (int offset = 1; offset <= 128; ++offset) {
        const int candidate = (fromNote + offset) % 128;
        if (!kit.findNote(static_cast<uint8_t>(candidate))) return candidate;
    }
    return -1;
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
        case NewBankType::Layered:
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
            case NewBankType::Layered:
                ws.createLayeredPatchBank(nextBankIndex(ws.layeredPatchBanks()), name, relFile);
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
        static const char* kTypeLabels[] = {"レイヤードパッチ", "デバイスパッチ", "パフォーマンスパッチ", "ドラムキット"};
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

// Primes a SwPatchPickerState's Bank/Program drill-down state from the
// reference's *current* bank value, mirroring FITOM_X本体's own
// PatchPickerDialog::open(): jump straight to the Program level showing the
// bank already referenced (if any), rather than forcing the user back
// through the Bank level every time they reopen the picker on an
// already-set reference. -1 (not set yet, HwPatch/Patch/DrumNote's own
// convention) starts at the Bank level instead, same as if none had been
// picked.
void primeSwPatchPickerLevel(SwPatchPickerState& p, int currentBank) {
    if (currentBank >= 0) {
        p.bank = currentBank;
        p.level = PatchPickerLevel::Program;
    } else {
        p.level = PatchPickerLevel::Bank;
    }
}

// Points the shared SwPatchPickerState at the HwPatch identified by
// {deviceBankIndex, devicePatchProg} and opens the picker. Called from
// renderPatchEditor() when the user clicks the sw_bank/sw_prog label.
void openSwPatchPicker(AppContext& ctx, size_t deviceBankIndex, int devicePatchProg) {
    SwPatchPickerState& p = ctx.swPatchPicker;
    p.target = SwPatchPickerTarget::Device;
    p.deviceBankIndex = deviceBankIndex;
    p.devicePatchProg = devicePatchProg;
    auto& deviceBanks = ctx.workspace.deviceBanks();
    fpe::HwPatch* hwPatch = deviceBankIndex < deviceBanks.size() ? deviceBanks[deviceBankIndex].findByProg(devicePatchProg) : nullptr;
    primeSwPatchPickerLevel(p, hwPatch ? hwPatch->sw_bank : -1);
    p.open = true;
}

// Same picker, repointed at a layered fpe::Patch's own sw_bank/sw_prog
// (D-036) instead of a HwPatch's. Called from renderLayeredPatchEditor()
// when the user clicks that patch's sw_bank/sw_prog label.
void openLayeredSwPatchPicker(AppContext& ctx, size_t layeredBankIndex, int layeredPatchProg) {
    SwPatchPickerState& p = ctx.swPatchPicker;
    p.target = SwPatchPickerTarget::Layered;
    p.layeredBankIndex = layeredBankIndex;
    p.layeredPatchProg = layeredPatchProg;
    auto& layeredBanks = ctx.workspace.layeredPatchBanks();
    fpe::Patch* patch = layeredBankIndex < layeredBanks.size() ? layeredBanks[layeredBankIndex].findByProg(layeredPatchProg) : nullptr;
    primeSwPatchPickerLevel(p, patch ? patch->sw_bank : -1);
    p.open = true;
}

// Same picker again, repointed at a DrumNote's own sw_bank/sw_prog override
// (D-038) instead of a HwPatch's/Patch's. Called from renderDrumNoteEditor()
// when the user clicks that note's sw_bank/sw_prog label. Routed kits only -
// a "direct" kit's own sw_bank/sw_prog isn't wired to this picker (see
// docs/DESIGN.md D-038's scope note).
void openDrumNoteSwPatchPicker(AppContext& ctx, size_t kitIndex, uint8_t note) {
    SwPatchPickerState& p = ctx.swPatchPicker;
    p.target = SwPatchPickerTarget::DrumNote;
    p.drumKitIndex = kitIndex;
    p.drumNote = note;
    auto& kits = ctx.workspace.drumKits();
    fpe::DrumNote* drumNote = kitIndex < kits.size() ? kits[kitIndex].findNote(note) : nullptr;
    primeSwPatchPickerLevel(p, drumNote ? drumNote->sw_bank : -1);
    p.open = true;
}

// Modal picking a performance (SW) bank/patch - clicking a patch writes its
// {bank,prog} into the target's sw_bank/sw_prog fields.
//
// Drills down Bank->Program one level per frame, mirroring FITOM_X本体's own
// PatchPickerDialog UX (apps/fitom_gui/PatchPickerDialog.cpp in the FITOM_X
// repo) - no Category level here, since FITOM_X本体 itself has no chip-family
// grouping for performance banks (they're not chip-tagged, and FITOM_X's own
// picker has no equivalent screen for this reference kind at all - see
// docs/DESIGN.md).
//
// Re-resolves the target sw_bank/sw_prog int fields every frame from the
// indices in SwPatchPickerState (per its `target`) rather than holding a
// pointer captured at open time - if the target patch has since vanished
// (bank/patch deleted while the picker was open), the picker just closes
// itself quietly rather than dereferencing something stale.
void renderSwPatchPicker(AppContext& ctx) {
    SwPatchPickerState& p = ctx.swPatchPicker;
    if (!p.open) return;

    int* targetSwBank = nullptr;
    int* targetSwProg = nullptr;
    if (p.target == SwPatchPickerTarget::Device) {
        auto& deviceBanks = ctx.workspace.deviceBanks();
        if (p.deviceBankIndex >= deviceBanks.size()) {
            p.open = false;
            return;
        }
        fpe::HwPatch* hwPatch = deviceBanks[p.deviceBankIndex].findByProg(p.devicePatchProg);
        if (!hwPatch) {
            p.open = false;
            return;
        }
        targetSwBank = &hwPatch->sw_bank;
        targetSwProg = &hwPatch->sw_prog;
    } else if (p.target == SwPatchPickerTarget::Layered) {
        auto& layeredBanks = ctx.workspace.layeredPatchBanks();
        if (p.layeredBankIndex >= layeredBanks.size()) {
            p.open = false;
            return;
        }
        fpe::Patch* layeredPatch = layeredBanks[p.layeredBankIndex].findByProg(p.layeredPatchProg);
        if (!layeredPatch) {
            p.open = false;
            return;
        }
        targetSwBank = &layeredPatch->sw_bank;
        targetSwProg = &layeredPatch->sw_prog;
    } else {
        auto& kits = ctx.workspace.drumKits();
        if (p.drumKitIndex >= kits.size()) {
            p.open = false;
            return;
        }
        fpe::DrumNote* note = kits[p.drumKitIndex].findNote(p.drumNote);
        if (!note) {
            p.open = false;
            return;
        }
        targetSwBank = &note->sw_bank;
        targetSwProg = &note->sw_prog;
    }
    auto& swBanks = ctx.workspace.performanceBanks();

    const char* title = "パッチピッカー (SW)";
    ImGui::OpenPopup(title);
    bool stayOpen = true;
    if (ImGui::BeginPopupModal(title, &stayOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (p.level != PatchPickerLevel::Bank) {
            if (ImGui::Button("↑ 上へ")) p.level = PatchPickerLevel::Bank;
            ImGui::Separator();
        }

        ImGui::BeginChild("swpatchpickerlist", ImVec2(420, 320), true);
        if (p.level == PatchPickerLevel::Bank) {
            ImGui::TextUnformatted("参照するパフォーマンスバンクを選択してください。");
            for (auto& bank : swBanks) {
                const std::string label = "[bank " + std::to_string(bank.bankIndex) + "] " + bank.name;
                if (ImGui::Selectable(label.c_str(), bank.bankIndex == *targetSwBank)) {
                    p.bank = bank.bankIndex;
                    p.level = PatchPickerLevel::Program;
                }
            }
            if (swBanks.empty()) ImGui::TextDisabled("(パフォーマンスバンクがありません)");
        } else {
            ImGui::Text("パッチを選択してください: bank %d", p.bank);
            fpe::SwBank* bank = nullptr;
            for (auto& b : swBanks) {
                if (b.bankIndex == p.bank) { bank = &b; break; }
            }
            if (bank) {
                const bool isCurrentBank = bank->bankIndex == *targetSwBank;
                for (auto& patch : bank->patches) {
                    const std::string label = "[prog " + std::to_string(patch.prog) + "] " + patch.name;
                    const bool selected = isCurrentBank && patch.prog == *targetSwProg;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        *targetSwBank = bank->bankIndex;
                        *targetSwProg = patch.prog;
                        p.open = false;
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (bank->patches.empty()) ImGui::TextDisabled("(パッチがありません)");
            } else {
                ImGui::TextDisabled("(このバンクは見つかりません)");
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("参照解除", ImVec2(120, 0))) {
            *targetSwBank = -1;
            *targetSwProg = -1;
            p.open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(120, 0))) {
            p.open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!stayOpen) p.open = false;
}

// Points the shared HwPatchPickerState at the ToneLayer identified by
// {layeredBankIndex, layeredPatchProg, layerIndex} and opens the picker.
// Called from renderToneLayerEditor() when the user clicks the
// hw_bank/hw_prog label. Primes the drill-down navigation state
// (PatchPickerLevel) from the ToneLayer's *current* value, same as FITOM_X本体's
// PatchPickerDialog::open() - if it already has a real chip-family tag, jump
// straight to the Program level showing that bank's contents (so re-opening
// the picker on an already-set reference doesn't force reselecting
// category/bank); otherwise (freshly created ToneLayer, voice_patch_type
// still the None default) start at the top, Category level.
void openHwPatchPicker(AppContext& ctx, size_t layeredBankIndex, int layeredPatchProg, int layerIndex) {
    HwPatchPickerState& p = ctx.hwPatchPicker;
    p.layeredBankIndex = layeredBankIndex;
    p.layeredPatchProg = layeredPatchProg;
    p.layerIndex = layerIndex;

    p.level = PatchPickerLevel::Category;
    auto& layeredBanks = ctx.workspace.layeredPatchBanks();
    if (layeredBankIndex < layeredBanks.size()) {
        fpe::Patch* patch = layeredBanks[layeredBankIndex].findByProg(layeredPatchProg);
        if (patch && layerIndex >= 0 && static_cast<size_t>(layerIndex) < patch->layers.size()) {
            const fpe::ToneLayer& layer = patch->layers[static_cast<size_t>(layerIndex)];
            if (layer.voice_patch_type != fpe::VoicePatchType::None) {
                p.category = layer.voice_patch_type;
                p.bank = layer.hw_bank;
                p.level = PatchPickerLevel::Program;
            }
        }
    }
    p.open = true;
}

// Modal picking a device (HW) voice patch for a ToneLayer's hw_bank/hw_prog -
// clicking a patch writes its {voice_patch_type, bank, prog} into the target
// ToneLayer's fields. Scoped to HW/device patches only (per the project
// owner's request - ToneLayer's other reference kind, Patch::sw_bank/sw_prog,
// is not this picker).
//
// Drills down Category(チップファミリー)->Bank->Program one level per frame,
// mirroring FITOM_X本体's own PatchPickerDialog (apps/fitom_gui/
// PatchPickerDialog.cpp in the FITOM_X repo) rather than showing every bank's
// patches in one always-expanded tree - real profiles can have dozens of
// device banks spread across a dozen+ chip families (see
// ../FITOM_staging/banks/), which made the old flat tree hard to browse.
//
// Re-resolves the target ToneLayer* every frame from {layeredBankIndex,
// layeredPatchProg, layerIndex} rather than holding a pointer captured at
// open time (same reasoning as renderSwPatchPicker()) - if the target
// patch/layer has since vanished, the picker just closes itself quietly.
void renderHwPatchPicker(AppContext& ctx) {
    HwPatchPickerState& p = ctx.hwPatchPicker;
    if (!p.open) return;

    auto& layeredBanks = ctx.workspace.layeredPatchBanks();
    if (p.layeredBankIndex >= layeredBanks.size()) {
        p.open = false;
        return;
    }
    fpe::Patch* patch = layeredBanks[p.layeredBankIndex].findByProg(p.layeredPatchProg);
    if (!patch || p.layerIndex < 0 || static_cast<size_t>(p.layerIndex) >= patch->layers.size()) {
        p.open = false;
        return;
    }
    fpe::ToneLayer& target = patch->layers[static_cast<size_t>(p.layerIndex)];
    auto& hwBanks = ctx.workspace.deviceBanks();

    const char* title = "パッチピッカー (HW)";
    ImGui::OpenPopup(title);
    bool stayOpen = true;
    if (ImGui::BeginPopupModal(title, &stayOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (p.level != PatchPickerLevel::Category) {
            if (ImGui::Button("↑ 上へ")) {
                p.level = (p.level == PatchPickerLevel::Program) ? PatchPickerLevel::Bank : PatchPickerLevel::Category;
            }
            ImGui::Separator();
        }

        ImGui::BeginChild("hwpatchpickerlist", ImVec2(480, 360), true);
        if (p.level == PatchPickerLevel::Category) {
            ImGui::TextUnformatted("チップファミリーを選択してください。");
            std::vector<fpe::VoicePatchType> categories = collectDeviceCategories(hwBanks);
            for (fpe::VoicePatchType c : categories) {
                const std::string label = fpe::voicePatchTypeToString(c);
                if (ImGui::Selectable(label.c_str(), c == target.voice_patch_type)) {
                    p.category = c;
                    p.bank = 0;
                    p.level = PatchPickerLevel::Bank;
                }
            }
            if (categories.empty()) ImGui::TextDisabled("(デバイスパッチバンクがありません)");
        } else if (p.level == PatchPickerLevel::Bank) {
            ImGui::Text("バンクを選択してください: [%s]", fpe::voicePatchTypeToString(p.category).c_str());
            bool any = false;
            // Synthesized OPLL-family ROM bank 0 first, mirroring FITOM_X's
            // own FITOMBridge::getHwBankList() (D-050).
            const bool hasOpllRom = fpe::opllRomVariantSel(p.category) >= 0;
            if (hasOpllRom) {
                any = true;
                const std::string label = "[bank 0] " + std::string(fpe::opllRomBankName());
                const bool selected = p.category == target.voice_patch_type && target.hw_bank == 0;
                if (ImGui::Selectable(label.c_str(), selected)) {
                    p.bank = 0;
                    p.level = PatchPickerLevel::Program;
                }
            }
            for (auto& bank : hwBanks) {
                if (bank.voicePatchType != p.category) continue;
                // FITOM_X's resolveTriple() always routes OPLL-family bank 0
                // to the ROM voices, so a JSON bank 0 registered for one of
                // those families would never actually sound - hide it rather
                // than offer an unreachable duplicate (same precedence rule
                // as getHwBankList()'s own `if (hasOpllRom && bankNo == 0)`).
                if (hasOpllRom && bank.bankIndex == 0) continue;
                any = true;
                const std::string label = "[bank " + std::to_string(bank.bankIndex) + "] " + bank.name;
                const bool selected = bank.voicePatchType == target.voice_patch_type && bank.bankIndex == target.hw_bank;
                if (ImGui::Selectable(label.c_str(), selected)) {
                    p.bank = bank.bankIndex;
                    p.level = PatchPickerLevel::Program;
                }
            }
            if (!any) ImGui::TextDisabled("(このチップファミリーのバンクがありません)");
        } else if (fpe::isOpllRomVoiceRef(p.category, p.bank)) {
            ImGui::Text("パッチを選択してください: [%s] %s", fpe::voicePatchTypeToString(p.category).c_str(),
                        fpe::opllRomBankName());
            const bool isCurrentBank = p.category == target.voice_patch_type && target.hw_bank == 0;
            for (const auto& rom : fpe::opllRomVoices(p.category)) {
                const std::string label = "[prog " + std::to_string(rom.prog) + "] " + rom.name;
                const bool selected = isCurrentBank && rom.prog == target.hw_prog;
                if (ImGui::Selectable(label.c_str(), selected)) {
                    target.voice_patch_type = p.category;
                    target.hw_bank = 0;
                    target.hw_prog = rom.prog; // (variantSel<<4)|instIndex, not the list index
                    p.open = false;
                    ImGui::CloseCurrentPopup();
                }
            }
        } else {
            ImGui::Text("パッチを選択してください: [%s] bank %d", fpe::voicePatchTypeToString(p.category).c_str(), p.bank);
            fpe::HwBank* bank = nullptr;
            for (auto& b : hwBanks) {
                if (b.voicePatchType == p.category && b.bankIndex == p.bank) { bank = &b; break; }
            }
            if (bank) {
                const bool isCurrentBank = bank->voicePatchType == target.voice_patch_type && bank->bankIndex == target.hw_bank;
                for (auto& hwPatch : bank->patches) {
                    const std::string label = "[prog " + std::to_string(hwPatch.prog) + "] " + hwPatch.name;
                    const bool selected = isCurrentBank && hwPatch.prog == target.hw_prog;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        target.voice_patch_type = bank->voicePatchType;
                        target.hw_bank = bank->bankIndex;
                        target.hw_prog = hwPatch.prog;
                        p.open = false;
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (bank->patches.empty()) ImGui::TextDisabled("(パッチがありません)");
            } else {
                ImGui::TextDisabled("(このバンクは見つかりません)");
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("キャンセル", ImVec2(120, 0))) {
            p.open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!stayOpen) p.open = false;
}

// Points the shared DrumSourcePatchPickerState at a specific DrumNote and
// opens the picker - called from renderDrumNoteEditor() when the user
// clicks the resolved source-patch label. Primes the drill-down navigation
// state from the note's *current* {voice_patch_type,patch_bank,patch_prog}
// - unlike HwPatchPickerState/SwPatchPickerState there's no "unset" sentinel
// for this reference (a fresh DrumNote defaults to {None,0,0}, itself a
// legitimate "レイヤードパッチバンク0番prog0" selection, not a marker
// meaning "nothing chosen yet"), so this always starts at Program level -
// same as FITOM_X本体's own PatchPickerDialog::open().
void openDrumSourcePatchPicker(AppContext& ctx, size_t kitIndex, uint8_t note) {
    DrumSourcePatchPickerState& p = ctx.drumSourcePatchPicker;
    p.kitIndex = kitIndex;
    p.isDirect = false;
    p.note = note;
    auto& kits = ctx.workspace.drumKits();
    fpe::DrumNote* drumNote = kitIndex < kits.size() ? kits[kitIndex].findNote(note) : nullptr;
    p.category = drumNote ? drumNote->voice_patch_type : fpe::VoicePatchType::None;
    p.bank = drumNote ? drumNote->patch_bank : 0;
    p.level = PatchPickerLevel::Program;
    p.open = true;
}

// Same picker, repointed at a "direct" DrumKit's own patch_bank/patch_prog/
// voice_patch_type triple instead of one of its notes' - called from
// renderBankDetail()'s Drum/direct case.
void openDrumSourcePatchPickerDirect(AppContext& ctx, size_t kitIndex) {
    DrumSourcePatchPickerState& p = ctx.drumSourcePatchPicker;
    p.kitIndex = kitIndex;
    p.isDirect = true;
    auto& kits = ctx.workspace.drumKits();
    if (kitIndex < kits.size()) {
        p.category = kits[kitIndex].voice_patch_type;
        p.bank = kits[kitIndex].patch_bank;
    } else {
        p.category = fpe::VoicePatchType::None;
        p.bank = 0;
    }
    p.level = PatchPickerLevel::Program;
    p.open = true;
}

namespace {
// レイヤードパッチバンク以外の3つの発音元(デバイス/PCM波形/サンプル
// ゾーン)はいずれもfpe::VoicePatchTypeでタグ付けされており、タグの値
// 空間が重複しないため(D-011/D-013 - PCM波形/AWMは通常のHwBankとは別の
// PatchWorkspaceベクタ・別JSON形状を持つ)、カテゴリ値1つからどの
// ベクタを見ればよいかを一意に判定できる。FITOM_X本体側もこれらを
// 同じCC#0カテゴリ空間の一部として扱っている(config_schema/
// profile.schema.jsonのpcm_banks[].group注記 - PCMバンクのentries[]は
// 「パッチピッカー等で選択可能なnamed patchとして自動的にHwBankRegistry
// 側にも公開される」)。
// 内蔵リズム音源(0x70)は5つ目の発音元。上記3つと違ってPatchWorkspaceの
// どのベクタにも対応する実体が無く、patch_bank/patch_progの意味自体も
// 「バンク/プログラム」ではなく「対象チップ/楽器番号」に読み替わるため、
// 独立した種別として扱う(fpe/BuiltinVoices.h、D-050)。
enum class DrumSourceKind { Layered, Device, Pcm, SampleZone, BuiltinRhythm };
DrumSourceKind classifyDrumSourceCategory(fpe::VoicePatchType category) {
    if (category == fpe::VoicePatchType::None) return DrumSourceKind::Layered;
    if (category == fpe::VoicePatchType::BuiltinRhythmBankSelector) return DrumSourceKind::BuiltinRhythm;
    if (fpe::isPcmWaveformVoicePatchType(category)) return DrumSourceKind::Pcm;
    if (fpe::isSampleBasedVoicePatchType(category)) return DrumSourceKind::SampleZone;
    return DrumSourceKind::Device;
}
} // namespace

// Modal picking a DrumNote's (or "direct" DrumKit's) source patch - the same
// dual "normal mode vs direct mode" semantics as CC#0 itself (see
// DrumSourcePatchPickerState's comment): normal mode indexes a layered
// PatchBank/Patch, direct mode indexes one of three differently-shaped
// "device" registries (regular HwBank/HwPatch, PCM波形 PcmBank/
// PcmBankEntry, or AWM SampleZoneBank/SampleZonePatch - D-011/D-013).
//
// Drills down Category->Bank->Program one level per frame, mirroring
// FITOM_X本体's own PatchPickerDialog UX (apps/fitom_gui/
// PatchPickerDialog.cpp in the FITOM_X repo) - the Category level unifies
// "レイヤード" with every チップファミリー across all three direct-mode
// registries into one list (classifyDrumSourceCategory() picks the right
// registry once a category is chosen), same as FITOM_X本体 treats
// ADPCM-B/A・PCM-D8・AWM as just more CC#0 category values alongside the
// regular chip families (see FITOM_X's config_schema/profile.schema.json,
// referenced above).
void renderDrumSourcePatchPicker(AppContext& ctx) {
    DrumSourcePatchPickerState& p = ctx.drumSourcePatchPicker;
    if (!p.open) return;

    auto& kits = ctx.workspace.drumKits();
    if (p.kitIndex >= kits.size()) {
        p.open = false;
        return;
    }
    fpe::DrumKit& kit = kits[p.kitIndex];

    fpe::VoicePatchType* targetType = nullptr;
    int* targetBank = nullptr;
    int* targetProg = nullptr;
    if (p.isDirect) {
        targetType = &kit.voice_patch_type;
        targetBank = &kit.patch_bank;
        targetProg = &kit.patch_prog;
    } else {
        fpe::DrumNote* note = kit.findNote(p.note);
        if (!note) {
            p.open = false;
            return;
        }
        targetType = &note->voice_patch_type;
        targetBank = &note->patch_bank;
        targetProg = &note->patch_prog;
    }

    const char* title = "パッチピッカー (ドラムノート ソースパッチ)";
    ImGui::OpenPopup(title);
    bool stayOpen = true;
    if (ImGui::BeginPopupModal(title, &stayOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (p.level != PatchPickerLevel::Category) {
            if (ImGui::Button("↑ 上へ")) {
                p.level = (p.level == PatchPickerLevel::Program) ? PatchPickerLevel::Bank : PatchPickerLevel::Category;
            }
            ImGui::Separator();
        }

        ImGui::BeginChild("drumsourcepatchpickerlist", ImVec2(560, 460), true);
        if (p.level == PatchPickerLevel::Category) {
            ImGui::TextUnformatted(
                "発音元(レイヤードパッチ、またはデバイスボイスパッチ/PCM波形/サンプルゾーン/内蔵リズム)を選択してください。");
            std::vector<fpe::VoicePatchType> categories = { fpe::VoicePatchType::None };
            auto collect = [&categories](const auto& banks) {
                for (auto& bank : banks) {
                    if (std::find(categories.begin(), categories.end(), bank.voicePatchType) == categories.end())
                        categories.push_back(bank.voicePatchType);
                }
            };
            collect(ctx.workspace.deviceBanks());
            collect(ctx.workspace.pcmBanks());
            collect(ctx.workspace.sampleZoneBanks());
            // Neither of the two built-in families has a bank in any of those
            // three registries, so they have to be appended explicitly or
            // they can never be picked (D-050). 内蔵リズム(0x70) is a
            // documented DrumNote voice_patch_type value (FITOM_X
            // config_schema/drumkit.schema.json) and staging's
            // opna_builtin/opll_rhythm kits are built entirely out of it.
            addOpllRomCategories(categories);
            categories.push_back(fpe::VoicePatchType::BuiltinRhythmBankSelector);
            for (fpe::VoicePatchType c : categories) {
                const std::string label =
                    (c == fpe::VoicePatchType::None) ? "レイヤード (normal mode)" : deviceCategoryLabel(c);
                if (ImGui::Selectable(label.c_str(), c == *targetType)) {
                    p.category = c;
                    p.bank = 0;
                    p.level = PatchPickerLevel::Bank;
                }
            }
        } else if (p.level == PatchPickerLevel::Bank) {
            const std::string categoryLabel = (p.category == fpe::VoicePatchType::None)
                ? "レイヤード"
                : deviceCategoryLabel(p.category);
            ImGui::Text(p.category == fpe::VoicePatchType::BuiltinRhythmBankSelector
                            ? "対象チップを選択してください: [%s]"
                            : "バンクを選択してください: [%s]",
                        categoryLabel.c_str());
            bool any = false;
            switch (classifyDrumSourceCategory(p.category)) {
            case DrumSourceKind::Layered:
                for (auto& bank : ctx.workspace.layeredPatchBanks()) {
                    any = true;
                    const std::string label = "[bank " + std::to_string(bank.bankIndex) + "] " + bank.name;
                    const bool selected = *targetType == fpe::VoicePatchType::None && bank.bankIndex == *targetBank;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        p.bank = bank.bankIndex;
                        p.level = PatchPickerLevel::Program;
                    }
                }
                break;
            case DrumSourceKind::Device: {
                // Synthesized OPLL-family ROM bank 0 first, and hide any JSON
                // bank 0 of the same family behind it - same precedence as
                // FITOM_X's resolveTriple()/getHwBankList() (D-050,
                // renderHwPatchPicker() does the identical thing).
                const bool hasOpllRom = fpe::opllRomVariantSel(p.category) >= 0;
                if (hasOpllRom) {
                    any = true;
                    const std::string label = "[bank 0] " + std::string(fpe::opllRomBankName());
                    const bool selected = p.category == *targetType && *targetBank == 0;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        p.bank = 0;
                        p.level = PatchPickerLevel::Program;
                    }
                }
                for (auto& bank : ctx.workspace.deviceBanks()) {
                    if (bank.voicePatchType != p.category) continue;
                    if (hasOpllRom && bank.bankIndex == 0) continue;
                    any = true;
                    const std::string label = "[bank " + std::to_string(bank.bankIndex) + "] " + bank.name;
                    const bool selected = bank.voicePatchType == *targetType && bank.bankIndex == *targetBank;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        p.bank = bank.bankIndex;
                        p.level = PatchPickerLevel::Program;
                    }
                }
                break;
            }
            case DrumSourceKind::BuiltinRhythm:
                // "Bank" here is the target chip, not a bank number
                // (PatchManager::resolveBuiltinRhythm()'s chipSel) - D-050.
                for (const auto& chip : fpe::builtinRhythmChips()) {
                    any = true;
                    const int chipSel = static_cast<int>(chip.chipSel);
                    const std::string label = "[chip " + std::to_string(chipSel) + "] " + chip.label;
                    const bool selected = *targetType == p.category && chipSel == *targetBank;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        p.bank = chipSel;
                        p.level = PatchPickerLevel::Program;
                    }
                }
                break;
            case DrumSourceKind::Pcm:
                for (auto& bank : ctx.workspace.pcmBanks()) {
                    if (bank.voicePatchType != p.category) continue;
                    any = true;
                    const std::string label = "[bank " + std::to_string(bank.bankIndex) + "] " + bank.name;
                    const bool selected = bank.voicePatchType == *targetType && bank.bankIndex == *targetBank;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        p.bank = bank.bankIndex;
                        p.level = PatchPickerLevel::Program;
                    }
                }
                break;
            case DrumSourceKind::SampleZone:
                for (auto& bank : ctx.workspace.sampleZoneBanks()) {
                    if (bank.voicePatchType != p.category) continue;
                    any = true;
                    const std::string label = "[bank " + std::to_string(bank.bankIndex) + "] " + bank.name;
                    const bool selected = bank.voicePatchType == *targetType && bank.bankIndex == *targetBank;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        p.bank = bank.bankIndex;
                        p.level = PatchPickerLevel::Program;
                    }
                }
                break;
            }
            if (!any) ImGui::TextDisabled("(このカテゴリのバンクがありません)");
        } else {
            const std::string categoryLabel = (p.category == fpe::VoicePatchType::None)
                ? "レイヤード"
                : deviceCategoryLabel(p.category);
            if (p.category == fpe::VoicePatchType::BuiltinRhythmBankSelector) {
                ImGui::Text("楽器を選択してください: [%s] %s", categoryLabel.c_str(),
                            orNA(fpe::builtinRhythmChipLabel(p.bank)).c_str());
            } else if (fpe::isOpllRomVoiceRef(p.category, p.bank)) {
                ImGui::Text("パッチを選択してください: [%s] %s", categoryLabel.c_str(), fpe::opllRomBankName());
            } else {
                ImGui::Text("パッチを選択してください: [%s] bank %d", categoryLabel.c_str(), p.bank);
            }
            bool found = false;
            switch (classifyDrumSourceCategory(p.category)) {
            case DrumSourceKind::Layered: {
                fpe::PatchBank* bank = nullptr;
                for (auto& b : ctx.workspace.layeredPatchBanks()) {
                    if (b.bankIndex == p.bank) { bank = &b; break; }
                }
                if (bank) {
                    found = true;
                    const bool isCurrentBank = *targetType == fpe::VoicePatchType::None && bank->bankIndex == *targetBank;
                    for (auto& patch : bank->patches) {
                        const std::string label = "[prog " + std::to_string(patch.prog) + "] " + patch.name;
                        const bool selected = isCurrentBank && patch.prog == *targetProg;
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            *targetType = fpe::VoicePatchType::None;
                            *targetBank = bank->bankIndex;
                            *targetProg = patch.prog;
                            p.open = false;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    if (bank->patches.empty()) ImGui::TextDisabled("(パッチがありません)");
                }
                break;
            }
            case DrumSourceKind::BuiltinRhythm: {
                // patch_prog は楽器(=デバイスチャンネル)番号そのもの(D-050)。
                const auto parts = fpe::builtinRhythmParts(p.bank);
                found = !parts.empty();
                const bool isCurrentChip = *targetType == p.category && p.bank == *targetBank;
                for (const auto& part : parts) {
                    const std::string label = "[prog " + std::to_string(part.prog) + "] " + part.name;
                    const bool selected = isCurrentChip && part.prog == *targetProg;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        *targetType = p.category;
                        *targetBank = p.bank;
                        *targetProg = part.prog;
                        p.open = false;
                        ImGui::CloseCurrentPopup();
                    }
                }
                break;
            }
            case DrumSourceKind::Device: {
                if (fpe::isOpllRomVoiceRef(p.category, p.bank)) {
                    // 合成されたROM音色バンク0(D-050)。progは配列添字ではなく
                    // (variantSel<<4)|instIndex を書き込む。
                    found = true;
                    const bool isCurrentBank = p.category == *targetType && *targetBank == 0;
                    for (const auto& rom : fpe::opllRomVoices(p.category)) {
                        const std::string label = "[prog " + std::to_string(rom.prog) + "] " + rom.name;
                        const bool selected = isCurrentBank && rom.prog == *targetProg;
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            *targetType = p.category;
                            *targetBank = 0;
                            *targetProg = rom.prog;
                            p.open = false;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    break;
                }
                fpe::HwBank* bank = nullptr;
                for (auto& b : ctx.workspace.deviceBanks()) {
                    if (b.voicePatchType == p.category && b.bankIndex == p.bank) { bank = &b; break; }
                }
                if (bank) {
                    found = true;
                    const bool isCurrentBank = bank->voicePatchType == *targetType && bank->bankIndex == *targetBank;
                    for (auto& hwPatch : bank->patches) {
                        const std::string label = "[prog " + std::to_string(hwPatch.prog) + "] " + hwPatch.name;
                        const bool selected = isCurrentBank && hwPatch.prog == *targetProg;
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            *targetType = bank->voicePatchType;
                            *targetBank = bank->bankIndex;
                            *targetProg = hwPatch.prog;
                            p.open = false;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    if (bank->patches.empty()) ImGui::TextDisabled("(パッチがありません)");
                }
                break;
            }
            case DrumSourceKind::Pcm: {
                fpe::PcmBank* bank = nullptr;
                for (auto& b : ctx.workspace.pcmBanks()) {
                    if (b.voicePatchType == p.category && b.bankIndex == p.bank) { bank = &b; break; }
                }
                if (bank) {
                    found = true;
                    const bool isCurrentBank = bank->voicePatchType == *targetType && bank->bankIndex == *targetBank;
                    for (size_t i = 0; i < bank->entries.size(); ++i) {
                        const std::string label = "[" + std::to_string(i) + "] " + bank->entries[i].name;
                        const bool selected = isCurrentBank && static_cast<int>(i) == *targetProg;
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            *targetType = bank->voicePatchType;
                            *targetBank = bank->bankIndex;
                            *targetProg = static_cast<int>(i);
                            p.open = false;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    if (bank->entries.empty()) ImGui::TextDisabled("(エントリがありません)");
                }
                break;
            }
            case DrumSourceKind::SampleZone: {
                fpe::SampleZoneBank* bank = nullptr;
                for (auto& b : ctx.workspace.sampleZoneBanks()) {
                    if (b.voicePatchType == p.category && b.bankIndex == p.bank) { bank = &b; break; }
                }
                if (bank) {
                    found = true;
                    const bool isCurrentBank = bank->voicePatchType == *targetType && bank->bankIndex == *targetBank;
                    for (auto& patch : bank->patches) {
                        const std::string label = "[prog " + std::to_string(patch.prog) + "] " + patch.name;
                        const bool selected = isCurrentBank && patch.prog == *targetProg;
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            *targetType = bank->voicePatchType;
                            *targetBank = bank->bankIndex;
                            *targetProg = patch.prog;
                            p.open = false;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    if (bank->patches.empty()) ImGui::TextDisabled("(パッチがありません)");
                }
                break;
            }
            }
            if (!found) {
                ImGui::TextDisabled(p.category == fpe::VoicePatchType::BuiltinRhythmBankSelector
                                        ? "(この対象チップには内蔵リズムがありません)"
                                        : "(このバンクは見つかりません)");
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
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
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "MIDI出力ポートのフォールバック時のみ使用\n"
                "(FITOM_Xの内部パイプに接続できている場合はFITOM_X側が\n"
                "接続ごとに自動でチャンネルを割り当てるため、この設定は\n"
                "使われません)");
        }

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

// Renders one editable ToneLayer row inside renderLayeredPatchEditor():
// enabled checkbox, note range, transpose/volume/pan offsets, and the
// hw_bank/hw_prog device-voice-patch reference. Per the project owner's
// request, that reference is shown as a resolved "group/bank/patch name"
// label (not raw hw_bank/hw_prog integers) with a clickable picker
// (renderHwPatchPicker(), HW/device patches only - mirrors the sw_bank/
// sw_prog treatment in renderPatchEditor(), D-034), plus a trailing "編集"
// button that opens the referenced HwPatch's own modeless editor
// (openPatchEditor()/renderPatchEditor()) - reusing that existing modeless
// window as the "overlay" the project owner said was an acceptable
// alternative to a true modal.
void renderToneLayerEditor(AppContext& ctx, size_t layeredBankIndex, int layeredPatchProg, int layerIndex,
                           fpe::ToneLayer& layer) {
    fpe::PatchWorkspace& ws = ctx.workspace;
    ImGui::PushID(layerIndex);

    ImGui::Text("ToneLayer %d", layerIndex);
    ImGui::SameLine();
    ImGui::Checkbox("有効", &layer.enabled);

    int noteRange[2] = {layer.note_range_lo, layer.note_range_hi};
    ImGui::SetNextItemWidth(160);
    if (ImGui::InputInt2("音域(lo-hi)", noteRange)) {
        layer.note_range_lo = static_cast<uint8_t>(std::clamp(noteRange[0], 0, 127));
        layer.note_range_hi = static_cast<uint8_t>(std::clamp(noteRange[1], 0, 127));
    }

    int transpose = layer.transpose;
    ImGui::SetNextItemWidth(120);
    if (ImGui::SliderInt("移調", &transpose, -48, 48)) layer.transpose = static_cast<int8_t>(transpose);
    ImGui::SameLine();
    int volumeOffset = layer.volume_offset;
    ImGui::SetNextItemWidth(120);
    if (ImGui::SliderInt("音量オフセット", &volumeOffset, -64, 63)) layer.volume_offset = static_cast<int8_t>(volumeOffset);
    ImGui::SameLine();
    int panOffset = layer.pan_offset;
    ImGui::SetNextItemWidth(120);
    if (ImGui::SliderInt("パンオフセット", &panOffset, -64, 63)) layer.pan_offset = static_cast<int8_t>(panOffset);

    // hw_bank/hw_prog: resolved name label (click => renderHwPatchPicker())
    // + trailing "編集" button (opens the referenced HwPatch's own editor).
    // resolveHwRefNames() (not a bare deviceBanks() lookup) so an
    // OPLL-family ROM voice - bank 0, which has no *.hwbank.json at all -
    // resolves to its real name instead of "(N/A) / (N/A)"; real profiles
    // use those heavily (D-050).
    const HwRefNames names = resolveHwRefNames(ws, layer.voice_patch_type, layer.hw_bank, layer.hw_prog);
    std::optional<size_t> deviceIdx;
    if (!names.builtin) deviceIdx = findDeviceBankVectorIndex(ws, layer.voice_patch_type, layer.hw_bank);

    const std::string groupStr = deviceCategoryLabel(layer.voice_patch_type);
    const std::string hwLabel = "HW: " + groupStr + " " + std::to_string(layer.hw_bank) + "/" +
                                 std::to_string(layer.hw_prog) + " : " + orNA(names.bankName) + " / " +
                                 orNA(names.patchName);
    if (ImGui::Selectable(hwLabel.c_str(), false, 0, ImVec2(560, 0))) {
        openHwPatchPicker(ctx, layeredBankIndex, layeredPatchProg, layerIndex);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("クリックしてデバイスボイスパッチ(HW)を選択");
    ImGui::SameLine();
    ImGui::BeginDisabled(!names.editable);
    if (ImGui::Button("編集") && deviceIdx) {
        openPatchEditor(ctx, *deviceIdx, layer.hw_prog);
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::PopID();
}

// --- Patch editor (Device/HwPatch only for now - see D-015) --------------

// Per-field valid range for a given chip family, used to size sliders and
// grey out fields the chip doesn't actually read (D-016). `used=false`
// fields stay visible-but-disabled for the always-shown controls (the
// main AR/DR/SL/SR/RR/TL sliders and the WS band), so the form's layout
// doesn't jump around when switching between patches of different chip
// families - but are hidden entirely inside the OP panel's "詳細"
// fold-out (D-031), since that section doesn't have this layout-jump
// concern (one already-open editor is bound to one bank's fixed chip
// family) and showing every rarely-used field disabled for every chip
// family there was mostly clutter once OPM/OPZ added several more of
// them.
struct FieldRange {
    int minV = 0;
    int maxV = 99;
    bool used = true;
};
struct HwVoiceFieldRanges {
    FieldRange FB, ALG, AMS, PMS, NFQ, FB2;
};
struct HwOpFieldRanges {
    FieldRange AR, DR, SL, SR, RR, TL, KSR, KSL, MUL, DT1, DT2, PDT, AM, VIB, EGT, WS, REV, EGS, DT3;
};

// OPN(YM2203)/OPN2 family register widths, confirmed against FITOM_X's
// actual register-write masks in core/src/OPN_new.cpp (FB&7, ALG&7,
// DT1&7, MUL&0xF, TL&0x7F, AR/DR/SR&0x1F, KSR&3, SL/RR&0xF, EGT&0xF) and
// docs/voice-parameter-reference.md's OPN section (fields not listed
// there - DT2/KSL/PDT/AM/VIB/WS/REV/EGS/DT3/AMS/PMS/NFQ/FB2 - are unused
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
    r.PDT = {0, 0, false};
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
// MUL&0xF, TL&0x7F) and docs/hwpatch-reference.md (now explicit that
// OPL/OPLL only take the field's upper bits on the wire - AR/DR/SR/TL
// keep the same on-disk/editable width as every other chip (5bit/0-31,
// 7bit/0-127); it's the chip driver, not the schema, that discards the
// low bit(s) - D-023 briefly narrowed these to the post-shift width,
// which the user corrected back per the updated FITOM_X doc, D-024).
// DT1/DT2/EGT are explicitly called out as "無関係" for this family
// (SR/RR cover the same ground EGT would) and REV/EGS/DT3 are OPZ-only.
// WS differs per chip: OPL has no waveform register at all (always
// sine), OPL2 is 2bit (0-3), OPL3_2 (the real OPL3 chip's 2op mode) is
// 3bit (0-7) - see D-021.
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

// OPL3 (YMF262) 4OP mode - VOICE_PATCH_OPL3 (0x30), distinct from the 2OP
// residual VOICE_PATCH_OPL3_2 (0x22, oplVoiceRanges() above). Confirmed
// against core/src/OPL_new.cpp's COPL3::updateVoice(): `hw.ALG & 0x7` is
// used as a single 3bit packed value (`alValue()`) - bit0=CON1 (front pair
// M1/C1 connection), bit1=CON2 (back pair M2/C2 connection), bit2=
// ConnectionSEL (4OP link, drives the 0x104 CONNECTIONSEL register bit and
// carmsk[] carrier-mask lookup) - matching the 8 assets/alg_diagrams/
// opl3_al<0-7>.png diagrams (one per packed value) rather than the 2OP
// family's single ALG-bit0 opl_alg<0-1>.png pair. `hw.FB`/`hw.FB2` are each
// written to their own independent 0xC0 register (front/back pair) -
// `(p.hw.FB & 7)`/`(p.hw.FB2 & 7)` - unlike the 2OP family where FB2 is
// unused. AMS/PMS/NFQ aren't referenced (OPM/PSG-only fields).
HwVoiceFieldRanges opl3FourOpVoiceRanges() {
    HwVoiceFieldRanges r;
    r.FB = {0, 7, true};
    r.ALG = {0, 7, true};
    r.AMS = {0, 0, false};
    r.PMS = {0, 0, false};
    r.NFQ = {0, 0, false};
    r.FB2 = {0, 7, true};
    return r;
}
// `pdtUsed` is only true for OPL3 4op mode's front/back-pair pseudo-detune
// (ops[0]/ops[2] only - see getOpFieldRanges()'s OPL3 branch); every other
// caller in this family (OPL/OPL2/OPL3_2, none of which have a per-pair
// pseudo-detune concept per docs/voice-parameter-reference.md) leaves it at
// the default false.
HwOpFieldRanges oplOpRanges(int wsMax, bool pdtUsed = false) {
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
    r.PDT = pdtUsed ? FieldRange{-32768, 32767, true} : FieldRange{0, 0, false};
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
// modulation depth, so both stay used=true here). ALG is fixed at 0 for
// every patch this editor's common ops[]-editable layout ever shows
// (D-023 correction to D-021): hw.ALG is only read at all when
// ext.ALG_EXT bit0 (preset flag) is set (`instNo = preset ? ALG&0xF : 0`
// in core/src/OPLL_new.cpp), and preset/ROM patches are exactly the
// `isBuiltinRef()==true` case that never reaches this ops[] editor in the
// first place (see D-021's addendum on OPLL builtin banks) - so within
// this editor, ALG is always the ignored/0 user-tone case, hence
// unused=false here (not a connection selector, unlike OPL/OPL2/OPL3_2 -
// see isOplAlgFamily()). WS is a genuine but narrower field than the rest
// of the OPL family: 1 bit per operator (core/src/OPLL_new.cpp:
// `(WS&1)<<3`/`(WS&1)<<4`), not mentioned in the doc's OPLL field table
// at all - confirmed by reading the actual register-write code since the
// doc has a gap here.
HwVoiceFieldRanges opllVoiceRanges() {
    HwVoiceFieldRanges r;
    r.FB = {0, 7, true};
    r.ALG = {0, 0, false};
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

// OPM (YM2151) - confirmed against FITOM_X's docs/voice-parameter-
// reference.md OPM section and the actual register-write masks in
// core/src/OPM_new.cpp's COPM::updateVoice() (FB&7/ALG&7 in reg 0x20+ch;
// DT1&7/MUL&0xF in reg 0x40+op*8+ch; TL written unmasked - 7bit per the
// real YM2151 register, matching every other chip's TL width; KSR&3/AR
// (5bit, no mask loss - a 2026年7月 bugfix removed an erroneous extra
// shift that had been discarding AR/DR/SR's low 2 bits) in reg
// 0x80+op*8+ch; AM&1/DR(5bit) in reg 0xA0+...; DT2&3/SR(5bit) in reg
// 0xC0+...; SL(4bit)/RR(4bit) in reg 0xE0+...; AMS&3/PMS&7 via
// enableAM()/enablePM()'s reg 0x38+ch writes).
// NOTE ON CONFIDENCE: NFQ (noise frequency, reg 0x0F) isn't explicitly
// masked in the driver (`0x80 | p.hw.NFQ`), but the real YM2151's NFRQ
// register is a well-documented 5bit field - values above 31 would
// corrupt the adjacent noise-enable bit the driver ORs in, so 5bit is
// used here as the safe editable range rather than confirmed via an
// explicit source mask.
// KSL/PDT/VIB/EGT/WS/REV/EGS/DT3/FB2 aren't referenced anywhere in
// COPM::updateVoice() - marked unused=false (WS/REV/EGS/DT3 are OPZ-only,
// see opzOpRanges() below).
HwVoiceFieldRanges opmVoiceRanges() {
    HwVoiceFieldRanges r;
    r.FB = {0, 7, true};
    r.ALG = {0, 7, true};
    r.AMS = {0, 3, true};
    r.PMS = {0, 7, true};
    r.NFQ = {0, 31, true};
    r.FB2 = {0, 0, false};
    return r;
}
HwOpFieldRanges opmOpRanges() {
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
    r.DT2 = {0, 3, true};
    r.PDT = {0, 0, false};
    r.AM = {0, 1, true};
    r.VIB = {0, 0, false};
    r.EGT = {0, 0, false};
    r.WS = {0, 0, false}; // OPM has no waveform-select register at all (OPZ-only)
    r.REV = {0, 0, false};
    r.EGS = {0, 0, false};
    r.DT3 = {0, 0, false};
    return r;
}

// OPZ (YM2414)/OPZ2 (YM2424, shares the COPZ driver - FITOM_X's
// docs/chip-driver-architecture.md lists both under "COPZ（共用）") -
// COPZ derives from COPM and doesn't override hw-level field handling, so
// opzVoiceRanges() is just opmVoiceRanges() (see getVoiceFieldRanges()).
// Adds REV/EGS/WS/DT3 on the op level - core/src/OPM_new.cpp's
// COPZ::updateVoice(): `(o.WS & 7) << 4` / `(o.DT3 & 0xF)` in reg
// 0x40+op*8+ch, `(o.EGS & 0x3) << 6` / `(o.REV & 0x1F)` in reg
// 0xC0+op*8+ch.
//
// NOTE ON CONFIDENCE: docs/manuals/hwpatch-reference.md declares REV as
// 0-15(4bit)/EGS as 0-127(7bit)/DT3 as 0-15(4bit) - used below, following
// this project's established D-024 precedent (schema's declared width is
// the editable range even where a driver's actual register write masks
// away some bits). REV's driver mask (`&0x1F`, 5bit) is actually *wider*
// than the declared 4bit range, so no truncation happens there - but
// EGS's driver mask (`&0x3`, 2bit) captures only 2 of the declared 7
// bits. Whether that's a genuine FITOM_X driver bug (like the AR/DR/SR
// shift bug OPM_new.cpp's comments mention having just been fixed) or an
// intentional real YM2414 hardware limit hasn't been confirmed here -
// that's FITOM_X's own scope to resolve, not this editor's; the
// documented schema width is used as usual pending that.
HwOpFieldRanges opzOpRanges() {
    HwOpFieldRanges r = opmOpRanges();
    r.WS = {0, 7, true};
    r.REV = {0, 15, true};
    r.EGS = {0, 127, true};
    r.DT3 = {0, 15, true};
    return r;
}

// Generic wide-open fallback for chip families whose exact register widths
// haven't been confirmed against FITOM_X's source yet (D-016 tracks which
// ones still need this) - every field shown and editable, 0-99 (or the
// PDT field's full int16 range), so nothing is artificially blocked before
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
    r.PDT = {-32768, 32767, true};
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

// OPZ2 (YM2424) shares the COPZ driver with OPZ (YM2414) - FITOM_X's
// docs/chip-driver-architecture.md lists them as "共用" (shared) - so they
// take identical field ranges (opzOpRanges()).
bool isOpzFamily(fpe::VoicePatchType t) {
    return t == fpe::VoicePatchType::OPZ || t == fpe::VoicePatchType::OPZ2;
}

HwVoiceFieldRanges getVoiceFieldRanges(fpe::VoicePatchType t) {
    if (t == fpe::VoicePatchType::OPN || t == fpe::VoicePatchType::OPN2) return opnVoiceRanges();
    if (t == fpe::VoicePatchType::OPM || isOpzFamily(t)) return opmVoiceRanges();
    if (t == fpe::VoicePatchType::OPL || t == fpe::VoicePatchType::OPL2 ||
        t == fpe::VoicePatchType::OPL3_2 || t == fpe::VoicePatchType::OPL_RHY) {
        // OPL_RHY (COPLRhythm): confirmed against core/src/OPL_new.cpp's
        // updateVoice() - the FB/ALG channel register write
        // (`0xC0+physCh = 0x30 | (FB&7)<<1 | (ALG&1)`) is byte-for-byte the
        // same as plain OPL/OPL2/OPL3_2's, so it takes identical ranges
        // (and, per isOplAlgFamily() below, the same opl_alg<0-1>.png
        // series/parallel diagrams) despite being an otherwise very
        // different editing surface (see the "Inst." combo/op-count logic
        // in renderPatchEditor(), D-033).
        return oplVoiceRanges();
    }
    if (t == fpe::VoicePatchType::OPL3) return opl3FourOpVoiceRanges();
    if (isOpllFamily(t)) return opllVoiceRanges();
    return genericVoiceRanges();
}
// `opIndex` (-1 = "don't know/not applicable") only matters for OPL3 4OP
// mode's PDT field, which is a per-pair (not per-operator) concept - see
// opl3FourOpVoiceRanges()'s comment and COPL3::updateFnumber() (only
// ops[0].PDT/ops[2].PDT are ever read). Every other chip family ignores it.
HwOpFieldRanges getOpFieldRanges(fpe::VoicePatchType t, int opIndex = -1) {
    if (t == fpe::VoicePatchType::OPN || t == fpe::VoicePatchType::OPN2) return opnOpRanges();
    if (t == fpe::VoicePatchType::OPM) return opmOpRanges();
    if (isOpzFamily(t)) return opzOpRanges();
    if (t == fpe::VoicePatchType::OPL) return oplOpRanges(0);        // no WS register at all - always sine
    // OPL2 and OPL_RHY both mask WS with `& 0x3` (2bit) in their respective
    // drivers (COPL2::updateVoice()/COPLRhythm::writeOperatorRegs()) - same
    // range, reused here rather than duplicated.
    if (t == fpe::VoicePatchType::OPL2 || t == fpe::VoicePatchType::OPL_RHY) return oplOpRanges(3);
    if (t == fpe::VoicePatchType::OPL3_2) return oplOpRanges(7);     // 3bit WS (0-7)
    if (t == fpe::VoicePatchType::OPL3) {
        // 3bit WS (same register width as OPL3_2), PDT used only on the
        // front/back pair's lead operator (index 0/2).
        return oplOpRanges(7, opIndex == 0 || opIndex == 2);
    }
    if (isOpllFamily(t)) return opllOpRanges();
    return genericOpRanges();
}

// The directory the running executable itself lives in - not the current
// working directory. Same helper/precedent as Preferences.cpp's exeDir()
// (D-020); duplicated here rather than exported from Preferences.h since
// it's a small, self-contained platform shim and Preferences.h has no
// reason to expose it as part of its own interface.
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

// Locates assets/ (alg_diagrams/*.png, waveforms/*.png) by searching upward
// from the running executable's own directory (NOT the process's current
// working directory - see below) for a known marker file, same approach
// tests/smoke_test.cpp uses for fixtures/ (there, CWD is always the build
// tree's test-run directory, so the distinction doesn't matter). This one
// matters for the GUI: a plain double-click launch happens to have CWD ==
// exe dir, but invoking it from a command line with a *different* CWD (e.g.
// `bin\fitom_patch_editor_gui.exe config\profiles\foo.profile.json` run
// from one directory up) does not - searching upward from CWD in that case
// walks the wrong ancestor chain entirely and never finds the assets/
// CMakeLists.txt actually copied next to the exe, silently breaking every
// ALG/WS image (D-035, reported by the project owner reproducing exactly
// this invocation from a FITOM_staging checkout). Anchoring on exeDir()
// instead (falling back to CWD only if exeDir() itself couldn't be
// resolved) fixes this regardless of the caller's CWD.
fs::path assetsDir() {
    static fs::path cached;
    static bool resolved = false;
    if (resolved) return cached;
    resolved = true;
    fs::path p = exeDir();
    if (p.empty()) p = fs::current_path();
    for (;;) {
        if (fs::exists(p / "assets" / "alg_diagrams" / "opn_al0.png")) {
            cached = p / "assets";
            return cached;
        }
        if (!p.has_parent_path() || p == p.parent_path()) break;
        p = p.parent_path();
    }
    cached = fs::path("assets"); // not found - callers just get load failures (missing-asset, not a crash)
    return cached;
}

// Shared by every get*Texture() cache below: loads path (PNG, via
// ImageLoader.h/stb_image - D-022) and uploads it as a GL texture. Returns
// id=0 (still cached, so a missing/bad asset only fails once per run, not
// every frame) if the file is missing or fails to parse.
struct CachedTex {
    GLuint id = 0;
    int width = 0, height = 0;
};
CachedTex loadTexture(const fs::path& path) {
    CachedTex entry;
    ImageRGBA img;
    if (loadImageRgba(path.string(), img)) {
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

// Lazily loads+uploads assets/alg_diagrams/opn_al<alg>.png as a GL texture,
// caching by ALG value (0-7) so repeated frames don't re-read the file.
// Returns 0 (and caches that too, to avoid retrying every frame) if the
// asset is missing or fails to parse.
GLuint getOpnAlgTexture(int alg, int& outWidth, int& outHeight) {
    static std::unordered_map<int, CachedTex> cache;
    auto it = cache.find(alg);
    if (it == cache.end()) {
        it = cache.emplace(alg, loadTexture(assetsDir() / "alg_diagrams" / ("opn_al" + std::to_string(alg) + ".png")))
                 .first;
    }
    outWidth = it->second.width;
    outHeight = it->second.height;
    return it->second.id;
}

// Same idea as getOpnAlgTexture() but for OPL/OPL2/OPL3_2's 1bit ALG
// (0=series FM, 1=parallel/AM - assets/alg_diagrams/opl_alg<0-1>.png,
// regenerated from the real opl_al0.bmp/opl_al1.bmp reference images'
// topology, D-021). OPLL doesn't use this - see isOplAlgFamily().
GLuint getOplAlgTexture(int alg, int& outWidth, int& outHeight) {
    static std::unordered_map<int, CachedTex> cache;
    auto it = cache.find(alg);
    if (it == cache.end()) {
        it = cache.emplace(alg, loadTexture(assetsDir() / "alg_diagrams" / ("opl_alg" + std::to_string(alg) + ".png")))
                 .first;
    }
    outWidth = it->second.width;
    outHeight = it->second.height;
    return it->second.id;
}

// Same idea again, but for OPL3 (YMF262) 4OP mode's 3bit packed ALG
// (bit0=CON1 front pair/bit1=CON2 back pair/bit2=ConnectionSEL - see
// opl3FourOpVoiceRanges()'s comment) - assets/alg_diagrams/opl3_al<0-7>.png,
// one diagram per packed value. A different asset set from both
// getOpnAlgTexture() (3bit but OPN/OPM-semantics ALG) and getOplAlgTexture()
// (the 2OP family's 1bit ALG) - OPL3 4OP mode's ALG bits mean something
// chip-specific to this mode, so it gets its own images rather than reusing
// either.
GLuint getOpl3AlgTexture(int alg, int& outWidth, int& outHeight) {
    static std::unordered_map<int, CachedTex> cache;
    auto it = cache.find(alg);
    if (it == cache.end()) {
        it = cache.emplace(alg, loadTexture(assetsDir() / "alg_diagrams" / ("opl3_al" + std::to_string(alg) + ".png")))
                 .first;
    }
    outWidth = it->second.width;
    outHeight = it->second.height;
    return it->second.id;
}

// Lazily loads+uploads assets/waveforms/ws<n>.png (n=0-7) as a GL texture -
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
        it = cache.emplace(ws, loadTexture(assetsDir() / "waveforms" / ("ws" + std::to_string(ws) + ".png"))).first;
    }
    outWidth = it->second.width;
    outHeight = it->second.height;
    return it->second.id;
}

// Same idea as getWsTexture() but for OPM/OPZ/OPZ2's WS field
// (assets/waveforms/opz_ws<0-7>.png, D-031) - a *different* 8-waveform
// set from the OPL family's, sharing only some shapes (see opzOpRanges()'
// callers): WS0/2/4/6 are byte-identical reuses of the OPL family's
// ws0/ws1/ws2/ws4.png respectively (same shape, different index - hence
// separate files rather than sharing getWsTexture()'s cache/filenames),
// WS1 is E:\...\material\waveform.xlsx Sheet1's rightmost column (the
// OPZ-specific waveform); WS3/5/7 are derived from WS1 (the source
// spreadsheet has no dedicated columns for these - explicit direction
// from the project owner, see D-031): WS3 = WS1 at 2x frequency with the
// 2nd period zeroed (pulse - one cycle then silence for the rest of the
// window), WS5 = WS1 half-wave rectified (negative half clipped to 0, no
// frequency change), WS7 = WS3 full-wave rectified (abs value, so both
// of WS3's excursions become positive humps). Used for OPM too (see
// isOplWsImageFamily()'s caller in renderHwOpEditor()) since OPM's WS is
// unused=false (opmOpRanges()) - always shows the WS0 (sine) image,
// disabled/greyed via FieldRange.used, rather than a separate blank case.
GLuint getOpzWsTexture(int ws, int& outWidth, int& outHeight) {
    static std::unordered_map<int, CachedTex> cache;
    auto it = cache.find(ws);
    if (it == cache.end()) {
        it = cache.emplace(ws, loadTexture(assetsDir() / "waveforms" / ("opz_ws" + std::to_string(ws) + ".png")))
                 .first;
    }
    outWidth = it->second.width;
    outHeight = it->second.height;
    return it->second.id;
}

// Lazily loads+uploads assets/waveforms/lfo<n>.png (n=0-6) as a GL texture -
// SwPatch's LFO waveform fields (fpe::FmSwVoice::LWF - channel vibrato - and
// fpe::FmSwOp::SLW - per-operator tremolo, "same choices as LWF" per its own
// comment, so both share this one set) shown via the same image+spinner
// treatment as HwPatch's WS (renderImageSpinner(), D-021), per the project
// owner's explicit "波形についてはhwパッチのwsと同様にイメージ表示とする"
// request. Unlike ws<n>.png/opz_ws<n>.png (plotted from a real reference
// spreadsheet), these 7 images are placeholders with only the numeric index
// burned in (also per the project owner's explicit instruction - "画像は
// 数値のみ埋め込んだプレースホルダで良い。あとで人間が調整する") - the
// actual waveform shapes (up-saw/square/triangle/S&H/down-saw/delta/sine,
// see FmSwVoice::LWF's comment) are left for a human to draw later.
GLuint getLfoWaveTexture(int wf, int& outWidth, int& outHeight) {
    static std::unordered_map<int, CachedTex> cache;
    auto it = cache.find(wf);
    if (it == cache.end()) {
        it = cache.emplace(wf, loadTexture(assetsDir() / "waveforms" / ("lfo" + std::to_string(wf) + ".png"))).first;
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

// Shallow (one level) diff between two JSON objects: returns only the keys
// of `curr` whose value differs from `prev`'s same key (or is entirely
// absent from `prev`). Used by buildHwPatchDiffJson() below - fine for
// FmHwVoice/FmHwOp's flat uint8_t/int16_t fields, no nested-object diffing
// needed.
nlohmann::json shallowJsonDiff(const nlohmann::json& prev, const nlohmann::json& curr) {
    nlohmann::json diff = nlohmann::json::object();
    for (auto it = curr.begin(); it != curr.end(); ++it) {
        auto prevIt = prev.find(it.key());
        if (prevIt == prev.end() || *prevIt != it.value()) {
            diff[it.key()] = it.value();
        }
    }
    return diff;
}

// Builds a partial ("diff-only") HwPatch override - only the top-level
// hw.* keys and per-operator fields that actually changed between `prev`
// and `curr` (D-027). Per docs/manuals/midi-message-reference.md 8.1,
// this is exactly what the wire protocol expects for incremental updates
// ("オーバーライドしたいパラメータのみを含むJSONオブジェクト" - omitted
// keys mean "unchanged"); `ops[]` entries are `null` when that operator
// didn't change at all, or a partial object when only some of its fields
// did. Returns an empty object if nothing changed at all (caller should
// skip sending in that case - an empty top-level object is NOT the same
// as `{}` sent for target-type=0x00, which the doc defines as "clear the
// override entirely", so this must never be sent literally as-is).
nlohmann::json buildHwPatchDiffJson(const fpe::HwPatch& prev, const fpe::HwPatch& curr) {
    nlohmann::json diff = shallowJsonDiff(nlohmann::json(prev.hw), nlohmann::json(curr.hw));

    nlohmann::json opsArr = nlohmann::json::array();
    bool anyOpChanged = false;
    for (size_t i = 0; i < curr.ops.size(); ++i) {
        const nlohmann::json prevOpJson = (i < prev.ops.size()) ? nlohmann::json(prev.ops[i]) : nlohmann::json(fpe::FmHwOp{});
        const nlohmann::json opDiff = shallowJsonDiff(prevOpJson, nlohmann::json(curr.ops[i]));
        if (opDiff.empty()) {
            opsArr.push_back(nullptr); // unchanged - see the doc's null/{} convention above
        } else {
            opsArr.push_back(opDiff);
            anyOpChanged = true;
        }
    }
    if (anyOpChanged) diff["ops"] = opsArr;
    return diff;
}

// Live visual aid only - NOT a sample-accurate emulation of any specific
// chip's envelope generator (that's FITOM_X's job at runtime). Assumptions,
// since none of these are pinned down by an explicit range/direction in
// FmHwOp's own field comments: higher AR/DR/RR = faster (shorter visual
// ramp); TL is an attenuation (0 = loudest, max = silent - common Yamaha
// FM convention), so peak height scales from TL's own confirmed range
// (`ranges.TL.maxV`, not a hardcoded constant - D-024 fix: this used to
// normalize every chip against a fixed /99, so a chip whose real range is
// narrower than 0-99 (e.g. OPN/OPL/OPLL's 0-31 AR/DR/SR) could never
// visually reach "full speed" even at its own maximum value); SL is ALSO
// an attenuation like TL (0 = no further attenuation/sustain stays at the
// TL-scaled peak, max = fully attenuated/silent - D-025 fix: this used to
// treat SL as an absolute height with the opposite polarity and no
// relationship to TL at all). Sustain height is therefore the peak
// (already reduced by TL) further scaled down by SL's own attenuation
// fraction, not an independent value on the same 0-1 scale. If SR
// ("Sustain Rate (0 = sustain/ADSR mode, >0 = percussive mode)" per
// FmHwOp's own comment) is nonzero, the envelope doesn't hold a flat
// sustain - it keeps decaying toward 0 at a rate derived from SR instead,
// same as a real percussive (non-looping) voice would.
void renderEnvelopeCurve(const fpe::FmHwOp& op, const HwOpFieldRanges& ranges) {
    const ImVec2 size(200.0f, 70.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(20, 20, 20, 255));
    draw->AddRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(120, 120, 120, 255));

    auto rateToSegWidth = [](uint8_t rate, int maxRate, float weight) {
        const float denom = static_cast<float>(std::max(maxRate, 1));
        const float norm = 1.0f - std::min<float>(rate, maxRate) / denom; // slower rate -> wider (longer) segment
        return std::max(0.03f, norm) * weight;
    };
    // 0 = no attenuation, max = fully attenuated (TL/SL's shared convention).
    auto attenuationToNorm = [](uint8_t level, int maxLevel) {
        const float denom = static_cast<float>(std::max(maxLevel, 1));
        return std::min<float>(level, maxLevel) / denom;
    };

    const float peak = 1.0f - attenuationToNorm(op.TL, ranges.TL.maxV);
    const float sustain = peak * (1.0f - attenuationToNorm(op.SL, ranges.SL.maxV));
    const bool percussive = op.SR > 0;

    const float attackW = rateToSegWidth(op.AR, ranges.AR.maxV, size.x * 0.30f);
    const float decayW = rateToSegWidth(op.DR, ranges.DR.maxV, size.x * 0.25f);
    const float sustainW = percussive ? rateToSegWidth(op.SR, ranges.SR.maxV, size.x * 0.20f) : size.x * 0.20f;
    const float releaseW = rateToSegWidth(op.RR, ranges.RR.maxV, size.x * 0.25f);

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

// White-key count/semitone span for renderDrumNoteKeyboardPicker() - 5
// octaves (D-045, widened from the original 3) so more of the MIDI range is
// visible without paging. 36 white keys = 5*7+1 (the "+1" trailing C, same
// convention as renderPreviewKeyboard()'s other callers) = a 60-semitone
// span; `kDrumNoteKeyboardMaxBase` is the highest `baseNote` that still
// keeps the whole 36-key span within 0-127.
constexpr int kDrumNoteKeyboardWhiteKeys = 36;
constexpr int kDrumNoteKeyboardSemitoneSpan = 60;
constexpr int kDrumNoteKeyboardMaxBase = 127 - kDrumNoteKeyboardSemitoneSpan;

// Points the shared DrumNoteKeyboardPickerState at a specific DrumNote's
// play_note and opens the picker (D-038) - called from
// renderDrumNoteEditor() next to the by-name dropdown, per the project
// owner's request that play_note be settable either way. Centers the
// initial keyboard view (2 octaves below the note's own octave, so the
// current value lands roughly in the middle of the visible 5 octaves)
// around the note's current play_note (rounded down to a C). `selectedNote`
// starts at the current play_note too (D-045) - so pressing "OK" without
// clicking anything is a no-op, same value as before, rather than requiring
// a click first.
void openDrumNoteKeyboardPicker(AppContext& ctx, size_t kitIndex, uint8_t note, uint8_t currentPlayNote) {
    ctx.drumNoteKeyboardPicker.kitIndex = kitIndex;
    ctx.drumNoteKeyboardPicker.note = note;
    ctx.drumNoteKeyboardPicker.baseNote =
        std::clamp((currentPlayNote / 12) * 12 - 24, 0, kDrumNoteKeyboardMaxBase);
    ctx.drumNoteKeyboardPicker.selectedNote = currentPlayNote;
    ctx.drumNoteKeyboardPicker.open = true;
}

// Modal on-screen keyboard (renderPreviewKeyboard(), 5 octaves at a time,
// D-045) for picking a DrumNote's play_note by clicking a key instead of
// typing a number (D-038). `baseNote` pages independently of the note being
// edited via the two octave-shift buttons, since the picker's own scroll
// position and the value it's about to write are two different things.
//
// D-045 split what a key click does into two: a single click previews the
// clicked pitch (through the note's current source patch, reusing D-044's
// one-shot preview mechanism) and records it as the pending `selectedNote`,
// without touching play_note yet; only a double click on a key, or the new
// "OK" button (which commits whatever `selectedNote` currently holds),
// actually writes it - so clicking around to audition pitches can't
// accidentally change the value.
void renderDrumNoteKeyboardPicker(AppContext& ctx) {
    DrumNoteKeyboardPickerState& p = ctx.drumNoteKeyboardPicker;
    if (!p.open) return;

    auto& kits = ctx.workspace.drumKits();
    if (p.kitIndex >= kits.size()) {
        p.open = false;
        return;
    }
    fpe::DrumNote* note = kits[p.kitIndex].findNote(p.note);
    if (!note) {
        p.open = false;
        return;
    }

    const char* title = "プレイノート選択 (キーボード)";
    ImGui::OpenPopup(title);
    bool stayOpen = true;
    if (ImGui::BeginPopupModal(title, &stayOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("現在のプレイノート: %s (%d)", midiNoteName(note->play_note).c_str(), note->play_note);
        ImGui::Text("選択中: %s (%d)", midiNoteName(p.selectedNote).c_str(), p.selectedNote);
        ImGui::TextDisabled("クリックで試聴、ダブルクリックまたはOKで確定");
        ImGui::Separator();

        ImGui::BeginDisabled(p.baseNote <= 0);
        if (ImGui::ArrowButton("##octdown", ImGuiDir_Left)) p.baseNote = std::max(0, p.baseNote - 12);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%s - %s", midiNoteName(p.baseNote).c_str(),
                     midiNoteName(p.baseNote + kDrumNoteKeyboardSemitoneSpan).c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(p.baseNote >= kDrumNoteKeyboardMaxBase);
        if (ImGui::ArrowButton("##octup", ImGuiDir_Right)) {
            p.baseNote = std::min(kDrumNoteKeyboardMaxBase, p.baseNote + 12);
        }
        ImGui::EndDisabled();

        KeyboardResult kb = renderPreviewKeyboard(p.baseNote, kDrumNoteKeyboardWhiteKeys, 70.0f); // 5 octaves, D-045
        if (kb.pressedNote >= 0) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                note->play_note = static_cast<uint8_t>(kb.pressedNote);
                p.open = false;
                ImGui::CloseCurrentPopup();
            } else {
                p.selectedNote = kb.pressedNote;
                fpe::DrumNote previewNote = *note;
                previewNote.play_note = static_cast<uint8_t>(kb.pressedNote);
                startDrumNoteListPreview(ctx, previewNote);
            }
        }

        ImGui::Separator();
        ImGui::BeginDisabled(p.selectedNote < 0);
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            note->play_note = static_cast<uint8_t>(p.selectedNote);
            p.open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(120, 0))) {
            p.open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!stayOpen) p.open = false;
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
void sliderI16(const char* label, int16_t& field, int minV, int maxV) {
    int v = field;
    if (ImGui::SliderInt(label, &v, minV, maxV)) field = static_cast<int16_t>(std::clamp(v, minV, maxV));
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
// (assets/waveforms/ws<0-7>.png, D-021) to show instead of a plain number -
// OPL/OPL2/OPL3_2/OPL3/OPL_RHY (2/3bit WS - OPL3 4OP mode's WS is the same
// 3bit register width as OPL3_2's, `o.WS & 0x7` in COPL3::updateVoice(), and
// OPL_RHY's is the same 2bit width as OPL2's, `o.WS & 0x3` in
// COPLRhythm::writeOperatorRegs() - see getOpFieldRanges() - so both reuse
// this same ws<0-7>.png set rather than needing their own) and the OPLL
// family (1bit WS, confirmed from core/src/OPLL_new.cpp - see
// opllOpRanges()). Deliberately broader than the ALG-family checks in
// renderPatchEditor() (which treat OPL3 4OP mode's ALG separately from the
// 2OP family's, and exclude OPLL entirely, since OPLL's ALG isn't a
// connection selector) - WS's meaning (waveform shape) is consistent across
// all of these, only its bit width differs (already reflected in
// ranges.WS via getOpFieldRanges()).
bool isOplWsImageFamily(fpe::VoicePatchType t) {
    return t == fpe::VoicePatchType::OPL || t == fpe::VoicePatchType::OPL2 ||
           t == fpe::VoicePatchType::OPL3_2 || t == fpe::VoicePatchType::OPL3 ||
           t == fpe::VoicePatchType::OPL_RHY || isOpllFamily(t);
}

// Renders a value as an image (with the value burned into its own
// top-left corner, per D-017's ALG convention) flanked by spin buttons,
// falling back to a plain "◀ label n ▶" spinner when no texture is
// available (chip family not in scope yet, or the asset failed to load).
// Shared by ALG's channel-parameter band and WS's per-operator band
// (D-021) - `getTexture` abstracts over which asset folder/filename
// pattern to use (opl_alg<n>.png vs ws<n>.png).
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

// OPL_RHY (VOICE_PATCH_OPL_RHY, COPLRhythm) only - renders `ext.rhythm_ch`
// as a symbolic "Inst." dropdown (HH/CYM/TOM/SD/BD) rather than a bare
// number, per the explicit "設定値は数値ではなくシンボルをドロップダウン
// 等で選択する" request. The 0-4 numbering and symbol order are FITOM_X's
// own (`docs/terminology.md`'s "OPL系内蔵リズムチャンネル" section:
// `ext.rhythm_ch`, 0=HH/1=CYM/2=TOM/3=SD/4=BD - `resolveTriple()` reads
// this to force-route the patch to one of COPLRhythm's 5 fixed channels;
// it's a routing-time field, independent of `hw.ALG`, which
// COPLRhythm::queryCh() reads separately for the device's own channel
// bookkeeping). 255 ("unset") shows as no selection instead of silently
// defaulting to HH, since an unset rhythm_ch fails patch resolution
// entirely on the FITOM_X side (docs/terminology.md: "未設定(255)または
// 範囲外は解決失敗") - defaulting here would hide that this patch doesn't
// work yet.
//
// Only BD (rhythm_ch=4) is a 2-operator instrument -
// COPLRhythm::updateVoice() reads `hwOp[0]`+`hwOp[1]` for ch==4, `hwOp[0]`
// only otherwise (confirmed real data:
// FITOM_staging/banks/OPL2/msx_audio/msx_audio_preset_rhythm.hwbank.json's
// prog 0 "OPL Bass Drum" has 2 ops + rhythm_ch:4, every other prog has 1 op
// + rhythm_ch 0-3). So selecting an instrument here also resizes `ops[]` to
// match (D-033) - the per-operator panels below immediately reflect what's
// actually used, rather than leaving a stale unused OP2 around (or missing
// the one BD needs). This only fires on an explicit selection, never on a
// merely-loaded/unopened patch, so correctly-sized data from disk is never
// silently touched before the user interacts with the dropdown.
void renderRhythmInstrumentCombo(fpe::HwPatch& patch) {
    static constexpr std::pair<uint8_t, const char*> kInstruments[5] = {
        {0, "HH"}, {1, "CYM"}, {2, "TOM"}, {3, "SD"}, {4, "BD"},
    };
    const uint8_t current = patch.ext.rhythm_ch;
    const char* preview = "(未設定)";
    for (const auto& [v, label] : kInstruments) {
        if (v == current) {
            preview = label;
            break;
        }
    }
    ImGui::SetNextItemWidth(90);
    if (ImGui::BeginCombo("Inst.", preview)) {
        for (const auto& [v, label] : kInstruments) {
            const bool selected = (v == current);
            if (ImGui::Selectable(label, selected)) {
                patch.ext.rhythm_ch = v;
                patch.ops.resize((v == 4) ? 2 : 1);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void renderHwOpEditor(int index, fpe::FmHwOp& op, const HwOpFieldRanges& ranges, fpe::VoicePatchType groupType) {
    ImGui::PushID(index);
    // Height picked so the collapsed-by-default "詳細" state (title + 6
    // ADSR/TL sliders + envelope curve + WS band) fits with no internal
    // scrollbar - grew since this was first sized in D-015/D-016, mainly
    // from the WS image+spinner band D-021 added. Expanding "詳細" itself
    // still scrolls, which is fine (that's an explicit user action, not
    // the initial state the user asked to fit without scrolling).
    ImGui::BeginChild("op", ImVec2(230, 420), true);
    ImGui::Text("OP %d", index + 1);
    ImGui::Separator();
    renderEnvelopeCurve(op, ranges);
    sliderU8Ranged("AR", op.AR, ranges.AR);
    sliderU8Ranged("DR", op.DR, ranges.DR);
    sliderU8Ranged("SL", op.SL, ranges.SL);
    sliderU8Ranged("SR", op.SR, ranges.SR);
    sliderU8Ranged("RR", op.RR, ranges.RR);
    sliderU8Ranged("TL", op.TL, ranges.TL);

    // WS (waveform select) is elevated out of the "詳細" fold-out into its
    // own visible image+spinner control (like ALG's channel-band control -
    // D-017), rather than a plain number buried in the details tree, per
    // the explicit "OPパネルにWS設定を追加する" request (D-021). OPM/OPZ/
    // OPZ2 use their own waveform-image set (getOpzWsTexture(), D-031) -
    // OPM has no real WS register (opmOpRanges().WS is unused=false), but
    // still gets the same image+spinner layout, just disabled/greyed via
    // FieldRange.used (per the explicit "WS表示をOPL系と同じレイアウトで
    // 配置。ただしOPMの場合は非活性" request) rather than a separate blank
    // case.
    if (groupType == fpe::VoicePatchType::OPM || isOpzFamily(groupType)) {
        renderImageSpinner("ws", "WS", op.WS, ranges.WS, 100.0f,
                            [](int v, int& w, int& h) { return getOpzWsTexture(v, w, h); });
    } else if (isOplWsImageFamily(groupType)) {
        renderImageSpinner("ws", "WS", op.WS, ranges.WS, 100.0f,
                            [](int v, int& w, int& h) { return getWsTexture(v, w, h); });
    } else {
        inputU8Ranged("WS", op.WS, ranges.WS);
    }

    // Fields unused by the current chip family are hidden entirely here
    // (unlike the sliders/WS band above, which stay visible-but-disabled
    // per FieldRange's usual convention) - per the explicit "OPパネルの
    // 詳細バンドから...未使用のフィールドを非表示にする" request: now that
    // OPM/OPZ added several more of these (KSL/PDT/VIB/EGT are unused for
    // both, REV/EGS/DT3/WS are OPZ-only), showing every field disabled
    // for every chip family made this section mostly clutter. Each patch
    // editor window is bound to one bank's fixed chip family for its
    // whole lifetime, so this doesn't cause the "layout jumps around"
    // problem FieldRange's doc comment originally warned about (that
    // concern was about switching between different chip families, which
    // doesn't happen within one already-open editor).
    if (ImGui::TreeNode("詳細")) {
        if (ranges.KSR.used) inputU8Ranged("KSR", op.KSR, ranges.KSR);
        if (ranges.KSL.used) inputU8Ranged("KSL", op.KSL, ranges.KSL);
        if (ranges.MUL.used) inputU8Ranged("MUL", op.MUL, ranges.MUL);
        if (ranges.DT1.used) inputU8Ranged("DT1", op.DT1, ranges.DT1);
        if (ranges.DT2.used) inputU8Ranged("DT2", op.DT2, ranges.DT2);
        if (ranges.PDT.used) inputI16Ranged("PDT", op.PDT, ranges.PDT);
        if (ranges.AM.used) inputU8Ranged("AM", op.AM, ranges.AM);
        if (ranges.VIB.used) inputU8Ranged("VIB", op.VIB, ranges.VIB);
        if (ranges.EGT.used) inputU8Ranged("EGT", op.EGT, ranges.EGT);
        if (ranges.REV.used) inputU8Ranged("REV", op.REV, ranges.REV);
        if (ranges.EGS.used) inputU8Ranged("EGS", op.EGS, ranges.EGS);
        if (ranges.DT3.used) inputU8Ranged("DT3", op.DT3, ranges.DT3);
        ImGui::TreePop();
    }
    ImGui::EndChild();
    ImGui::PopID();
}

// Called once when a patch editor closes (D-027): resends the FULL
// last-registered (i.e. actually-persisted-to-disk) HwPatch, so any
// live-only edits made after the last "登録" - which were already heard
// via the realtime diff stream while the editor was open - don't leave
// FITOM_X's active preview permanently diverged from what's on disk once
// the editor showing them is gone. A no-op if this editor never actually
// rendered (editor.initialized false - e.g. its bank/prog vanished before
// the first frame) or no preview backend is available.
void sendFullRegisteredOverride(AppContext& ctx, PatchEditorWindow& editor) {
    if (!editor.initialized) return;
    auto& banks = ctx.workspace.deviceBanks();
    if (editor.bankIndex >= banks.size()) return;
    const auto& bank = banks[editor.bankIndex];
    if (ctx.previewOutput.ensureReady() == PreviewOutput::ActiveBackend::None) return;
    const uint8_t ch = ctx.previewOutput.activeChannel(ctx.preferences.midiChannel);
    ctx.previewOutput.selectDevice(ch, static_cast<uint8_t>(bank.voicePatchType), static_cast<uint8_t>(bank.bankIndex),
                                    static_cast<uint8_t>(editor.registered.prog));
    ctx.previewOutput.sendHwPatchOverride(ch, buildHwPatchOverrideJson(editor.registered).dump());
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
    const bool builtin = patch->isBuiltinRef();

    ImGui::Text("[%s bank %d prog %d]", groupStr.c_str(), bank.bankIndex, patch->prog);
    if (!builtin) {
        // Top-right per the project owner's placement request (D-027).
        // "登録" persists the whole workspace (matching the existing
        // tryCreateBank()-after-create precedent - HwBank has no
        // narrower single-bank save API) and snapshots the just-saved
        // state into editor.registered, which is what gets resent in
        // full when this editor closes (see sendFullRegisteredOverride()).
        ImGui::SameLine();
        const float buttonW = 90.0f;
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > buttonW) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - buttonW);
        if (ImGui::Button("登録", ImVec2(buttonW, 0))) {
            try {
                ctx.workspace.save();
                // D-049: also persist into a running FITOM_X's own
                // in-memory copy of this bank+prog, so it's audible in a
                // live session without restarting FITOM_X (previously
                // impossible - see D-047 - until FITOM_X's own 2026-08
                // addition of this bank-direct-edit SysEx). Fire-and-forget:
                // sendXBankOverride()/send() already no-op harmlessly if
                // nothing's connected.
                ctx.previewOutput.sendHwPatchBankOverride(static_cast<uint8_t>(bank.voicePatchType),
                                                           static_cast<uint8_t>(bank.bankIndex),
                                                           static_cast<uint8_t>(patch->prog),
                                                           buildHwPatchOverrideJson(*patch).dump());
                editor.registered = *patch;
                // D-048: close on a successful save, per the project
                // owner's request, extending D-047's drum-note-editor
                // change to the other three modeless patch editors. Not in
                // kiosk mode - closing there exits the whole process
                // (D-026), which "close the window after 登録" was never
                // asked to do; kiosk mode keeps its existing "stays open,
                // title-bar X exits" behavior.
                if (!ctx.kioskMode) editor.open = false;
            } catch (const std::exception& e) {
                ctx.errorMessage = std::string("保存に失敗しました:\n") + e.what();
            }
        }
    }

    char nameBuf[256];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", patch->name.c_str());
    if (ImGui::InputText("名前", nameBuf, sizeof(nameBuf))) patch->name = nameBuf;

    if (builtin) {
        // D-050: also resolve the reference to the ROM voice it actually
        // names. The metadata bank's own entry names are free-form (staging
        // ships the bank empty as a skeleton), so without this the row said
        // nothing about which preset it binds a performance patch to.
        const std::string romName =
            fpe::opllRomVoiceName(patch->builtin->patch_type, patch->builtin->patch_no);
        ImGui::TextWrapped(
            "内蔵ROM音色への参照(builtin)のため、ops[]による編集はできません(patch_type=%s, patch_no=%d: %s)。",
            patch->builtin->patch_type.c_str(), patch->builtin->patch_no, orNA(romName).c_str());
        return;
    }

    // Lazily captures the patch's state the first time this editor
    // actually renders it (not at construction time, since
    // openPatchEditor()/kiosk setup in main() don't have easy access to
    // the resolved HwPatch& itself) - see PatchEditorWindow's comment.
    if (!editor.initialized) {
        editor.lastSent = *patch;
        editor.registered = *patch;
        editor.initialized = true;
    }

    // sw_bank/sw_prog reference a performance (SW) patch by raw number, but
    // are shown here as a resolved "bank name / patch name" label rather
    // than bare integer fields (per the project owner's request). Clicking
    // it opens renderSwPatchPicker() (SW patches only, as instructed) to
    // repoint the reference instead of typing numbers directly.
    {
        std::string swLabel;
        const fpe::SwPatch* swPatch = nullptr;
        if (patch->sw_bank >= 0 && patch->sw_prog >= 0) {
            const fpe::SwBank* swBank = ws.findPerformanceBank(patch->sw_bank);
            swPatch = ws.resolvePerformancePatch(patch->sw_bank, patch->sw_prog);
            swLabel = "パフォーマンス: " +
                      std::to_string(patch->sw_bank) + "/" + std::to_string(patch->sw_prog) + " : " +
                      (swBank ? swBank->name : std::string("(N/A)")) + " / " +
                      (swPatch ? swPatch->name : std::string("(N/A)"));
        } else {
            swLabel = "パフォーマンス: (N/A)";
        }
        if (ImGui::Selectable(swLabel.c_str(), false, 0, ImVec2(640, 0))) {
            openSwPatchPicker(ctx, editor.bankIndex, patch->prog);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("クリックしてパフォーマンスパッチを選択");
        ImGui::SameLine();
        // Trailing "編集" button (opens the referenced SwPatch's own
        // editor), mirroring ToneLayer's hw_bank/hw_prog row
        // (renderToneLayerEditor(), D-036).
        ImGui::BeginDisabled(!swPatch);
        if (ImGui::Button("編集##swedit")) {
            auto swIdx = findPerformanceBankVectorIndex(ws, patch->sw_bank);
            if (swIdx) openPerformancePatchEditor(ctx, *swIdx, patch->sw_prog);
        }
        ImGui::EndDisabled();
    }

    const HwVoiceFieldRanges voiceRanges = getVoiceFieldRanges(bank.voicePatchType);

    ImGui::Separator();
    ImGui::Text("チャンネルパラメータ");

    // ALG is shown as its own input here - the connection-diagram image
    // (OPN/OPN2/OPM/OPZ/OPZ2: assets/alg_diagrams/opn_al<0-7>.png,
    // D-016/D-017, extended to OPM/OPZ/OPZ2 in D-031 since they share the
    // same 3bit 0-7 ALG semantics per docs/voice-parameter-reference.md;
    // OPL/OPL2/OPL3_2/OPL_RHY: opl_alg<0-1>.png, D-021 (OPL_RHY added in
    // D-033 - COPLRhythm's FB/ALG channel register write is identical to
    // plain OPL/OPL2/OPL3_2's, see getVoiceFieldRanges()) - OPLL is
    // excluded, see isOplAlgFamily()); OPL3 4OP mode: opl3_al<0-7>.png (its
    // own 3bit packed ALG semantics - CON1/CON2/ConnectionSEL - distinct
    // from both of the above, see opl3FourOpVoiceRanges()) has the current
    // ALG value burned into its own top-left corner (so the image itself
    // represents the setting, not a separate "ALG n" text widget), flanked
    // left/right by spin buttons - at the left edge of this band, rather
    // than a slider elsewhere.
    ImGui::BeginGroup();
    {
        const bool isOpnAlgFamily = bank.voicePatchType == fpe::VoicePatchType::OPN ||
                                     bank.voicePatchType == fpe::VoicePatchType::OPN2 ||
                                     bank.voicePatchType == fpe::VoicePatchType::OPM || isOpzFamily(bank.voicePatchType);
        const bool isOplAlgFamily = bank.voicePatchType == fpe::VoicePatchType::OPL ||
                                     bank.voicePatchType == fpe::VoicePatchType::OPL2 ||
                                     bank.voicePatchType == fpe::VoicePatchType::OPL3_2 ||
                                     bank.voicePatchType == fpe::VoicePatchType::OPL_RHY;
        const bool isOpl3FourOpAlgFamily = bank.voicePatchType == fpe::VoicePatchType::OPL3;
        if (isOpnAlgFamily) {
            renderImageSpinner("alg", "ALG", patch->hw.ALG, voiceRanges.ALG, 150.0f,
                                [](int v, int& w, int& h) { return getOpnAlgTexture(v, w, h); });
        } else if (isOplAlgFamily) {
            renderImageSpinner("alg", "ALG", patch->hw.ALG, voiceRanges.ALG, 150.0f,
                                [](int v, int& w, int& h) { return getOplAlgTexture(v, w, h); });
        } else if (isOpl3FourOpAlgFamily) {
            renderImageSpinner("alg", "ALG", patch->hw.ALG, voiceRanges.ALG, 150.0f,
                                [](int v, int& w, int& h) { return getOpl3AlgTexture(v, w, h); });
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
    if (bank.voicePatchType == fpe::VoicePatchType::OPM || bank.voicePatchType == fpe::VoicePatchType::OPZ ||
        bank.voicePatchType == fpe::VoicePatchType::OPZ2 || bank.voicePatchType == fpe::VoicePatchType::OPN2) {
        ImGui::SetNextItemWidth(150);
        sliderU8Ranged("AMS", patch->hw.AMS, voiceRanges.AMS);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        sliderU8Ranged("PMS", patch->hw.PMS, voiceRanges.PMS);
        ImGui::SameLine();
    }
    if (bank.voicePatchType == fpe::VoicePatchType::OPM || bank.voicePatchType == fpe::VoicePatchType::OPZ ||
        bank.voicePatchType == fpe::VoicePatchType::OPZ2) {
        ImGui::SetNextItemWidth(150);
        sliderU8Ranged("NFQ", patch->hw.NFQ, voiceRanges.NFQ);
        ImGui::SameLine();
    }
     if (bank.voicePatchType == fpe::VoicePatchType::OPL3) {
        ImGui::SetNextItemWidth(150);
        sliderU8Ranged("FB2", patch->hw.FB2, voiceRanges.FB2);
        ImGui::SameLine();
    }
    // OPL_RHY-only: "Inst." combo picks which of COPLRhythm's 5 fixed
    // channels (ext.rhythm_ch) this patch targets, ahead of (and
    // controlling the op-count behind) the ALG/FB band below - see
    // renderRhythmInstrumentCombo()/D-033.
    if (bank.voicePatchType == fpe::VoicePatchType::OPL_RHY) {
        ImGui::SetNextItemWidth(150);
        renderRhythmInstrumentCombo(*patch);
        ImGui::SameLine();
    }
    ImGui::EndGroup();

    ImGui::Separator();
    // Recomputed per operator index (rather than hoisted once outside the
    // loop) because OPL3 4OP mode's PDT field's `used` flag depends on the
    // index (front/back pair lead operators only, 0/2 - see
    // getOpFieldRanges()); every other chip family ignores the index and
    // returns the same ranges regardless.
    for (size_t i = 0; i < patch->ops.size(); ++i) {
        const HwOpFieldRanges opRanges = getOpFieldRanges(bank.voicePatchType, static_cast<int>(i));
        renderHwOpEditor(static_cast<int>(i), patch->ops[i], opRanges, bank.voicePatchType);
        if (i + 1 < patch->ops.size()) ImGui::SameLine();
    }

    ImGui::Separator();
    const PreviewOutput::ActiveBackend backend = ctx.previewOutput.ensureReady();
    const bool connected = backend != PreviewOutput::ActiveBackend::None;
    const uint8_t previewChannel = ctx.previewOutput.activeChannel(ctx.preferences.midiChannel);
    // For the pipe backend, the channel shown here is the one FITOM_X
    // assigned this connection (docs/plugin-midi-pipe.md 4.1), not a
    // user-chosen value - showing it helps confirm multiple simultaneously
    // running patch editor instances really did get distinct channels.
    const std::string statusText =
        backend == PreviewOutput::ActiveBackend::FitomXPipe
            ? "FITOM_Xに接続済み(割当CH " + std::to_string(previewChannel) + ")"
        : backend == PreviewOutput::ActiveBackend::RtMidi ? "MIDI出力(フォールバック)で試聴中"
                                                           : "未接続(オフライン、プリファレンスでMIDI出力を設定できます)";
    ImGui::TextColored(connected ? ImVec4(0.4f, 1.0f, 0.6f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "試聴: %s",
                        statusText.c_str());

    // Realtime diff-only SysEx streaming (D-027): every frame, compare the
    // live in-memory patch against whatever FITOM_X was last actually told
    // (editor.lastSent) and send only what changed - not the whole patch -
    // per docs/manuals/midi-message-reference.md 8.1's "parameters you
    // want to override only" wire format. This is independent of "登録"
    // (which persists to disk); dragging a slider is heard immediately but
    // isn't saved until 登録 is pressed.
    if (connected) {
        const nlohmann::json diff = buildHwPatchDiffJson(editor.lastSent, *patch);
        if (!diff.empty()) {
            if (!editor.deviceSelected) {
                ctx.previewOutput.selectDevice(previewChannel, static_cast<uint8_t>(bank.voicePatchType),
                                                static_cast<uint8_t>(bank.bankIndex), static_cast<uint8_t>(patch->prog));
                editor.deviceSelected = true;
            }
            ctx.previewOutput.sendHwPatchOverride(previewChannel, diff.dump());
            editor.lastSent = *patch;
        }
    }

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
        if (!editor.open) {
            // Just closed this frame (title-bar X) - see D-027.
            sendFullRegisteredOverride(ctx, editor);
        }
    }
    ctx.openEditors.erase(
        std::remove_if(ctx.openEditors.begin(), ctx.openEditors.end(), [](const PatchEditorWindow& e) {
            return !e.open;
        }),
        ctx.openEditors.end());
}

// --- Layered patch editor ---------------------------------------------------
// Opened via openLayeredPatchEditor() from renderBankDetail()'s Layered case,
// mirroring how a Device row opens renderPatchEditor() (D-015/D-034
// precedent). Edits name/poly and each ToneLayer (see
// renderToneLayerEditor()). No realtime preview/SysEx streaming here (unlike
// the HwPatch editor, D-027) - a layered Patch has no synthesis parameters of
// its own to preview, only references to HwPatches that already have their
// own preview-capable editor (reachable via each layer's "編集" button).
void renderLayeredPatchEditor(AppContext& ctx, LayeredPatchEditorWindow& editor) {
    fpe::PatchWorkspace& ws = ctx.workspace;
    auto& banks = ws.layeredPatchBanks();
    if (editor.bankIndex >= banks.size()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "このバンクは既に存在しません。");
        return;
    }
    auto& bank = banks[editor.bankIndex];
    fpe::Patch* patch = bank.findByProg(editor.prog);
    if (!patch) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "このパッチは既に存在しません。");
        return;
    }

    ImGui::Text("[layered bank %d prog %d]", bank.bankIndex, patch->prog);
    ImGui::SameLine();
    {
        // Top-right placement, matching renderPatchEditor()'s "登録" button
        // (D-027) - persists the whole workspace (PatchBank has no narrower
        // single-bank save API either).
        const float buttonW = 90.0f;
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > buttonW) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - buttonW);
        if (ImGui::Button("登録", ImVec2(buttonW, 0))) {
            try {
                ctx.workspace.save();
                // D-049: persist into FITOM_X's own in-memory copy too, see
                // renderPatchEditor()'s comment. fpe::to_json(Patch)
                // already matches the wire schema's recognized keys
                // (name/poly/layers) - the extra prog/sw_bank/sw_prog keys
                // it also includes are simply ignored (unrecognized by
                // FITOM_X's mergePatchFromJson(), same as HwPatch/SwPatch's
                // own harmlessly-ignored extras elsewhere).
                ctx.previewOutput.sendLayeredPatchBankOverride(
                    static_cast<uint8_t>(bank.bankIndex), static_cast<uint8_t>(patch->prog),
                    nlohmann::json(*patch).dump());
                if (!ctx.kioskMode) editor.open = false; // D-048, see renderPatchEditor()'s comment
            } catch (const std::exception& e) {
                ctx.errorMessage = std::string("保存に失敗しました:\n") + e.what();
            }
        }
    }

    char nameBuf[256];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", patch->name.c_str());
    if (ImGui::InputText("名前", nameBuf, sizeof(nameBuf))) patch->name = nameBuf;

    int poly = patch->poly;
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("poly (0=auto)", &poly)) patch->poly = std::max(0, poly);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = デバイスチャンネル数から自動決定");

    // sw_bank/sw_prog: patch-level performance-patch override (falls back to
    // each layer's own HwPatch::sw_bank/sw_prog when unset, see
    // LayeredPatch.h). Same resolved-label + picker treatment as HwPatch's
    // own sw_bank/sw_prog in renderPatchEditor() (D-034), via the shared
    // SwPatchPickerState/renderSwPatchPicker() repointed at this Patch
    // directly (openLayeredSwPatchPicker(), D-036) instead of at a HwPatch.
    {
        std::string swLabel;
        const fpe::SwPatch* swPatch = nullptr;
        if (patch->sw_bank >= 0 && patch->sw_prog >= 0) {
            const fpe::SwBank* swBank = ws.findPerformanceBank(patch->sw_bank);
            swPatch = ws.resolvePerformancePatch(patch->sw_bank, patch->sw_prog);
            swLabel = "パフォーマンス: " +
                      std::to_string(patch->sw_bank) + "/" + std::to_string(patch->sw_prog) + " : " +
                      (swBank ? swBank->name : std::string("(N/A)")) + " / " +
                      (swPatch ? swPatch->name : std::string("(N/A)"));
        } else {
            swLabel = "パフォーマンス: (N/A)";
        }
        if (ImGui::Selectable(swLabel.c_str(), false, 0, ImVec2(640, 0))) {
            openLayeredSwPatchPicker(ctx, editor.bankIndex, patch->prog);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("クリックしてパフォーマンスパッチを選択");
        ImGui::SameLine();
        // Trailing "編集" button (opens the referenced SwPatch's own
        // editor), mirroring ToneLayer's hw_bank/hw_prog row
        // (renderToneLayerEditor(), D-036) and renderPatchEditor()'s own
        // sw_bank/sw_prog row above.
        ImGui::BeginDisabled(!swPatch);
        if (ImGui::Button("編集##swedit")) {
            auto swIdx = findPerformanceBankVectorIndex(ws, patch->sw_bank);
            if (swIdx) openPerformancePatchEditor(ctx, *swIdx, patch->sw_prog);
        }
        ImGui::EndDisabled();
    }

    ImGui::Separator();
    ImGui::Text("ToneLayer (%zu)", patch->layers.size());
    ImGui::Separator();
    for (size_t i = 0; i < patch->layers.size(); ++i) {
        renderToneLayerEditor(ctx, editor.bankIndex, patch->prog, static_cast<int>(i), patch->layers[i]);
    }
}

constexpr ImVec2 kLayeredPatchEditorInitialSize(700.0f, 600.0f);

void renderLayeredPatchEditors(AppContext& ctx) {
    for (auto& editor : ctx.openLayeredEditors) {
        if (!editor.open) continue;
        const std::string title = "レイヤードパッチ編集##layerededitor" + std::to_string(editor.id);
        ImGui::SetNextWindowSize(kLayeredPatchEditorInitialSize, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title.c_str(), &editor.open)) {
            renderLayeredPatchEditor(ctx, editor);
        }
        ImGui::End();
    }
    ctx.openLayeredEditors.erase(
        std::remove_if(ctx.openLayeredEditors.begin(), ctx.openLayeredEditors.end(),
                        [](const LayeredPatchEditorWindow& e) { return !e.open; }),
        ctx.openLayeredEditors.end());
}

// --- Performance patch editor ----------------------------------------------
// Opened via openPerformancePatchEditor() from renderBankDetail()'s
// Performance case, mirroring how Device/Layered rows open their own modeless
// editors (D-015/D-036 precedent). Edits a fpe::SwPatch's name, channel-level
// vibrato (FmSwVoice) and per-operator velocity-sensitivity/tremolo
// (FmSwOp x4, see include/fpe/SwPatch.h). Per the project owner's explicit
// request: waveform fields (LWF/SLW) are shown as an image (same
// image+spinner treatment as HwPatch's WS, D-021) rather than a bare number,
// mode fields (LFM/SLM) as a symbol dropdown rather than a bare number, and
// everything else as a plain slider for now (exact register widths for
// these fields aren't confirmed against any FITOM_X doc yet, unlike
// HwPatch's FieldRange tables - a human will narrow these later, per the
// project owner).

// Symbolic dropdown for FmSwVoice::LFM / FmSwOp::SLM - both fields share the
// same 3-value enum (FmSwOp::SLM's comment: "mode (same semantics as
// LFM)"), so one shared combo handles both rather than duplicating it.
void renderLfoModeCombo(const char* label, uint8_t& mode) {
    static constexpr std::pair<uint8_t, const char*> kModes[3] = {
        {0, "ループ"},
        {1, "ワンショット(ホールド)"},
        {2, "ワンショット(リセット)"},
    };
    const char* preview = "?";
    for (const auto& [v, name] : kModes) {
        if (v == mode) {
            preview = name;
            break;
        }
    }
    if (ImGui::BeginCombo(label, preview)) {
        for (const auto& [v, name] : kModes) {
            const bool selected = (v == mode);
            if (ImGui::Selectable(name, selected)) mode = v;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

// Channel-level vibrato band (FmSwVoice) - laid out the same way
// renderPatchEditor()'s ALG band is (image+spinner control at the left,
// flanked by the rest of the group's sliders), since LWF is this struct's
// own "value shown as an image" field, same role ALG plays for HwPatch.
void renderSwVoiceEditor(fpe::FmSwVoice& sw) {
    ImGui::Text("チャンネルピッチLFO(ビブラート)");
    ImGui::BeginGroup();
    renderImageSpinner("lwf", "Waveform", sw.LWF, FieldRange{0, 6, true}, 150.0f,
                        [](int v, int& w, int& h) { return getLfoWaveTexture(v, w, h); });
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(150);
    sliderU8("リセット", sw.LFS, 0, 1);
    ImGui::SetNextItemWidth(150);
    renderLfoModeCombo("モード", sw.LFM);
    ImGui::SetNextItemWidth(150);
    sliderU8("ディレイ", sw.LFD, 0, 99);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    sliderU8("周波数", sw.LFR, 0, 99);
    ImGui::SetNextItemWidth(150);
    sliderU8("フェードイン", sw.LFI, 0, 99);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    sliderI16("デプス(cent)", sw.depth_cents, -1200, 1200);
    ImGui::EndGroup();
}

// Per-operator velocity-sensitivity + tremolo band (FmSwOp), one box per
// operator laid out side-by-side like renderHwOpEditor()'s per-op boxes.
// VLD/VLR are explicitly commented "reserved, currently unused" in
// include/fpe/SwPatch.h, so they're shown disabled (matching the
// FieldRange.used=false convention used for chip-unused HwPatch fields)
// rather than omitted, keeping every operator box the same shape.
void renderSwOpEditor(int index, fpe::FmSwOp& op) {
    ImGui::PushID(index);
    ImGui::BeginChild("swop", ImVec2(230, 420), true);
    ImGui::Text("OP %d", index + 1);
    ImGui::Separator();
    ImGui::TextUnformatted("ベロシティ感度");
    sliderU8("TL", op.VTL, 0, 99);
    sliderU8("AR", op.VAR, 0, 99);
    sliderU8("DR", op.VDR, 0, 99);
    sliderU8("SL", op.VSL, 0, 99);
    sliderU8("SR", op.VSR, 0, 99);
    sliderU8("RR", op.VRR, 0, 99);
    ImGui::BeginDisabled();
    sliderU8("VLD(未使用)", op.VLD, 0, 99);
    sliderU8("VLR(未使用)", op.VLR, 0, 99);
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextUnformatted("オペレータTL LFO");
    renderImageSpinner("slw", "波形", op.SLW, FieldRange{0, 6, true}, 100.0f,
                        [](int v, int& w, int& h) { return getLfoWaveTexture(v, w, h); });
    sliderU8("リセット", op.SLS, 0, 1);
    renderLfoModeCombo("モード", op.SLM);
    sliderU8("デプス", op.SLD, 0, 127);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0-63=正、64-127=負(-64..-1)");
    sliderU8("ディレイ", op.SLY, 0, 99);
    sliderU8("周波数", op.SLR, 0, 99);
    sliderU8("フェードイン", op.SLI, 0, 99);
    ImGui::EndChild();
    ImGui::PopID();
}

void renderPerformancePatchEditor(AppContext& ctx, PerformancePatchEditorWindow& editor) {
    fpe::PatchWorkspace& ws = ctx.workspace;
    auto& banks = ws.performanceBanks();
    if (editor.bankIndex >= banks.size()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "このバンクは既に存在しません。");
        return;
    }
    auto& bank = banks[editor.bankIndex];
    fpe::SwPatch* patch = bank.findByProg(editor.prog);
    if (!patch) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "このパッチは既に存在しません。");
        return;
    }

    ImGui::Text("[performance bank %d prog %d]", bank.bankIndex, patch->prog);
    ImGui::SameLine();
    {
        // Top-right "登録" placement, matching renderPatchEditor()/
        // renderLayeredPatchEditor()'s convention - SwBank has no narrower
        // single-bank save API either, so this persists the whole workspace.
        const float buttonW = 90.0f;
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > buttonW) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - buttonW);
        if (ImGui::Button("登録", ImVec2(buttonW, 0))) {
            try {
                ctx.workspace.save();
                // D-049: persist into FITOM_X's own in-memory copy too, see
                // renderPatchEditor()'s comment. fpe::to_json(SwPatch)
                // already matches the wire schema's own example verbatim
                // (confirmed D-046) - extra prog/name keys are ignored.
                ctx.previewOutput.sendSwPatchBankOverride(
                    static_cast<uint8_t>(bank.bankIndex), static_cast<uint8_t>(patch->prog),
                    nlohmann::json(*patch).dump());
                if (!ctx.kioskMode) editor.open = false; // D-048, see renderPatchEditor()'s comment
            } catch (const std::exception& e) {
                ctx.errorMessage = std::string("保存に失敗しました:\n") + e.what();
            }
        }
    }

    char nameBuf[256];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", patch->name.c_str());
    if (ImGui::InputText("名前", nameBuf, sizeof(nameBuf))) patch->name = nameBuf;

    ImGui::SetNextItemWidth(200);
    sliderI16("微調整(cent)", patch->fine_transpose, -1200, 1200);

    ImGui::Separator();
    renderSwVoiceEditor(patch->sw);

    ImGui::Separator();
    ImGui::Text("オペレータ");
    for (size_t i = 0; i < patch->ops.size(); ++i) {
        renderSwOpEditor(static_cast<int>(i), patch->ops[i]);
        if (i + 1 < patch->ops.size()) ImGui::SameLine();
    }
}

constexpr ImVec2 kPerformancePatchEditorInitialSize(1100.0f, 700.0f);

void renderPerformancePatchEditors(AppContext& ctx) {
    for (auto& editor : ctx.openPerformanceEditors) {
        if (!editor.open) continue;
        const std::string title = "パフォーマンスパッチ編集##perfeditor" + std::to_string(editor.id);
        ImGui::SetNextWindowSize(kPerformancePatchEditorInitialSize, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title.c_str(), &editor.open)) {
            renderPerformancePatchEditor(ctx, editor);
        }
        ImGui::End();
    }
    ctx.openPerformanceEditors.erase(
        std::remove_if(ctx.openPerformanceEditors.begin(), ctx.openPerformanceEditors.end(),
                        [](const PerformancePatchEditorWindow& e) { return !e.open; }),
        ctx.openPerformanceEditors.end());
}

// --- Drum note editor -------------------------------------------------------
// Opened via openDrumNoteEditor() from renderBankDetail()'s Drum/routed
// case, mirroring how Device/Layered/Performance rows open their own
// modeless editors (D-015/D-036/D-037 precedent). Edits one fpe::DrumNote:
// name, its source patch (voice_patch_type/patch_bank/patch_prog - a
// picker rather than raw numbers, same rationale as every other patch
// reference in this editor, D-034/D-036), play_note (by name or by an
// on-screen keyboard - explicitly not a raw number, per the project
// owner's request), fine_tune/pan/gate_time (plain sliders/input - exact
// register widths unconfirmed, same "human narrows later" treatment as
// SwPatch's own unconfirmed fields, D-037), and its own sw_bank/sw_prog
// override. Unlike LayeredPatchEditorWindow/PerformancePatchEditorWindow,
// this DOES support realtime preview (selectDevice + note on/off on a
// press-and-hold button) per the project owner's explicit "登録前に
// プレビュー発音可能とする" requirement - a DrumNote, unlike a layered Patch
// or SwPatch, fully determines what would sound (source patch + play note)
// on its own, so there's a real "would this sound right" question to answer
// before saving.
void renderDrumNoteEditor(AppContext& ctx, DrumNoteEditorWindow& editor) {
    fpe::PatchWorkspace& ws = ctx.workspace;
    auto& kits = ws.drumKits();
    if (editor.kitIndex >= kits.size()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "このドラムキットは既に存在しません。");
        return;
    }
    fpe::DrumKit& kit = kits[editor.kitIndex];
    fpe::DrumNote* note = kit.findNote(editor.note);
    if (!note) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "このドラムノートは既に存在しません。");
        return;
    }

    ImGui::Text("[drum prog %d] note %d (%s)", kit.prog, note->note, midiNoteName(note->note).c_str());
    ImGui::SameLine();
    {
        // Top-right "登録", matching every other patch editor's own (D-027) -
        // persists the whole workspace (DrumKit has no narrower single-note
        // save API either), and closes itself on a successful save (D-047,
        // per the project owner's request - extended to the other three
        // modeless patch editors too by D-048, see renderPatchEditor()'s
        // comment for the kiosk-mode caveat, which doesn't apply here since
        // DrumNoteEditorWindow has no kiosk-slot equivalent to begin with).
        const float buttonW = 90.0f;
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > buttonW) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - buttonW);
        if (ImGui::Button("登録", ImVec2(buttonW, 0))) {
            try {
                ws.save();
                // D-049: persist the whole kit into FITOM_X's own in-memory
                // DrumPatch too - drum banks are always bank 0
                // (profile.schema.json), so only kit.prog varies.
                ctx.previewOutput.sendDrumKitBankOverride(0, static_cast<uint8_t>(kit.prog),
                                                           buildDrumKitOverrideJson(kit).dump());
                editor.open = false;
            } catch (const std::exception& e) {
                ctx.errorMessage = std::string("保存に失敗しました:\n") + e.what();
            }
        }
    }

    char nameBuf[256];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", note->name.c_str());
    if (ImGui::InputText("名前", nameBuf, sizeof(nameBuf))) note->name = nameBuf;

    ImGui::Separator();
    // ソースパッチ: resolved label + picker, per the project owner's request
    // that this be a patch picker rather than a raw voice_patch_type/
    // patch_bank/patch_prog triple of numbers.
    {
        const std::string label =
            "ソースパッチ: " + describeDrumSourcePatch(ws, note->voice_patch_type, note->patch_bank, note->patch_prog);
        if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(640, 0))) {
            openDrumSourcePatchPicker(ctx, editor.kitIndex, note->note);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("クリックして発音元パッチ(レイヤード/デバイス/PCM波形/サンプルゾーン)を選択");
        ImGui::SameLine();
        ImGui::BeginDisabled(!drumSourcePatchHasEditor(note->voice_patch_type, note->patch_bank));
        if (ImGui::Button("編集##srcedit")) {
            openDrumSourcePatchEditor(ctx, note->voice_patch_type, note->patch_bank, note->patch_prog);
        }
        ImGui::EndDisabled();
    }

    ImGui::Separator();
    // プレイノート: ノート名ドロップダウン、またはスクリーンキーボード型
    // ピッカーのいずれかで選択(数値入力は用意しない、依頼通り)。
    {
        ImGui::TextUnformatted("プレイノート");
        ImGui::SameLine();
        const std::string currentLabel = midiNoteName(note->play_note) + " (" + std::to_string(note->play_note) + ")";
        ImGui::SetNextItemWidth(140);
        if (ImGui::BeginCombo("##playnotecombo", currentLabel.c_str())) {
            for (int n = 0; n <= 127; ++n) {
                const bool selected = (n == note->play_note);
                const std::string itemLabel = midiNoteName(n) + " (" + std::to_string(n) + ")";
                if (ImGui::Selectable(itemLabel.c_str(), selected)) note->play_note = static_cast<uint8_t>(n);
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("キーボードで選択")) {
            openDrumNoteKeyboardPicker(ctx, editor.kitIndex, note->note, note->play_note);
        }
    }

    ImGui::Separator();
    {
        int fineTune = note->fine_tune;
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderInt("微調整 fine_tune (kfs)", &fineTune, -128, 127)) note->fine_tune = fineTune;
        ImGui::SameLine();
        int pan = note->pan;
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderInt("パンオフセット", &pan, -64, 63)) note->pan = pan;
        int gateTime = note->gate_time;
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputInt("ゲートタイム (0=NoteOffで停止)", &gateTime)) note->gate_time = std::max(0, gateTime);
    }

    ImGui::Separator();
    // sw_bank/sw_prog: per-note performance-patch override, same resolved-
    // label + picker treatment as HwPatch/Patch's own sw_bank/sw_prog
    // (D-034/D-036), reusing the shared SwPatchPickerState via
    // openDrumNoteSwPatchPicker().
    {
        std::string swLabel;
        const fpe::SwPatch* swPatch = nullptr;
        if (note->sw_bank >= 0 && note->sw_prog >= 0) {
            const fpe::SwBank* swBank = ws.findPerformanceBank(note->sw_bank);
            swPatch = ws.resolvePerformancePatch(note->sw_bank, note->sw_prog);
            swLabel = "パフォーマンス: " + std::to_string(note->sw_bank) + "/" + std::to_string(note->sw_prog) +
                      " : " + (swBank ? swBank->name : std::string("(N/A)")) + " / " +
                      (swPatch ? swPatch->name : std::string("(N/A)"));
        } else {
            swLabel = "パフォーマンス: (N/A)";
        }
        if (ImGui::Selectable(swLabel.c_str(), false, 0, ImVec2(640, 0))) {
            openDrumNoteSwPatchPicker(ctx, editor.kitIndex, note->note);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("クリックしてパフォーマンスパッチを選択");
        ImGui::SameLine();
        ImGui::BeginDisabled(!swPatch);
        if (ImGui::Button("編集##swedit")) {
            auto swIdx = findPerformanceBankVectorIndex(ws, note->sw_bank);
            if (swIdx) openPerformancePatchEditor(ctx, *swIdx, note->sw_prog);
        }
        ImGui::EndDisabled();
    }

    ImGui::Separator();
    // 試聴 (D-038): 登録前でも現在のソースパッチ+プレイノートでその場で
    // 発音できるようにする、との依頼。鍵盤全体は不要なので、
    // renderPreviewKeyboard()と同じIsItemActivated()/IsItemDeactivated()の
    // 押し続け方式のボタン1つで済ませる。fine_tune/pan/gate_timeは
    // (DeviceパッチのAR/DR等と違って)差分SysEx越しに送れる合成パラメータ
    // ではないため、この試聴には反映しない。sw_bank/sw_progは反映する
    // (D-046 - 実際のリズムトラック再生と音が異なるという報告を受けて
    // FITOM_X本体を調査した結果、CC#0/32/PCによる直接デバイス選択だけでは
    // 反映されないドラムノート固有のオーバーライドだと判明したため、
    // sendDrumNoteSwPatchOverride()で明示的に送るようにした)。
    {
        const PreviewOutput::ActiveBackend backend = ctx.previewOutput.ensureReady();
        const bool connected = backend != PreviewOutput::ActiveBackend::None;
        const uint8_t previewChannel = ctx.previewOutput.activeChannel(ctx.preferences.midiChannel);
        const std::string statusText =
            backend == PreviewOutput::ActiveBackend::FitomXPipe
                ? "FITOM_Xに接続済み(割当CH " + std::to_string(previewChannel) + ")"
            : backend == PreviewOutput::ActiveBackend::RtMidi ? "MIDI出力(フォールバック)で試聴中"
                                                               : "未接続(オフライン、プリファレンスでMIDI出力を設定できます)";
        ImGui::TextColored(connected ? ImVec4(0.4f, 1.0f, 0.6f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "試聴: %s",
                            statusText.c_str());

        ImGui::BeginDisabled(!connected);
        ImGui::Button("試聴 (押している間発音)");
        if (ImGui::IsItemActivated()) {
            ctx.previewOutput.selectDevice(previewChannel, static_cast<uint8_t>(note->voice_patch_type),
                                            static_cast<uint8_t>(note->patch_bank),
                                            static_cast<uint8_t>(note->patch_prog));
            sendDrumNoteSwPatchOverride(ctx, previewChannel, *note); // D-046 - must precede noteOn(), see comment above
            ctx.previewOutput.noteOn(previewChannel, note->play_note, 100);
            editor.heldPreviewNote = note->play_note;
        }
        if (ImGui::IsItemDeactivated()) {
            if (editor.heldPreviewNote >= 0) {
                ctx.previewOutput.noteOff(previewChannel, static_cast<uint8_t>(editor.heldPreviewNote), 0);
            }
            editor.heldPreviewNote = -1;
        }
        ImGui::EndDisabled();
    }
}

constexpr ImVec2 kDrumNoteEditorInitialSize(720.0f, 620.0f);

void renderDrumNoteEditors(AppContext& ctx) {
    for (auto& editor : ctx.openDrumNoteEditors) {
        if (!editor.open) continue;
        const std::string title = "ドラムノート編集##drumnoteeditor" + std::to_string(editor.id);
        ImGui::SetNextWindowSize(kDrumNoteEditorInitialSize, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title.c_str(), &editor.open)) {
            renderDrumNoteEditor(ctx, editor);
        }
        ImGui::End();
    }
    ctx.openDrumNoteEditors.erase(
        std::remove_if(ctx.openDrumNoteEditors.begin(), ctx.openDrumNoteEditors.end(),
                        [](const DrumNoteEditorWindow& e) { return !e.open; }),
        ctx.openDrumNoteEditors.end());
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

    if (ImGui::TreeNode("layered", "レイヤードパッチバンク (%zu)", ws.layeredPatchBanks().size())) {
        auto& banks = ws.layeredPatchBanks();
        for (size_t i = 0; i < banks.size(); ++i) {
            const auto& bank = banks[i];
            std::string label = "[bank " + std::to_string(bank.bankIndex) + "] " + bank.name +
                                 " (" + std::to_string(bank.patches.size()) + " patches)";
            if (ImGui::Selectable(label.c_str())) selectBank(ctx, BankCategory::Layered, i);
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

    // デバイス/サンプルゾーン/PCM波形バンクは、実データでは1カテゴリ
    // (voicePatchType、チップファミリー)あたり数十バンクに及ぶことがある
    // ため(../FITOM_staging/banks/参照)、単なるフラット一覧ではなく
    // チップファミリー単位でもう1段グルーピングする - FITOM_X本体自身の
    // パッチピッカー(apps/fitom_gui/PatchPickerDialog、FITOM_Xリポジトリ)の
    // Category階層と同じ軸(fpe::VoicePatchType)。3セクションとも同じ形の
    // 処理なので、ここだけで使うローカルラムダにまとめてある。
    auto renderCategorizedBankList = [&ctx](auto& banks, BankCategory category, auto&& countOf) {
        std::vector<fpe::VoicePatchType> types;
        for (auto& bank : banks) {
            if (std::find(types.begin(), types.end(), bank.voicePatchType) == types.end())
                types.push_back(bank.voicePatchType);
        }
        std::sort(types.begin(), types.end());
        for (fpe::VoicePatchType t : types) {
            ImGui::PushID(static_cast<int>(t));
            if (ImGui::TreeNode("group", "%s", fpe::voicePatchTypeToString(t).c_str())) {
                for (size_t i = 0; i < banks.size(); ++i) {
                    if (banks[i].voicePatchType != t) continue;
                    std::string label = "[bank " + std::to_string(banks[i].bankIndex) + "] " + banks[i].name +
                                         " (" + std::to_string(countOf(banks[i])) + " patches)";
                    if (ImGui::Selectable(label.c_str())) selectBank(ctx, category, i);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    };

    if (ImGui::TreeNode("device", "デバイスパッチバンク (%zu)", ws.deviceBanks().size())) {
        renderCategorizedBankList(ws.deviceBanks(), BankCategory::Device,
                                   [](const fpe::HwBank& b) { return b.patches.size(); });
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("samplezone", "サンプルゾーンバンク (%zu)", ws.sampleZoneBanks().size())) {
        renderCategorizedBankList(ws.sampleZoneBanks(), BankCategory::SampleZone,
                                   [](const fpe::SampleZoneBank& b) { return b.patches.size(); });
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("pcm", "PCM波形バンク (%zu)", ws.pcmBanks().size())) {
        renderCategorizedBankList(ws.pcmBanks(), BankCategory::Pcm,
                                   [](const fpe::PcmBank& b) { return b.entries.size(); });
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

// The routed-kit note list, or the direct-kit inline editor, for
// ws.drumKits()[kitIndex]. Factored out of renderBankDetail()'s
// BankCategory::Drum case (D-040) so kiosk mode's "drum" kind can render the
// exact same content as its own full-viewport top-level screen - the same
// way renderPatchEditor()/renderLayeredPatchEditor()/
// renderPerformancePatchEditor() are already shared between BankDetail and
// kiosk mode. Unlike those three, this was never behind its own modeless
// editor-window struct (see KioskDrumKitWindow's comment), so there was
// nothing to reuse until now.
void renderDrumKitDetail(AppContext& ctx, size_t kitIndex) {
    fpe::PatchWorkspace& ws = ctx.workspace;
    auto& kits = ws.drumKits();
    if (kitIndex >= kits.size()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "このドラムキットは既に存在しません。");
        return;
    }
    auto& kit = kits[kitIndex];
    const char* typeStr = (kit.type == fpe::DrumKitType::Routed) ? "routed" : "direct";
    ImGui::Text("ドラムキット [prog %d] %s (%s)", kit.prog, kit.name.c_str(), typeStr);
    ImGui::Separator();

    if (kit.type == fpe::DrumKitType::Routed) {
        // ドラムノート選択画面 (D-038): 0-127の全MIDIノートを表示し、
        // 未割当のノートも一覧できるようにする(依頼通り)。割当済みの
        // 行はシングルクリックでその場でプレビュー発音
        // (startDrumNoteListPreview()、D-044)、ダブルクリックでドラム
        // ノート編集画面(モードレス、openDrumNoteEditor())を開き、末尾に
        // 複製・削除ボタンを用意する。未割当の行は「作成」ボタンで
        // デフォルト値のDrumNoteを追加した上で編集画面を開く。複製・削除は
        // バンク作成(D-014)と同様、即座にws.save()する構造的な
        // 変更として扱う(登録ボタンでの明示保存が必要な、
        // 各ノートのフィールド編集そのものとは区別する)。
        ImGui::TextUnformatted("ドラムノート一覧 (0-127、未割当も表示)");
        ImGui::Separator();
        for (int n = 0; n < 128; ++n) {
            ImGui::PushID(n);
            fpe::DrumNote* note = kit.findNote(static_cast<uint8_t>(n));
            if (note) {
                const std::string label = "note " + std::to_string(n) + " (" + midiNoteName(n) + "): " +
                                           note->name + "  -> play " + midiNoteName(note->play_note) + " (" +
                                           std::to_string(note->play_note) + ")";
                // シングルクリックでその場でプレビュー発音、ダブルクリックで
                // 編集画面を開く(D-044)。ImGuiSelectableFlags_AllowDoubleClick
                // を付けるとSelectable()はどちらのクリックでもtrueを返すため、
                // IsMouseDoubleClicked()で判別する(Dear ImGuiの標準的な
                // ダブルクリック判定パターン)。
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(560, 0))) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        openDrumNoteEditor(ctx, kitIndex, static_cast<uint8_t>(n));
                    } else {
                        startDrumNoteListPreview(ctx, *note);
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("クリックで試聴、ダブルクリックで編集");
                ImGui::SameLine();
                if (ImGui::SmallButton("複製")) {
                    const int toNote = nextFreeDrumNote(kit, static_cast<uint8_t>(n));
                    if (toNote >= 0) {
                        fpe::DrumNote copy = *note;
                        copy.note = static_cast<uint8_t>(toNote);
                        try {
                            ws.upsertDrumNote(kit, copy);
                            ws.save();
                        } catch (const std::exception& e) {
                            ctx.errorMessage = std::string("複製に失敗しました:\n") + e.what();
                        }
                    } else {
                        ctx.errorMessage = "複製に失敗しました:\n空いているノート番号がありません。";
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("削除")) {
                    try {
                        ws.deleteDrumNote(kit, static_cast<uint8_t>(n));
                        ws.save();
                    } catch (const std::exception& e) {
                        ctx.errorMessage = std::string("削除に失敗しました:\n") + e.what();
                    }
                }
            } else {
                ImGui::TextDisabled("note %d (%s): (未割当)", n, midiNoteName(n).c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("作成")) {
                    fpe::DrumNote fresh;
                    fresh.note = static_cast<uint8_t>(n);
                    fresh.play_note = static_cast<uint8_t>(n);
                    try {
                        ws.upsertDrumNote(kit, fresh);
                        ws.save();
                        openDrumNoteEditor(ctx, kitIndex, static_cast<uint8_t>(n));
                    } catch (const std::exception& e) {
                        ctx.errorMessage = std::string("作成に失敗しました:\n") + e.what();
                    }
                }
            }
            ImGui::PopID();
        }
    } else {
        // "direct"キットは個別ノートのリストを持たず(DrumKit.h
        // effectiveNotes()参照)、note_min-note_max全体に単一の
        // ソースパッチをpassthroughで割り当てる形なので、
        // ドラムノート選択画面/編集画面の階層は適用されない
        // (未割当・複製・削除の概念自体が存在しない)。この場に
        // インラインでソースパッチピッカー+音域+登録ボタンのみを
        // 用意する(D-038、スコープ限定 - sw_bank/sw_prog・
        // fine_tune/pan/gate_timeは今回未対応、docs/STATUS.md参照)。
        {
            const float buttonW = 90.0f;
            const float avail = ImGui::GetContentRegionAvail().x;
            if (avail > buttonW) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - buttonW);
            if (ImGui::Button("登録", ImVec2(buttonW, 0))) {
                try {
                    ws.save();
                    // D-049: persist into FITOM_X's own in-memory DrumPatch
                    // too, same as the "routed" note editor's own 登録
                    // (buildDrumKitOverrideJson() already handles "direct"
                    // kits uniformly via kit.effectiveNotes()).
                    ctx.previewOutput.sendDrumKitBankOverride(0, static_cast<uint8_t>(kit.prog),
                                                               buildDrumKitOverrideJson(kit).dump());
                } catch (const std::exception& e) {
                    ctx.errorMessage = std::string("保存に失敗しました:\n") + e.what();
                }
            }
        }

        const std::string label =
            "ソースパッチ: " + describeDrumSourcePatch(ws, kit.voice_patch_type, kit.patch_bank, kit.patch_prog);
        if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(640, 0))) {
            openDrumSourcePatchPickerDirect(ctx, kitIndex);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("クリックして発音元パッチ(レイヤード/デバイス/PCM波形/サンプルゾーン)を選択");
        ImGui::SameLine();
        ImGui::BeginDisabled(!drumSourcePatchHasEditor(kit.voice_patch_type, kit.patch_bank));
        if (ImGui::Button("編集##srcedit")) {
            openDrumSourcePatchEditor(ctx, kit.voice_patch_type, kit.patch_bank, kit.patch_prog);
        }
        ImGui::EndDisabled();

        int noteRange[2] = {kit.note_min, kit.note_max};
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputInt2("音域(lo-hi)", noteRange)) {
            kit.note_min = static_cast<uint8_t>(std::clamp(noteRange[0], 0, 127));
            kit.note_max = static_cast<uint8_t>(std::clamp(noteRange[1], 0, 127));
        }
    }
}

// The patch/note list for the single bank or drum kit selected in
// renderOutline(). Deliberately shallow (name/prog/basic refs only, plus
// ToneLayer for layered patches since that's the bank's own on-disk shape) -
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
        case BankCategory::Layered: {
            auto& banks = ws.layeredPatchBanks();
            if (ctx.selectedIndex >= banks.size()) break;
            auto& bank = banks[ctx.selectedIndex];
            ImGui::Text("レイヤードパッチバンク [bank %d] %s", bank.bankIndex, bank.name.c_str());
            ImGui::Separator();
            for (auto& patch : bank.patches) {
                std::string label = "[prog " + std::to_string(patch.prog) + "] " + patch.name + " (" +
                                     std::to_string(patch.layers.size()) + " layers)";
                if (ImGui::Selectable(label.c_str())) openLayeredPatchEditor(ctx, ctx.selectedIndex, patch.prog);
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
                std::string label = "[prog " + std::to_string(patch.prog) + "] " + patch.name;
                if (ImGui::Selectable(label.c_str())) openPerformancePatchEditor(ctx, ctx.selectedIndex, patch.prog);
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
            renderDrumKitDetail(ctx, ctx.selectedIndex);
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

// Shows a native OS error dialog in addition to the existing stderr
// message, then the caller should exit. Necessary because FITOM_X spawns
// this editor (both kiosk mode, D-026, and the plain `<profile.json>`
// launch, D-010) as a concurrent, non-waited child process - it never
// observes this process's exit code, so a bare `return 1` is invisible to
// everyone (D-029, per the project owner: FITOM_X runs concurrently and
// can't block waiting for this process's return value). Originally added
// for startup failures that happen before any ImGui window exists to show
// renderErrorPopup() in; also reused mid-session (D-030) for the MIDI
// pipe's connection-capacity rejection, since that too should end the
// process rather than continue in a half-broken state.
// Windows-only for now (consistent with this project's other native-API
// code, e.g. Preferences.cpp's exeDir() - POSIX untested).
void showFatalErrorBox(const std::string& message) {
    std::fprintf(stderr, "%s\n", message.c_str());
#ifdef _WIN32
    // MessageBoxA takes an ANSI (system codepage) string, not UTF-8 - this
    // project's source (and therefore `message`, built from Japanese
    // string literals) is UTF-8 throughout (see CMakeLists.txt's /utf-8
    // comment), so passing it to MessageBoxA directly renders as mojibake
    // on a Shift-JIS-locale Windows install. Convert to UTF-16 and use
    // MessageBoxW instead.
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
    std::wstring wmessage(static_cast<size_t>(std::max(wlen, 0)), L'\0');
    if (wlen > 0) MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, wmessage.data(), wlen);
    MessageBoxW(nullptr, wmessage.c_str(), L"FITOM_X Patch Editor", MB_OK | MB_ICONERROR);
#endif
}

// Kiosk mode's second CLI argument (D-039, extended D-040) names which
// patch-kind editor to open. Kept as a stable lowercase English token
// independent of BankCategory's enumerator spelling, so FITOM_X's launch
// code has a fixed contract to target regardless of internal renames on
// this side. All six BankCategory values have a reserved token here, even
// though two of them ("pcmbank"/"samplezonebank") aren't wired to an actual
// editor screen yet - see kioskKindImplemented() - so FITOM_X's caller code
// can already standardize on the full keyword set instead of inventing its
// own placeholder strings for the two not-yet-supported kinds (D-040).
std::optional<BankCategory> parseKioskKind(const std::string& s) {
    if (s == "device") return BankCategory::Device;
    if (s == "layered") return BankCategory::Layered;
    if (s == "performance") return BankCategory::Performance;
    if (s == "drum") return BankCategory::Drum;
    if (s == "pcmbank") return BankCategory::Pcm;
    if (s == "samplezonebank") return BankCategory::SampleZone;
    return std::nullopt;
}

// Whether parseKioskKind() having recognized the token also means kiosk mode
// can actually open something for it. "pcmbank"/"samplezonebank" parse
// successfully (they're reserved keywords, D-040) but have no editor screen
// to open at all in this codebase yet - fpe::PcmBank/fpe::SampleZone have no
// per-patch edit form anywhere (see PcmBank.h/SampleZone.h's own comments
// and docs/STATUS.md's known-gaps list), unlike Device/Layered/Performance/
// Drum which all do.
bool kioskKindImplemented(BankCategory kind) {
    switch (kind) {
        case BankCategory::Device:
        case BankCategory::Layered:
        case BankCategory::Performance:
        case BankCategory::Drum:
            return true;
        case BankCategory::Pcm:
        case BankCategory::SampleZone:
            return false;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    // Kiosk mode (D-026; argument format extended to carry a kind in D-039,
    // Performance/Drum kinds added D-040):
    // `fitom_patch_editor_gui.exe <profile.json> <kind> <bank-file> <prog>`
    // (argc==5) launches directly into a single, full-viewport editor for
    // that patch and exits the whole process as soon as it's closed - no
    // menu, no outline. `kind` is one of "device" (bank-file =
    // *.hwbank.json, prog = HW prog), "layered" (bank-file =
    // *.patchbank.json, prog = layered Patch prog), "performance" (bank-file
    // = *.swbank.json, prog = SwPatch prog), or "drum" (bank-file =
    // *.drumkit.json, prog = DrumKit::prog - a *.drumkit.json file already
    // is one whole kit, so this is a sanity check against the profile's own
    // drum_banks[].prog registration rather than a lookup key) - see
    // parseKioskKind()/kioskKindImplemented(). "pcmbank"/"samplezonebank"
    // are recognized as reserved kind keywords but not yet implemented
    // (D-040) since PcmBank/SampleZone have no edit form of their own at
    // all yet. Meant to be spawned by a running FITOM_X instance as a
    // focused "edit this one patch" child process. The plain
    // `<profile.json>` form (argc==2, D-010) is unchanged and still opens
    // the normal windowed UI at Outline. Resolved and validated before
    // glfwInit()/any window creation, so a bad kiosk invocation fails fast -
    // via showFatalErrorBox() (D-029), not just stderr + a nonzero exit
    // code, since FITOM_X never waits for or observes this process's exit
    // code.
    fpe::PatchWorkspace kioskWorkspace;
    BankCategory kioskKind = BankCategory::Device;
    std::optional<size_t> kioskBankIndex;
    int kioskProg = 0;
    const bool kioskRequested = (argc == 5);
    if (kioskRequested) {
        const fs::path profilePath = argv[1];
        const std::optional<BankCategory> parsedKind = parseKioskKind(argv[2]);
        const fs::path bankFile = argv[3];
        if (!parsedKind) {
            showFatalErrorBox(std::string("キオスクモード: 不明な種別 '") + argv[2] +
                               "' です。'device'/'layered'/'performance'/'drum'/'pcmbank'/'samplezonebank' の"
                               "いずれかを指定してください。");
            return 1;
        }
        if (!kioskKindImplemented(*parsedKind)) {
            showFatalErrorBox(std::string("キオスクモード: 種別 '") + argv[2] +
                               "' はキーワードとして予約されていますが、まだ編集画面を実装していません。");
            return 1;
        }
        kioskKind = *parsedKind;
        try {
            kioskProg = std::stoi(argv[4]);
        } catch (const std::exception&) {
            showFatalErrorBox(std::string("キオスクモード: prog番号 '") + argv[4] + "' を解釈できません。");
            return 1;
        }
        try {
            kioskWorkspace.load(profilePath);
        } catch (const std::exception& e) {
            showFatalErrorBox("キオスクモード: プロファイルの読み込みに失敗しました:\n" + profilePath.string() +
                               "\n\n" + e.what());
            return 1;
        }
        switch (kioskKind) {
            case BankCategory::Device:
                kioskBankIndex = findDeviceBankIndexByFile(kioskWorkspace, bankFile);
                if (!kioskBankIndex || !kioskWorkspace.deviceBanks()[*kioskBankIndex].findByProg(kioskProg)) {
                    showFatalErrorBox("キオスクモード: 指定されたhwbankファイル/progに一致するデバイスパッチが"
                                       "プロファイル内に見つかりません:\n" +
                                       bankFile.string() + " prog " + std::to_string(kioskProg) + "\nプロファイル: " +
                                       profilePath.string());
                    return 1;
                }
                break;
            case BankCategory::Layered:
                kioskBankIndex = findLayeredBankIndexByFile(kioskWorkspace, bankFile);
                if (!kioskBankIndex || !kioskWorkspace.layeredPatchBanks()[*kioskBankIndex].findByProg(kioskProg)) {
                    showFatalErrorBox("キオスクモード: 指定されたpatchbankファイル/progに一致するレイヤードパッチが"
                                       "プロファイル内に見つかりません:\n" +
                                       bankFile.string() + " prog " + std::to_string(kioskProg) + "\nプロファイル: " +
                                       profilePath.string());
                    return 1;
                }
                break;
            case BankCategory::Performance:
                kioskBankIndex = findPerformanceBankIndexByFile(kioskWorkspace, bankFile);
                if (!kioskBankIndex || !kioskWorkspace.performanceBanks()[*kioskBankIndex].findByProg(kioskProg)) {
                    showFatalErrorBox("キオスクモード: 指定されたswbankファイル/progに一致するパフォーマンスパッチが"
                                       "プロファイル内に見つかりません:\n" +
                                       bankFile.string() + " prog " + std::to_string(kioskProg) + "\nプロファイル: " +
                                       profilePath.string());
                    return 1;
                }
                break;
            case BankCategory::Drum:
                kioskBankIndex = findDrumKitIndexByFile(kioskWorkspace, bankFile, kioskProg);
                if (!kioskBankIndex) {
                    showFatalErrorBox("キオスクモード: 指定されたdrumkitファイル/progに一致するドラムキットが"
                                       "プロファイル内に見つかりません:\n" +
                                       bankFile.string() + " prog " + std::to_string(kioskProg) + "\nプロファイル: " +
                                       profilePath.string());
                    return 1;
                }
                break;
            case BankCategory::Pcm:
            case BankCategory::SampleZone:
                break; // unreachable - kioskKindImplemented() already rejected these above
        }
    }

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        showFatalErrorBox("glfwInit() に失敗しました。");
        return 1;
    }

    // OpenGL 3.0 + GLSL 130: the same baseline Dear ImGui's own GLFW+OpenGL3
    // example uses, for maximum portability across platforms/drivers.
    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "FITOM_X Patch Editor", nullptr, nullptr);
    if (!window) {
        showFatalErrorBox("glfwCreateWindow() に失敗しました。");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        showFatalErrorBox("glewInit() に失敗しました。");
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
    // launched (see D-018). Kiosk mode loads Preferences too (for the same
    // RtMidi-fallback port/channel the normal preview keyboard uses,
    // D-026) but never consults autoLoadEnabled/argv[1]-as-profile - its
    // workspace/bank/prog were already resolved above.
    ctx.preferences = loadPreferences();
    ctx.previewOutput.configureRtMidiPort(ctx.preferences.midiPortIndex);

    if (kioskRequested) {
        ctx.kioskMode = true;
        ctx.kioskKind = kioskKind;
        ctx.workspace = std::move(kioskWorkspace);
        switch (kioskKind) {
            case BankCategory::Device:
                ctx.kioskEditor.bankIndex = *kioskBankIndex;
                ctx.kioskEditor.prog = kioskProg;
                ctx.kioskEditor.open = true;
                break;
            case BankCategory::Layered:
                ctx.kioskLayeredEditor.bankIndex = *kioskBankIndex;
                ctx.kioskLayeredEditor.prog = kioskProg;
                ctx.kioskLayeredEditor.open = true;
                break;
            case BankCategory::Performance:
                ctx.kioskPerformanceEditor.bankIndex = *kioskBankIndex;
                ctx.kioskPerformanceEditor.prog = kioskProg;
                ctx.kioskPerformanceEditor.open = true;
                break;
            case BankCategory::Drum:
                ctx.kioskDrumEditor.kitIndex = *kioskBankIndex;
                ctx.kioskDrumEditor.open = true;
                break;
            case BankCategory::Pcm:
            case BankCategory::SampleZone:
                break; // unreachable - kioskKindImplemented() already rejected these above
        }
    } else if (argc > 1) {
        // argv[1], if given, is the path to the profile that should already
        // be "open" on startup (see file-level comment above). Loading
        // doesn't touch the GL/ImGui state, so it's safe to do before the
        // render loop starts; tryLoadProfile() already handles success
        // (-> Outline) and failure (errorMessage set, state stays
        // MainMenu) uniformly with the FileBrowser pick path.
        tryLoadProfile(ctx, fs::path(argv[1]));
    } else if (ctx.preferences.autoLoadEnabled && !ctx.preferences.autoLoadProfilePath.empty()) {
        tryLoadProfile(ctx, fs::path(ctx.preferences.autoLoadProfilePath));
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // D-044: auto-stops a drum-note list single-click preview once its
        // fixed duration elapses - unconditional so it still fires even if
        // the user navigated away from the drum-note screen mid-preview
        // (kiosk or normal, doesn't matter which screen is showing now).
        updateDrumNoteListPreview(ctx);

        if (ctx.kioskMode) {
            // No outer "FITOM_X Patch Editor" menu/outline frame at all in
            // this mode (D-026) - just the one editor (Device/Layered/
            // Performance/Drum, per ctx.kioskKind - D-039/D-040), forced to
            // fill the whole viewport every frame (so it reads as "docked
            // full-size" rather than a movable/resizable floating window,
            // per the project owner's fallback if a truly chrome-less
            // window turned out to be more complex than it's worth). It
            // keeps its own title bar/close-X (unlike the borderless ideal)
            // specifically so there's still an obvious, discoverable way to
            // finish editing - closing it exits the whole process.
            bool* kioskOpen = &ctx.kioskEditor.open; // default covers unreachable Pcm/SampleZone below
            switch (ctx.kioskKind) {
                case BankCategory::Device:      kioskOpen = &ctx.kioskEditor.open; break;
                case BankCategory::Layered:     kioskOpen = &ctx.kioskLayeredEditor.open; break;
                case BankCategory::Performance: kioskOpen = &ctx.kioskPerformanceEditor.open; break;
                case BankCategory::Drum:        kioskOpen = &ctx.kioskDrumEditor.open; break;
                case BankCategory::Pcm:
                case BankCategory::SampleZone:  break; // unreachable - kioskKindImplemented() rejects these at startup
            }
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("パッチ編集", kioskOpen,
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
            switch (ctx.kioskKind) {
                case BankCategory::Device:      renderPatchEditor(ctx, ctx.kioskEditor); break;
                case BankCategory::Layered:     renderLayeredPatchEditor(ctx, ctx.kioskLayeredEditor); break;
                case BankCategory::Performance: renderPerformancePatchEditor(ctx, ctx.kioskPerformanceEditor); break;
                case BankCategory::Drum:        renderDrumKitDetail(ctx, ctx.kioskDrumEditor.kitIndex); break;
                case BankCategory::Pcm:
                case BankCategory::SampleZone:  break; // unreachable, see above
            }
            // Rendered unconditionally regardless of kind, same as the
            // non-kiosk branch below - each of these popups is a no-op
            // unless the content above actually opened the one it belongs
            // to (e.g. the ToneLayer hw_bank picker only exists for the
            // Layered kind, and the drum-note pickers only for the Drum
            // kind, but rendering all of them here regardless of kind is
            // harmless since nothing ever populates the unrelated ones).
            renderSwPatchPicker(ctx); // sw_bank/sw_prog label click, see openSwPatchPicker()
            renderHwPatchPicker(ctx); // ToneLayer hw_bank/hw_prog label click, see openHwPatchPicker()
            renderDrumSourcePatchPicker(ctx); // drum-note source-patch label click, see openDrumSourcePatchPicker()
            renderDrumNoteKeyboardPicker(ctx); // drum-note "キーボードで選択" click, see openDrumNoteKeyboardPicker()
            renderErrorPopup(ctx); // e.g. "登録" save failures - kiosk mode has no menu to show them elsewhere
            ImGui::End();
            // Any of the four kinds' content above can have opened nested
            // modeless editor windows via a "編集"/note-selection click
            // (Device's own sw_bank row, a Layered patch's ToneLayer/sw_bank
            // rows, a Drum kit's per-note rows or direct-kit source-patch
            // row) - render those as their own sibling windows (not nested
            // inside "パッチ編集"), same as the non-kiosk branch below. Each
            // already handles its own D-027 close-time SysEx flush
            // internally where relevant, so nothing extra is needed here
            // for them.
            renderPatchEditors(ctx);
            renderLayeredPatchEditors(ctx);
            renderPerformancePatchEditors(ctx);
            renderDrumNoteEditors(ctx);
            if (!*kioskOpen) {
                // Only the Device kind itself streams live SysEx (D-027) -
                // Layered/Performance/Drum have no synthesis parameters of
                // their own to preview at the top level (see
                // renderLayeredPatchEditor()'s comment; the Drum screen's
                // note-level preview button doesn't stream differential
                // SysEx either, see D-038), so there is nothing to flush for
                // them here; any nested Device editor they opened already
                // flushed itself via renderPatchEditors() above when it was
                // closed.
                if (ctx.kioskKind == BankCategory::Device) {
                    sendFullRegisteredOverride(ctx, ctx.kioskEditor); // D-027, same as the normal editor windows' close handling
                }
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        } else {
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
            renderSwPatchPicker(ctx); // sw_bank/sw_prog label click, see openSwPatchPicker()/openDrumNoteSwPatchPicker()
            renderLayeredPatchEditors(ctx);
            renderPerformancePatchEditors(ctx);
            renderDrumNoteEditors(ctx);
            renderHwPatchPicker(ctx); // ToneLayer hw_bank/hw_prog label click, see openHwPatchPicker()
            renderDrumSourcePatchPicker(ctx); // drum-note source-patch label click, see openDrumSourcePatchPicker()
            renderDrumNoteKeyboardPicker(ctx); // drum-note "キーボードで選択" click, see openDrumNoteKeyboardPicker()
            renderNewBankDialog(ctx);
            renderPreferencesDialog(ctx); // also renders the shared path-picker modal, nested inside its own popup (see D-019)
            renderErrorPopup(ctx);

            ImGui::End();
        }

        // D-030: FITOM_X's MIDI pipe accepted our connection but explicitly
        // refused to assign a channel because 16 other patch editor
        // instances are already connected (docs/plugin-midi-pipe.md 4.1).
        // Unlike "FITOM_X isn't running" (silently fall back to offline/
        // RtMidi), this is a real error the user needs to act on - show it
        // and exit, same pattern as showFatalErrorBox()'s other callers
        // (D-029), just reachable mid-session instead of only at startup.
        if (ctx.previewOutput.pipeRejectedForCapacity()) {
            showFatalErrorBox(
                "FITOM_Xへの接続数が上限(16)に達しているため、これ以上パッチエディタで試聴接続できません。\n"
                "他のパッチエディタウィンドウを閉じてから、もう一度開いてください。");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

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
