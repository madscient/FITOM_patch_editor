// Smoke test for the fpe_data library: loads the tests/../fixtures profile,
// checks the loaded values against what's in the fixture JSON, exercises
// CRUD + save + reload round-trip, and checks VoicePatchType conversions.
//
// Not a full unit test suite (no framework dependency by design, to keep
// the build simple) - just enough to prove the load/edit/save path works
// end to end.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "fpe/PatchWorkspace.h"
#include "fpe/VoicePatchType.h"

namespace fs = std::filesystem;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_failures;                                                   \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

static fs::path fixturesDir() {
    // tests/smoke_test.cpp is built from the project root; fixtures/ is a
    // sibling of tests/ and src/.
    fs::path here = fs::current_path();
    // Search upward for a "fixtures/profile.json" so this works whether
    // ctest runs from the build dir or the source dir.
    for (fs::path p = here; !p.empty(); p = p.parent_path()) {
        if (fs::exists(p / "fixtures" / "profile.json")) return p / "fixtures";
        if (p == p.root_path()) break;
    }
    // Fall back to a path relative to this source file, resolved at
    // configure time via a compile definition (see CMakeLists.txt).
#ifdef FPE_FIXTURES_DIR
    return fs::path(FPE_FIXTURES_DIR);
#else
    return here / "fixtures";
#endif
}

static void testVoicePatchType() {
    using fpe::VoicePatchType;
    CHECK(fpe::stringToVoicePatchType("OPM") == VoicePatchType::OPM);
    CHECK(fpe::stringToVoicePatchType("AWM") == VoicePatchType::AWM);
    CHECK(!fpe::stringToVoicePatchType("NOT_A_CHIP").has_value());
    CHECK(fpe::voicePatchTypeToString(VoicePatchType::OPZ2) == "OPZ2");
    CHECK(fpe::isSampleBasedVoicePatchType(VoicePatchType::AWM));
    CHECK(fpe::isSampleBasedVoicePatchType(VoicePatchType::ADPCMB_Y8950));
    CHECK(!fpe::isSampleBasedVoicePatchType(VoicePatchType::OPM));
    CHECK(!fpe::isValidHwBankTag(VoicePatchType::None));
    CHECK(fpe::isValidHwBankTag(VoicePatchType::SSG));
}

static void testLoad(fpe::PatchWorkspace& ws) {
    for (const auto& w : ws.warnings()) {
        std::fprintf(stderr, "load warning: %s\n", w.c_str());
    }
    CHECK(ws.warnings().empty());

    CHECK(ws.profile().profile_name == "Test Profile");
    CHECK(ws.profile().extra.contains("midi_inputs"));
    CHECK(ws.profile().extra["midi_inputs"][0] == "Test Input");

    CHECK(ws.nativePatchBanks().size() == 1);
    CHECK(ws.performanceBanks().size() == 1);
    CHECK(ws.deviceBanks().size() == 1);
    CHECK(ws.drumKits().size() == 2);

    auto* patchBank = ws.findNativePatchBank(0);
    CHECK(patchBank != nullptr);
    if (patchBank) {
        CHECK(patchBank->name == "General");
        auto* patch = patchBank->findByProg(0);
        CHECK(patch != nullptr);
        if (patch) {
            CHECK(patch->name == "Test Strings");
            CHECK(patch->layers.size() == 1);
            CHECK(patch->layers[0].voice_patch_type == fpe::VoicePatchType::OPM);
            CHECK(patch->layers[0].note_range_hi == 127);
            CHECK(patch->layers[0].enabled == true);
        }
    }

    auto* hwBank = ws.findDeviceBank(fpe::VoicePatchType::OPM, 0);
    CHECK(hwBank != nullptr);
    if (hwBank) {
        CHECK(hwBank->patches.size() == 1);
        auto* hwPatch = hwBank->findByProg(0);
        CHECK(hwPatch != nullptr);
        if (hwPatch) {
            CHECK(hwPatch->ops.size() == 4);
            CHECK(hwPatch->hw.FB == 3);
            CHECK(hwPatch->sw_bank == 0);
            CHECK(hwPatch->sw_prog == 0);
        }
    }

    // Reference-following: HwPatch.sw_bank/sw_prog -> SwPatch
    if (hwBank) {
        auto* hwPatch = hwBank->findByProg(0);
        if (hwPatch) {
            auto* swPatch = ws.resolvePerformancePatch(hwPatch->sw_bank, hwPatch->sw_prog);
            CHECK(swPatch != nullptr);
            if (swPatch) CHECK(swPatch->name == "Slow Vibrato");
        }
    }

    auto* routedKit = ws.findDrumKit(0);
    CHECK(routedKit != nullptr);
    if (routedKit) {
        CHECK(routedKit->type == fpe::DrumKitType::Routed);
        CHECK(routedKit->notes.size() == 3);
        CHECK(routedKit->choke_groups.size() == 1);
        CHECK(routedKit->choke_groups[0].size() == 3);
        auto* note = routedKit->findNote(42);
        CHECK(note != nullptr);
        if (note) CHECK(note->name == "Closed Hi-Hat");
    }

    auto* directKit = ws.findDrumKit(1);
    CHECK(directKit != nullptr);
    if (directKit) {
        CHECK(directKit->type == fpe::DrumKitType::Direct);
        auto notes = directKit->effectiveNotes();
        CHECK(notes.size() == static_cast<size_t>(directKit->note_max - directKit->note_min + 1));
        CHECK(notes.front().note == directKit->note_min);
        CHECK(notes.front().play_note == directKit->note_min);
    }
}

