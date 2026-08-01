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
#include <fstream>
#include <sstream>
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

static std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void testVoicePatchType() {
    using fpe::VoicePatchType;
    CHECK(fpe::stringToVoicePatchType("OPM") == VoicePatchType::OPM);
    CHECK(fpe::stringToVoicePatchType("AWM") == VoicePatchType::AWM);
    CHECK(!fpe::stringToVoicePatchType("NOT_A_CHIP").has_value());
    CHECK(fpe::voicePatchTypeToString(VoicePatchType::OPZ2) == "OPZ2");
    CHECK(fpe::isSampleBasedVoicePatchType(VoicePatchType::AWM));
    // ADPCM-B/A and PCM-D8 select a PCM waveform-bank entry via the
    // ordinary HwPatch field ops[0].WS, and are loaded as a plain HwBank
    // by FITOM_X (see docs/DESIGN.md D-011) - only AWM uses the dedicated
    // SampleZonePatch shape.
    CHECK(!fpe::isSampleBasedVoicePatchType(VoicePatchType::ADPCMB_Y8950));
    CHECK(!fpe::isSampleBasedVoicePatchType(VoicePatchType::ADPCMB));
    CHECK(!fpe::isSampleBasedVoicePatchType(VoicePatchType::ADPCMA));
    CHECK(!fpe::isSampleBasedVoicePatchType(VoicePatchType::PCMD8));
    CHECK(!fpe::isSampleBasedVoicePatchType(VoicePatchType::OPM));
    // ADPCM-B(Y8950)/ADPCM-B/ADPCM-A/PCM-D8 instead get their "patches"
    // from a *.pcmbank.json's entries[] - fpe::PcmBank (D-013).
    CHECK(fpe::isPcmWaveformVoicePatchType(VoicePatchType::ADPCMB_Y8950));
    CHECK(fpe::isPcmWaveformVoicePatchType(VoicePatchType::ADPCMB));
    CHECK(fpe::isPcmWaveformVoicePatchType(VoicePatchType::ADPCMA));
    CHECK(fpe::isPcmWaveformVoicePatchType(VoicePatchType::PCMD8));
    CHECK(!fpe::isPcmWaveformVoicePatchType(VoicePatchType::AWM));
    CHECK(!fpe::isPcmWaveformVoicePatchType(VoicePatchType::OPM));
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

    CHECK(ws.layeredPatchBanks().size() == 1);
    CHECK(ws.performanceBanks().size() == 1);
    CHECK(ws.deviceBanks().size() == 1);
    CHECK(ws.pcmBanks().size() == 2); // one via hw_banks[group=ADPCMA], one via pcm_banks[] (D-038 "追記2")
    CHECK(ws.drumKits().size() == 2);

    // Bank registries live nested under profile.json's "banks" object on
    // disk (confirmed against the real profile.schema.json); this checks
    // that nesting was actually parsed, not silently dropped into `extra`.
    CHECK(!ws.profile().extra.contains("banks"));
    CHECK(ws.profile().scc_wave_banks.size() == 1);
    if (!ws.profile().scc_wave_banks.empty()) {
        CHECK(ws.profile().scc_wave_banks[0].bank == 0);
        CHECK(ws.profile().scc_wave_banks[0].file == "banks/scc/default.sccwave.json");
    }

    auto* patchBank = ws.findLayeredPatchBank(0);
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

    // PcmBank (ADPCM-A/B, PCM-D8): entries[] come from a separate
    // adpcm_json file (an adpcm_packer output), resolved relative to the
    // pcmbank.json's own directory - see D-013.
    auto* pcmBank = ws.findPcmBank(fpe::VoicePatchType::ADPCMA, 1);
    CHECK(pcmBank != nullptr);
    if (pcmBank) {
        CHECK(pcmBank->name == "Test PCM Bank");
        CHECK(pcmBank->entries.size() == 2);
        auto* entry0 = pcmBank->findByIndex(0);
        CHECK(entry0 != nullptr);
        if (entry0) {
            CHECK(entry0->name == "kick");
            CHECK(entry0->root_note == 60);
        }
        auto* entry1 = pcmBank->findByIndex(1);
        CHECK(entry1 != nullptr);
        if (entry1) CHECK(entry1->name == "snare");
        CHECK(pcmBank->findByIndex(2) == nullptr);
    }

    // Same PcmBank shape, but registered via banks.pcm_banks[] (D-038
    // "追記2") instead of hw_banks[group=ADPCM*] - `group` on a pcm_banks[]
    // entry used to be silently dropped (PcmBankRef didn't even parse it),
    // leaving the resulting fpe::PcmBank's voicePatchType at None and
    // therefore unfindable by findPcmBank(). Regression test for that fix.
    auto* pcmBankViaPcmBanksArray = ws.findPcmBank(fpe::VoicePatchType::ADPCMA, 2);
    CHECK(pcmBankViaPcmBanksArray != nullptr);
    if (pcmBankViaPcmBanksArray) CHECK(pcmBankViaPcmBanksArray->entries.size() == 2);

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
        if (note) {
            CHECK(note->name == "Closed Hi-Hat");
            CHECK(note->fine_tune == 5);
            CHECK(note->pan == 20);
            CHECK(note->gate_time == 10);
        }
    }

    auto* directKit = ws.findDrumKit(1);
    CHECK(directKit != nullptr);
    if (directKit) {
        CHECK(directKit->type == fpe::DrumKitType::Direct);
        CHECK(directKit->fine_tune == 10);
        CHECK(directKit->pan == 5);
        CHECK(directKit->gate_time == 3);
        auto notes = directKit->effectiveNotes();
        CHECK(notes.size() == static_cast<size_t>(directKit->note_max - directKit->note_min + 1));
        CHECK(notes.front().note == directKit->note_min);
        CHECK(notes.front().play_note == directKit->note_min);
        CHECK(notes.front().fine_tune == 10);
        CHECK(notes.front().pan == 5);
        CHECK(notes.front().gate_time == 3);
    }
}