static void testCrudAndRoundTrip(fpe::PatchWorkspace& ws, const fs::path& outDir) {
    // Native patch bank / patch / tone layer CRUD
    auto& newPatchBank = ws.createNativePatchBank(1, "User Bank", "patches/01_user.patchbank.json");
    auto& patch = ws.createPatch(newPatchBank, 5, "My Lead");
    fpe::ToneLayer layer;
    layer.voice_patch_type = fpe::VoicePatchType::OPN2;
    layer.hw_bank = 0;
    layer.hw_prog = 3;
    patch.layers.push_back(layer);
    CHECK(newPatchBank.patches.size() == 1);

    auto* dup = ws.duplicatePatch(newPatchBank, 5, 6);
    CHECK(dup != nullptr);
    if (dup) CHECK(dup->layers.size() == 1);
    CHECK(newPatchBank.patches.size() == 2);

    CHECK(ws.deletePatch(newPatchBank, 6));
    CHECK(newPatchBank.patches.size() == 1);

    // Performance bank / patch CRUD
    auto& newSwBank = ws.createPerformanceBank(1, "User SW Bank", "sw/user.swbank.json");
    ws.createPerformancePatch(newSwBank, 0, "Fast Vibrato");
    CHECK(newSwBank.patches.size() == 1);

    // Device bank / voice patch CRUD
    auto& newHwBank = ws.createDeviceBank(fpe::VoicePatchType::SSG, 0, "PSG Bank", "banks/SSG/00.hwbank.json");
    ws.createDeviceVoicePatch(newHwBank, 0, "Square Lead");
    CHECK(newHwBank.patches.size() == 1);
    CHECK(ws.findDeviceBank(fpe::VoicePatchType::SSG, 0) == &newHwBank);

    // Drum kit / note CRUD
    auto& newKit = ws.createDrumKit(2, "User Kit", "drums/user.drumkit.json");
    fpe::DrumNote note;
    note.note = 40;
    note.name = "Electric Snare";
    note.play_note = 40;
    ws.upsertDrumNote(newKit, note);
    CHECK(newKit.notes.size() == 1);
    note.name = "Electric Snare (renamed)";
    ws.upsertDrumNote(newKit, note); // same note number -> replace, not append
    CHECK(newKit.notes.size() == 1);
    CHECK(newKit.notes[0].name == "Electric Snare (renamed)");

    // Save to a scratch directory and reload into a fresh workspace.
    fs::create_directories(outDir);
    fs::path savedProfile = outDir / "profile.json";
    ws.saveAs(savedProfile);
    CHECK(fs::exists(savedProfile));
    CHECK(fs::exists(outDir / "patches" / "01_user.patchbank.json"));
    CHECK(fs::exists(outDir / "drums" / "user.drumkit.json"));

    fpe::PatchWorkspace reloaded;
    reloaded.load(savedProfile);
    for (const auto& w : reloaded.warnings()) std::fprintf(stderr, "reload warning: %s\n", w.c_str());
    CHECK(reloaded.warnings().empty());

    CHECK(reloaded.nativePatchBanks().size() == 2);
    auto* reloadedUserBank = reloaded.findNativePatchBank(1);
    CHECK(reloadedUserBank != nullptr);
    if (reloadedUserBank) {
        auto* reloadedPatch = reloadedUserBank->findByProg(5);
        CHECK(reloadedPatch != nullptr);
        if (reloadedPatch) {
            CHECK(reloadedPatch->name == "My Lead");
            CHECK(reloadedPatch->layers.size() == 1);
            CHECK(reloadedPatch->layers[0].voice_patch_type == fpe::VoicePatchType::OPN2);
            CHECK(reloadedPatch->layers[0].hw_prog == 3);
        }
    }

    auto* reloadedKit = reloaded.findDrumKit(2);
    CHECK(reloadedKit != nullptr);
    if (reloadedKit) {
        CHECK(reloadedKit->notes.size() == 1);
        CHECK(reloadedKit->notes[0].name == "Electric Snare (renamed)");
    }

    // Original data (loaded from the fixture, untouched by CRUD above)
    // must have round-tripped byte-for-byte-equivalent too.
    auto* reloadedGeneral = reloaded.findNativePatchBank(0);
    CHECK(reloadedGeneral != nullptr);
    if (reloadedGeneral) {
        auto* p = reloadedGeneral->findByProg(0);
        CHECK(p != nullptr);
        if (p) CHECK(p->name == "Test Strings");
    }
}

static void testDefaults() {
    // Fields not present in the JSON must fall back to the documented
    // defaults rather than erroring.
    nlohmann::json j = {{"prog", 7}}; // name/poly/sw_bank/sw_prog/layers all omitted
    fpe::Patch p = j.get<fpe::Patch>();
    CHECK(p.prog == 7);
    CHECK(p.name.empty());
    CHECK(p.poly == 0);
    CHECK(p.sw_bank == -1);
    CHECK(p.sw_prog == -1);
    CHECK(p.layers.empty());

    nlohmann::json layerJson = {{"voice_patch_type", 0x40}, {"hw_bank", 2}, {"hw_prog", 1}};
    fpe::ToneLayer layer = layerJson.get<fpe::ToneLayer>();
    CHECK(layer.note_range_lo == 0);
    CHECK(layer.note_range_hi == 127);
    CHECK(layer.enabled == true);
    CHECK(layer.transpose == 0);
}

int main() {
    testVoicePatchType();
    testDefaults();

    fpe::PatchWorkspace ws;
    ws.load(fixturesDir() / "profile.json");
    testLoad(ws);

    fs::path scratch = fs::temp_directory_path() / "fpe_smoke_test_out";
    if (fs::exists(scratch)) fs::remove_all(scratch);
    testCrudAndRoundTrip(ws, scratch);

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    if (g_failures > 0) {
        std::fprintf(stderr, "%d CHECK(s) FAILED\n", g_failures);
        return 1;
    }
    return 0;
}