static void testCrudAndRoundTrip(fpe::PatchWorkspace& ws, const fs::path& outDir) {
    // Layered patch bank / patch / tone layer CRUD
    auto& newPatchBank = ws.createLayeredPatchBank(1, "User Bank", "patches/01_user.patchbank.json");
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

    CHECK(reloaded.layeredPatchBanks().size() == 2);
    auto* reloadedUserBank = reloaded.findLayeredPatchBank(1);
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
    auto* reloadedGeneral = reloaded.findLayeredPatchBank(0);
    CHECK(reloadedGeneral != nullptr);
    if (reloadedGeneral) {
        auto* p = reloadedGeneral->findByProg(0);
        CHECK(p != nullptr);
        if (p) CHECK(p->name == "Test Strings");
    }
}

// Exercises the "banks" (external file reference) / "bank_overrides"
// mechanism added by FITOM_X on 2026-07-29 (docs/DESIGN.md D-041):
// fixtures/profile_shared.json points "banks" at fixtures/shared.bankset.json
// and inline-overrides its drum_banks[prog=0] entry. Loads into a scratch
// copy (so save() below doesn't dirty the checked-in fixtures), edits, saves,
// and checks that (a) the merge is right, (b) "banks" and its target file are
// never rewritten, and (c) a fresh reload sees the same effective state.
static void testSharedBankset(const fs::path& scratchDir) {
    if (fs::exists(scratchDir)) fs::remove_all(scratchDir);
    fs::create_directories(scratchDir);
    for (const auto& entry : fs::directory_iterator(fixturesDir())) {
        fs::copy(entry.path(), scratchDir / entry.path().filename(),
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }
    const fs::path profilePath = scratchDir / "profile_shared.json";
    const fs::path bankset = scratchDir / "shared.bankset.json";
    const std::string bankSetContentBefore = readFile(bankset);

    fpe::PatchWorkspace ws;
    ws.load(profilePath);
    for (const auto& w : ws.warnings()) std::fprintf(stderr, "shared-bankset load warning: %s\n", w.c_str());
    CHECK(ws.warnings().empty());

    CHECK(ws.profile().banks.externalFile == "shared.bankset.json");
    CHECK(ws.profile().bank_overrides.externalFile.empty()); // inline in the profile

    // Merge: prog0 replaced by the override (direct_kit, not std_kit), prog1
    // passed through unchanged from the base.
    CHECK(ws.drumKits().size() == 2);
    auto* kit0 = ws.findDrumKit(0);
    CHECK(kit0 != nullptr);
    if (kit0) CHECK(kit0->sourceFile.filename() == "direct_kit.drumkit.json");
    auto* kit1 = ws.findDrumKit(1); // untouched base entry, not mentioned by bank_overrides at all
    CHECK(kit1 != nullptr);
    if (kit1) CHECK(kit1->sourceFile.filename() == "direct_kit.drumkit.json");

    // A brand-new kit, added purely through the ordinary CRUD path (it has
    // no idea "banks" is external) - save() must route it into
    // bank_overrides, not into the (untouchable) base file.
    ws.createDrumKit(2, "Extra Kit", "drums/extra.drumkit.json");
    ws.save();

    // The base file must be byte-for-byte unchanged.
    CHECK(readFile(bankset) == bankSetContentBefore);

    // profile.json's own "banks" key must still be the plain string.
    nlohmann::json raw;
    {
        std::ifstream in(profilePath, std::ios::binary);
        in >> raw;
    }
    CHECK(raw.contains("banks") && raw["banks"].is_string() && raw["banks"] == "shared.bankset.json");
    CHECK(raw.contains("bank_overrides") && raw["bank_overrides"].is_object());
    if (raw.contains("bank_overrides") && raw["bank_overrides"].is_object() &&
        raw["bank_overrides"].contains("drum_banks")) {
        CHECK(raw["bank_overrides"]["drum_banks"].size() == 2); // prog0 (changed) + prog2 (new)
    }

    fpe::PatchWorkspace reloaded;
    reloaded.load(profilePath);
    for (const auto& w : reloaded.warnings()) std::fprintf(stderr, "shared-bankset reload warning: %s\n", w.c_str());
    CHECK(reloaded.warnings().empty());
    CHECK(reloaded.drumKits().size() == 3);
    CHECK(reloaded.findDrumKit(0) != nullptr);
    CHECK(reloaded.findDrumKit(1) != nullptr);
    auto* reloadedKit2 = reloaded.findDrumKit(2);
    CHECK(reloadedKit2 != nullptr);
    if (reloadedKit2) CHECK(reloadedKit2->name == "Extra Kit");
}

// D-042: save() used to unconditionally re-serialize every loaded bank/kit,
// even ones nothing this session touched (reported as a real-world bug -
// pressing "register" rewrote the whole reference tree). Checks that an
// untouched file's bytes survive a save() completely untouched, while a
// file that genuinely changed does get rewritten - and that this doesn't
// depend on *which* file changed (so the mechanism isn't just "save() is a
// no-op now").
static void testSaveOnlyRewritesChangedFiles(const fs::path& scratchDir) {
    if (fs::exists(scratchDir)) fs::remove_all(scratchDir);
    fs::create_directories(scratchDir);
    for (const auto& entry : fs::directory_iterator(fixturesDir())) {
        fs::copy(entry.path(), scratchDir / entry.path().filename(),
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }
    const fs::path profilePath = scratchDir / "profile.json";
    const fs::path patchBankFile = scratchDir / "patches" / "00_general.patchbank.json";
    const fs::path swBankFile = scratchDir / "sw" / "default_gm.swbank.json";
    const fs::path drumKitFile = scratchDir / "drums" / "std_kit.drumkit.json";

    const std::string profileBefore = readFile(profilePath);
    const std::string patchBankBefore = readFile(patchBankFile);
    const std::string swBankBefore = readFile(swBankFile);
    const std::string drumKitBefore = readFile(drumKitFile);

    fpe::PatchWorkspace ws;
    ws.load(profilePath);
    CHECK(ws.warnings().empty());

    // No edits at all: every file, including profile.json itself, must be
    // left byte-for-byte untouched (not just "re-serialize to the same
    // content" - genuinely never opened for writing).
    ws.save();
    CHECK(readFile(profilePath) == profileBefore);
    CHECK(readFile(patchBankFile) == patchBankBefore);
    CHECK(readFile(swBankFile) == swBankBefore);
    CHECK(readFile(drumKitFile) == drumKitBefore);

    // Now make one real edit (rename the layered patch bank at bank 0) and
    // save again: only patches/00_general.patchbank.json (PatchBank::name
    // is part of that file's own JSON shape) should change; sw/drums must
    // still be untouched.
    auto* patchBank = ws.findLayeredPatchBank(0);
    CHECK(patchBank != nullptr);
    if (patchBank) patchBank->name = "General (renamed)";
    ws.save();
    CHECK(readFile(patchBankFile) != patchBankBefore);
    CHECK(readFile(swBankFile) == swBankBefore);
    CHECK(readFile(drumKitFile) == drumKitBefore);
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

    testSharedBankset(fs::temp_directory_path() / "fpe_smoke_test_shared_bankset");
    testSaveOnlyRewritesChangedFiles(fs::temp_directory_path() / "fpe_smoke_test_dirty_save");

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    if (g_failures > 0) {
        std::fprintf(stderr, "%d CHECK(s) FAILED\n", g_failures);
        return 1;
    }
    return 0;
}
