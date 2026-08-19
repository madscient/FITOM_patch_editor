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

#include "fpe/BuiltinVoices.h"
#include "fpe/ChipCapabilities.h"
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
    // DSG is a recognized chip type, but never a bank tag - it owns no
    // *.hwbank.json at all (D-053).
    CHECK(!fpe::isValidHwBankTag(VoicePatchType::DSG));

    // Alias group strings FITOM_X's own stringToVoicePatchType() accepts.
    // Before D-053 an "OPNA"/"OPNB"/"SCCP"/"PSG"/"PCM" tagged bank - all
    // legal per profile.schema.json - was dropped outright with a warning.
    CHECK(fpe::stringToVoicePatchType("OPNA") == VoicePatchType::OPN2);
    CHECK(fpe::stringToVoicePatchType("OPNB") == VoicePatchType::OPN2);
    CHECK(fpe::stringToVoicePatchType("SCCP") == VoicePatchType::SCC);
    CHECK(fpe::stringToVoicePatchType("PSG") == VoicePatchType::SSG);
    CHECK(fpe::stringToVoicePatchType("PCM") == VoicePatchType::ADPCMB);
    CHECK(fpe::stringToVoicePatchType("AY8930") == VoicePatchType::EPSG);
    CHECK(fpe::stringToVoicePatchType("YM2163") == VoicePatchType::DSG);
    CHECK(!fpe::stringToVoicePatchType("NOSUCHCHIP").has_value());
    // Aliases are input-only: the canonical spelling is what round-trips.
    CHECK(fpe::voicePatchTypeToString(VoicePatchType::OPN2) == "OPN2");
    CHECK(fpe::voicePatchTypeToString(VoicePatchType::DSG) == "DSG");

    // The PSG family is the one family sharing a single bank namespace, so
    // every {type,bank} HwBank lookup has to collapse onto SSG for it.
    CHECK(fpe::isPsgFamilyVoicePatchType(VoicePatchType::EPSG));
    CHECK(fpe::isPsgFamilyVoicePatchType(VoicePatchType::SCC));
    // DSG must stay out of it - it has no user banks, so folding it in would
    // list every SSG bank under the DSG category (FITOM_X hit exactly this
    // and removed it again in 54830f3).
    CHECK(!fpe::isPsgFamilyVoicePatchType(VoicePatchType::DSG));
    CHECK(!fpe::isPsgFamilyVoicePatchType(VoicePatchType::OPN));
    CHECK(fpe::hwBankLookupVoicePatchType(VoicePatchType::SCC) == VoicePatchType::SSG);
    CHECK(fpe::hwBankLookupVoicePatchType(VoicePatchType::DSG) == VoicePatchType::DSG);
    CHECK(fpe::hwBankLookupVoicePatchType(VoicePatchType::OPM) == VoicePatchType::OPM);
}

// D-050/D-053: the families FITOM_X synthesizes internally instead of loading
// from a *.hwbank.json (fpe/BuiltinVoices.h).
static void testBuiltinVoices() {
    using fpe::VoicePatchType;

    // --- OPLL-family ROM voices (hw_bank 0) ---
    CHECK(fpe::opllRomVariantSel(VoicePatchType::OPLL) == 0);
    CHECK(fpe::opllRomVariantSel(VoicePatchType::OPLLX) == 1);
    CHECK(fpe::opllRomVariantSel(VoicePatchType::OPLLP) == 2);
    CHECK(fpe::opllRomVariantSel(VoicePatchType::VRC7) == 3);
    CHECK(fpe::opllRomVariantSel(VoicePatchType::OPL2) == -1);
    CHECK(fpe::isOpllRomVoiceRef(VoicePatchType::OPLL, 0));
    CHECK(!fpe::isOpllRomVoiceRef(VoicePatchType::OPLL, 2)); // real JSON bank
    CHECK(!fpe::isOpllRomVoiceRef(VoicePatchType::OPL2, 0));

    // 15 voices per variant (index 0 = deliberately-reserved silence), and
    // `prog` must be (variantSel<<4)|instIndex - NOT the list index. Sending
    // the raw index would make every non-OPLL category sound the wrong
    // chip's voices, since FITOM_X's resolveOpllRomVoice() re-decodes the
    // variant out of the program number itself
    // (FITOM_X docs/patch-structure-design.md「実装上の注意」).
    const auto opllx = fpe::opllRomVoices(VoicePatchType::OPLLX);
    CHECK(opllx.size() == 15);
    CHECK(opllx.front().prog == 0x11);
    CHECK(opllx.front().name == "Strings");
    CHECK(opllx.back().prog == 0x1F);
    CHECK(fpe::opllRomVoices(VoicePatchType::OPL2).empty());

    // Decoding is driven purely by hw_prog and ignores the referencing
    // category - staging's gm_layered_opll.patchbank.json really does point
    // voice_patch_type=OPLL layers at hw_prog=35 (=OPLLP's "Piano").
    const fpe::OpllRomVoiceRef rom = fpe::opllRomVoiceByProg(35);
    CHECK(rom.valid);
    CHECK(rom.variant == VoicePatchType::OPLLP);
    CHECK(rom.instIndex == 3);
    CHECK(rom.name == "Electric Guitar"); // f6dfd8d renamed OPLLP index 3 away from "Piano"
    CHECK(!fpe::opllRomVoiceByProg(0x20).valid);   // instIndex 0 = silence
    CHECK(!fpe::opllRomVoiceByProg(0x40).valid);   // variantSel 4 = undefined
    CHECK(fpe::opllRomVoiceByProg(0x01).name == "Violin");

    // --- DSG (YM2163) built-in voices ---
    // Unlike the OPLL ROM bank, these resolve at *any* hw_bank: DSG has no
    // user banks for a bank number to select between, and FITOM_X's
    // resolveDsgBuiltinVoice() never reads hw_bank at all.
    CHECK(fpe::isDsgBuiltinVoiceRef(VoicePatchType::DSG));
    CHECK(!fpe::isDsgBuiltinVoiceRef(VoicePatchType::SSG));

    // 5 waveforms x 4 envelopes, prog = wave*4 + env (a plain array index,
    // with no chip-variant bits packed in the way OPLL ROM progs have).
    const auto dsg = fpe::dsgBuiltinVoices();
    CHECK(dsg.size() == 20);
    CHECK(dsg.front().prog == 0);
    CHECK(dsg.front().name == "St.Percussive");
    CHECK(dsg.back().prog == 19);
    CHECK(dsg.back().name == "Hc.Plateau");
    CHECK(fpe::dsgBuiltinVoiceName(4) == "Or.Percussive");
    CHECK(fpe::dsgBuiltinVoiceName(19) == "Hc.Plateau");
    CHECK(fpe::dsgBuiltinVoiceName(20).empty());
    CHECK(fpe::dsgBuiltinVoiceName(-1).empty());

    // builtin_swpatch_meta bank's {patch_type, patch_no} form - one bank
    // mixes OPLL-family and DSG entries, told apart by patch_type.
    CHECK(fpe::builtinMetaVoiceName("VRC7", 1) == "Buzzy Bell");
    CHECK(fpe::builtinMetaVoiceName("OPLL", 0).empty());
    CHECK(fpe::builtinMetaVoiceName("OPL2", 1).empty());
    // patch_no 0 is reserved silence for OPLL but a real voice for DSG, so
    // the two ranges genuinely differ (OPLL系 1-15 / DSG 0-19).
    CHECK(fpe::builtinMetaVoiceName("DSG", 0) == "St.Percussive");
    CHECK(fpe::builtinMetaVoiceName("DSG", 19) == "Hc.Plateau");
    CHECK(fpe::builtinMetaVoiceName("DSG", 20).empty());

    // --- Built-in rhythm (voice_patch_type 0x70) ---
    // patch_bank holds the chip's own VoicePatchType, not a bank number:
    // OPN2(17)=OPNA with 6 parts, OPLL(40) with 5 - exactly what staging's
    // opna_builtin.drumkit.json / opll_rhythm.drumkit.json store.
    CHECK(fpe::builtinRhythmChips().size() == 3);
    CHECK(fpe::builtinRhythmChipLabel(static_cast<int>(VoicePatchType::OPN2)) == "OPNA");
    CHECK(fpe::builtinRhythmChipLabel(static_cast<int>(VoicePatchType::OPLL)) == "OPLL");
    CHECK(fpe::builtinRhythmChipLabel(static_cast<int>(VoicePatchType::OPL)).empty());
    CHECK(fpe::builtinRhythmParts(static_cast<int>(VoicePatchType::OPN2)).size() == 6);
    CHECK(fpe::builtinRhythmParts(static_cast<int>(VoicePatchType::OPLL)).size() == 5);
    CHECK(fpe::builtinRhythmParts(static_cast<int>(VoicePatchType::OPL)).empty());
    CHECK(fpe::builtinRhythmPartName(static_cast<int>(VoicePatchType::OPN2), 0) == "Bass Drum");
    CHECK(fpe::builtinRhythmPartName(static_cast<int>(VoicePatchType::OPN2), 5) == "Rim Shot");
    CHECK(fpe::builtinRhythmPartName(static_cast<int>(VoicePatchType::OPN2), 6).empty());
    // OPLL's part order differs from OPNA's (hardware register layout), so
    // the two tables genuinely cannot be shared.
    CHECK(fpe::builtinRhythmPartName(static_cast<int>(VoicePatchType::OPLL), 0) == "Hi-Hat");
    CHECK(fpe::builtinRhythmPartName(static_cast<int>(VoicePatchType::OPLL), 4) == "Bass Drum");
    CHECK(fpe::builtinRhythmPartName(static_cast<int>(VoicePatchType::OPLL), 5).empty());
    // DSG's 5 parts follow its rhythm trigger register's bit order, which is
    // a third distinct ordering again (CDSGRhythm::kTriggerBit).
    CHECK(fpe::builtinRhythmChipLabel(static_cast<int>(VoicePatchType::DSG)) == "DSG");
    CHECK(fpe::builtinRhythmParts(static_cast<int>(VoicePatchType::DSG)).size() == 5);
    CHECK(fpe::builtinRhythmPartName(static_cast<int>(VoicePatchType::DSG), 0) == "Bass Drum");
    CHECK(fpe::builtinRhythmPartName(static_cast<int>(VoicePatchType::DSG), 1) == "Hi Conga");
    CHECK(fpe::builtinRhythmPartName(static_cast<int>(VoicePatchType::DSG), 4) == "Hi-Hat Close");
    CHECK(fpe::builtinRhythmPartName(static_cast<int>(VoicePatchType::DSG), 5).empty());
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
    CHECK(ws.deviceBanks().size() == 2); // OPM bank 0 + PSG-family shared bank 5 (D-053)
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

    // PSG family shared bank namespace (D-053). The fixture registers this
    // bank with the legacy coarse group string "PSG", so this also covers the
    // alias table: before D-053 the bank was dropped outright at load.
    auto* psgBank = ws.findDeviceBank(fpe::VoicePatchType::SSG, 5);
    CHECK(psgBank != nullptr);
    if (psgBank) {
        CHECK(psgBank->voicePatchType == fpe::VoicePatchType::SSG);
        CHECK(psgBank->name == "PSG shared bank");
        // The whole point of the shared namespace: the same bank has to be
        // reachable from every PSG-family chip type, since real profiles
        // register all of them under "SSG"
        // (../FITOM_staging/config/profiles/unified.bankset.json) and each
        // patch names its real target chip via ext.target_voice_patch_type.
        CHECK(ws.findDeviceBank(fpe::VoicePatchType::EPSG, 5) == psgBank);
        CHECK(ws.findDeviceBank(fpe::VoicePatchType::SCC, 5) == psgBank);
        // ...but only within the family - other chips keep their own
        // per-chip-type namespace.
        CHECK(ws.findDeviceBank(fpe::VoicePatchType::OPM, 5) == nullptr);
        CHECK(ws.findDeviceBank(fpe::VoicePatchType::DSG, 5) == nullptr);
        auto* psgPatch = psgBank->findByProg(0);
        CHECK(psgPatch != nullptr);
        if (psgPatch) CHECK(psgPatch->ext.target_voice_patch_type == fpe::VoicePatchType::EPSG);
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

    // Regression test: prog0 and prog1 both resolve to direct_kit.drumkit.json
    // (see the merge check above) - two separate in-memory fpe::DrumKit
    // objects sharing one physical sourceFile, exactly the real-world
    // scenario a shared "banks" bankset (D-041) + a profile's own
    // bank_overrides can produce. Editing prog0 and saving must not have
    // prog1 (never touched this session) silently overwrite prog0's edit
    // back to the stale on-disk value. This was a real reported bug: D-049's
    // FITOM_X in-memory persistence reflected the edit correctly (it reads
    // the in-memory fpe::DrumKit object directly), but the file on disk kept
    // the pre-edit value - because save()'s old saveIfDirty() compared each
    // sibling against a baseline map it was ALSO mutating mid-pass, so
    // prog1's still-stale in-memory copy looked "dirty" relative to what
    // prog0 had just written and clobbered it right back.
    fpe::DrumKit* dupKit0 = reloaded.findDrumKit(0);
    CHECK(dupKit0 != nullptr);
    if (dupKit0) dupKit0->fine_tune = 42;
    reloaded.save();

    fpe::PatchWorkspace afterDupEdit;
    afterDupEdit.load(profilePath);
    for (const auto& w : afterDupEdit.warnings()) std::fprintf(stderr, "shared-bankset dup-edit warning: %s\n", w.c_str());
    CHECK(afterDupEdit.warnings().empty());
    fpe::DrumKit* afterKit0 = afterDupEdit.findDrumKit(0);
    CHECK(afterKit0 != nullptr);
    if (afterKit0) CHECK(afterKit0->fine_tune == 42);
    fpe::DrumKit* afterKit1 = afterDupEdit.findDrumKit(1);
    CHECK(afterKit1 != nullptr);
    // Same physical file as prog0 - correctly reflects prog0's just-saved
    // edit (not clobbered back to fine_tune=10, direct_kit.drumkit.json's
    // original value).
    if (afterKit1) CHECK(afterKit1->fine_tune == 42);
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

// spec/chip-capabilities.json (D-057). These checks double as a regression
// guard on the file itself: if a synced copy from FITOM_X ever changes a
// range or drops a parameter, the ones asserted here fail loudly instead of
// silently changing what the editor offers.
static void testChipCapabilities() {
    const fpe::ChipCapabilities& caps = fpe::ChipCapabilities::instance();
    CHECK(caps.loaded());
    if (!caps.loaded()) {
        std::fprintf(stderr, "  (chip-capabilities load error: %s)\n", caps.loadError().c_str());
        return;
    }
    CHECK(caps.chips().size() >= 30);

    // A chip's params[] is exhaustive: what is absent is never read.
    const fpe::ChipCaps* ssg = caps.chip(fpe::VoicePatchType::SSG);
    CHECK(ssg != nullptr);
    if (ssg) {
        CHECK(ssg->kind == fpe::ChipPatchKind::Fm);
        CHECK(ssg->operatorCount.value_or(0) == 1);
        CHECK(ssg->params.count("ALG") == 1);
        CHECK(ssg->params.count("NFQ") == 1);
        CHECK(ssg->params.count("HWEP") == 1);
        CHECK(ssg->params.count("FB") == 0);   // no feedback path on plain SSG
        CHECK(ssg->params.count("MUL") == 0);
        CHECK(ssg->params.count("WS") == 0);
    }
    // DSG has no user voices at all; SCC reads no channel-level field.
    const fpe::ChipCaps* dsg = caps.chip(fpe::VoicePatchType::DSG);
    CHECK(dsg && dsg->kind == fpe::ChipPatchKind::BuiltinRom);
    const fpe::ChipCaps* scc = caps.chip(fpe::VoicePatchType::SCC);
    CHECK(scc && scc->params.count("ALG") == 0 && scc->params.count("WS") == 1);

    // same_params_as is resolved at load, not deferred to callers.
    const fpe::ChipCaps* opz = caps.chip(fpe::VoicePatchType::OPZ);
    const fpe::ChipCaps* opz2 = caps.chip(fpe::VoicePatchType::OPZ2);
    CHECK(opz && opz2 && opz->params.size() == opz2->params.size());
    CHECK(opz2 && opz2->params.count("EGS") == 1);
    const fpe::ChipCaps* vrc7 = caps.chip(fpe::VoicePatchType::VRC7);
    CHECK(vrc7 && vrc7->params.count("ALG_EXT") == 1);

    fpe::HwPatch p;
    p.ops.resize(4);

    // Ranges the editor used to hand-maintain, including the three its own
    // tables had wrong before this file existed (D-057).
    auto range = [&](fpe::VoicePatchType t, const char* name, int op) {
        return caps.resolve(t, name, p, op);
    };
    const auto opnAm = range(fpe::VoicePatchType::OPN, "AM", 0);
    CHECK(opnAm && opnAm->maxV == 1); // was marked "unused" by the old table
    const auto opzRev = range(fpe::VoicePatchType::OPZ, "REV", 0);
    CHECK(opzRev && opzRev->maxV == 7); // old table said 15
    const auto opzEgs = range(fpe::VoicePatchType::OPZ, "EGS", 0);
    CHECK(opzEgs && opzEgs->maxV == 127 && opzEgs->effMax == 3);
    const auto oplAr = range(fpe::VoicePatchType::OPL, "AR", 0);
    CHECK(oplAr && oplAr->maxV == 31 && oplAr->quantum == 2);
    const auto oplTl = range(fpe::VoicePatchType::OPL, "TL", 0);
    CHECK(oplTl && oplTl->effMax == 63);
    const auto ssgTl = range(fpe::VoicePatchType::SSG, "TL", 0);
    CHECK(ssgTl && ssgTl->maxV == 127 && ssgTl->effMax == 63 && ssgTl->quantum == 4);
    const auto epsgWs = range(fpe::VoicePatchType::EPSG, "WS", 0);
    CHECK(epsgWs && epsgWs->maxV == 15 && epsgWs->effMax == 8 && epsgWs->values != nullptr);

    // driver_support == "pending": the chip has the parameter and a patch may
    // legitimately carry a value, but the driver does not act on it yet. It
    // must stay editable and must never be pruned - ../FITOM_staging has 109
    // OPN2-tagged patches with a non-zero PMS.
    const auto opn2Pms = range(fpe::VoicePatchType::OPN2, "PMS", -1);
    CHECK(opn2Pms && opn2Pms->maxV == 7);
    CHECK(opn2Pms && opn2Pms->driverPending());
    CHECK(opn2Pms && opn2Pms->active); // pending is not a condition
    const auto opn2Ams = range(fpe::VoicePatchType::OPN2, "AMS", -1);
    CHECK(opn2Ams && opn2Ams->maxV == 3 && opn2Ams->driverPending());
    // OPN (YM2203) has no hardware LFO at all, so it stays absent there.
    CHECK(!range(fpe::VoicePatchType::OPN, "PMS", -1).has_value());
    // A normally-supported parameter must not be flagged.
    CHECK(!range(fpe::VoicePatchType::OPM, "PMS", -1)->driverPending());

    // condition: SSG's software envelope and its HW envelope period are
    // mutually exclusive, switched by EGT bit3 on the same operator.
    p.ops[0].EGT = 0;
    CHECK(range(fpe::VoicePatchType::SSG, "AR", 0)->active == true);
    CHECK(range(fpe::VoicePatchType::SSG, "HWEP", -1)->active == false);
    p.ops[0].EGT = 8;
    CHECK(range(fpe::VoicePatchType::SSG, "AR", 0)->active == false);
    CHECK(range(fpe::VoicePatchType::SSG, "HWEP", -1)->active == true);
    p.ops[0].EGT = 0;

    // condition on a channel-scope field: SSG's noise period only matters
    // when ALG selects noise.
    p.hw.ALG = 0;
    CHECK(range(fpe::VoicePatchType::SSG, "NFQ", -1)->active == false);
    p.hw.ALG = 2;
    CHECK(range(fpe::VoicePatchType::SSG, "NFQ", -1)->active == true);

    // A `channels`-only condition must NOT gate the control - the patch's own
    // value is what routes it to that channel in the first place.
    const auto opnFix = range(fpe::VoicePatchType::OPN, "FIX", -1);
    CHECK(opnFix && opnFix->active == true && opnFix->values != nullptr);
    CHECK(opnFix && opnFix->channels != nullptr);

    // per_op: OPL3's pseudo-detune exists only on each pair's lead operator.
    CHECK(range(fpe::VoicePatchType::OPL3, "PDT", 0).has_value());
    CHECK(!range(fpe::VoicePatchType::OPL3, "PDT", 1).has_value());
    CHECK(range(fpe::VoicePatchType::OPL3, "PDT", 2).has_value());
    CHECK(!range(fpe::VoicePatchType::OPL3, "PDT", 3).has_value());
    // per_op overrides: OPLL's carrier TL is coarser than its modulator's.
    const auto opllTl0 = range(fpe::VoicePatchType::OPLL, "TL", 0);
    const auto opllTl1 = range(fpe::VoicePatchType::OPLL, "TL", 1);
    CHECK(opllTl0 && opllTl1 && opllTl0->quantum == 1 && opllTl1->quantum == 4);

    // per_condition: EPSG's DR is repurposed as a HW envelope period nibble.
    p.ops[0].EGT = 0;
    const auto epsgDrSw = range(fpe::VoicePatchType::EPSG, "DR", 0);
    CHECK(epsgDrSw && epsgDrSw->effMax == 31);
    p.ops[0].EGT = 8;
    const auto epsgDrHw = range(fpe::VoicePatchType::EPSG, "DR", 0);
    CHECK(epsgDrHw && epsgDrHw->effMax == 15);
    p.ops[0].EGT = 0;

    // operator_count, including OPL_RHY's per-instrument variation (only the
    // bass drum is a 2-operator voice).
    CHECK(caps.operatorCount(fpe::VoicePatchType::OPM, p) == 4);
    CHECK(caps.operatorCount(fpe::VoicePatchType::OPLL, p) == 2);
    p.ext.rhythm_ch = 0;
    CHECK(caps.operatorCount(fpe::VoicePatchType::OPL_RHY, p) == 1);
    p.ext.rhythm_ch = 4;
    CHECK(caps.operatorCount(fpe::VoicePatchType::OPL_RHY, p) == 2);
    p.ext.rhythm_ch = 255;
    CHECK(caps.operatorCount(fpe::VoicePatchType::OPL_RHY, p) == 0); // unset

    // An unknown/reserved type must resolve to nothing rather than throwing.
    CHECK(caps.chip(fpe::VoicePatchType::None) == nullptr);
    CHECK(!caps.resolve(fpe::VoicePatchType::None, "FB", p, -1).has_value());
}

// per_op / per_condition resolve by layering the branch onto the parent, so
// a key the branch omits keeps the parent's value (FITOM_X spec/README.md,
// "分岐する定義"). Exercised on a synthetic file because the real one happens
// to state effective_range on every branch that restates range - the rule
// still has to hold, or a future sync silently widens a slider.
static void testCapabilityBranchInheritance() {
    const fs::path file = fs::temp_directory_path() / "fpe_caps_branch" / "spec" /
                          "chip-capabilities.json";
    fs::create_directories(file.parent_path());
    {
        std::ofstream out(file);
        out << R"({
          "format": "fitom-chip-capabilities",
          "format_version": 1,
          "parameters": { "TL": {"scope":"operator","path":"ops[i].TL","schema_range":[0,127]},
                          "DR": {"scope":"operator","path":"ops[i].DR","schema_range":[0,31]} },
          "chips": [{
            "id":"TESTCHIP","voice_patch_type":16,"patch_kind":"fm","operator_count":2,
            "params": {
              "TL": {"range":[0,127],"effective_range":[0,63],"quantum":2,
                     "per_op":{"0":{},"1":{"range":[0,100]}}},
              "DR": {"range":[0,31],"effective_range":[0,15],
                     "per_condition":[{"condition":{"param":"TL","mask":1,"equals":0},
                                       "quantum":4}]}
            }
          }]
        })";
    }
    fpe::ChipCapabilities caps;
    CHECK(caps.loadFromFile(file));

    fpe::HwPatch p;
    p.ops.resize(2);
    const auto op0 = caps.resolve(fpe::VoicePatchType::OPN, "TL", p, 0);
    CHECK(op0 && op0->maxV == 127 && op0->effMax == 63 && op0->quantum == 2);
    // The branch restates only `range`; effective_range and quantum come from
    // the parent rather than being reset to the branch's range.
    const auto op1 = caps.resolve(fpe::VoicePatchType::OPN, "TL", p, 1);
    CHECK(op1 && op1->maxV == 100);
    CHECK(op1 && op1->effMax == 63);
    CHECK(op1 && op1->quantum == 2);
    // Same layering for per_condition.
    const auto dr = caps.resolve(fpe::VoicePatchType::OPN, "DR", p, 0);
    CHECK(dr && dr->maxV == 31 && dr->effMax == 15 && dr->quantum == 4);

    fs::remove_all(file.parent_path().parent_path());
}

// Saving a bank drops the fields its chip never reads, so the editor's output
// matches the field set FITOM_X's own writer produces (D-057).
static void testHwBankPruning() {
    if (!fpe::ChipCapabilities::instance().loaded()) return;

    fpe::HwBank bank;
    bank.name = "prune";
    bank.voicePatchType = fpe::VoicePatchType::SSG;
    fpe::HwPatch p;
    p.prog = 0;
    p.name = "ssg";
    p.hw.ALG = 2;
    p.hw.NFQ = 9;
    // FB/AMS/PMS/FB2 and the operator's MUL/KSR/WS are all meaningless on
    // SSG and left at their defaults here, which is what makes them
    // droppable - the "non-default values survive anyway" rule has its own
    // case further down.
    p.ops.resize(1);
    p.ops[0].AR = 31;
    p.ops[0].TL = 12;
    p.ext.HWEP = 4096;
    p.ext.target_voice_patch_type = fpe::VoicePatchType::SSG;
    bank.patches.push_back(p);

    const nlohmann::json j = bank;
    const nlohmann::json& pj = j.at("patches").at(0);
    CHECK(pj.contains("ALG") && pj.contains("NFQ"));
    CHECK(!pj.contains("FB"));
    CHECK(!pj.contains("AMS") && !pj.contains("PMS") && !pj.contains("FB2"));
    const nlohmann::json& op = pj.at("ops").at(0);
    CHECK(op.contains("AR") && op.contains("TL") && op.contains("EGT"));
    CHECK(!op.contains("MUL") && !op.contains("KSR") && !op.contains("WS"));
    CHECK(pj.at("ext").contains("HWEP"));
    CHECK(pj.at("ext").contains("target_voice_patch_type"));
    CHECK(!pj.at("ext").contains("rhythm_ch") && !pj.at("ext").contains("FIX"));
    // prog/name/sw_bank/sw_prog are chip-independent and always written.
    CHECK(pj.contains("prog") && pj.contains("name") && pj.contains("sw_bank"));

    // SCC reads no channel-level field and has no hardware envelope, so its
    // "ext" collapses to just the shared-PSG-bank target and the channel
    // keys vanish entirely.
    // The patch's own target has to move too: inside the shared PSG bank
    // namespace it, not the bank tag, decides the chip (see below). The
    // channel fields are cleared first because a non-default value is kept
    // regardless of the chip - see the OPN2 case further down.
    bank.voicePatchType = fpe::VoicePatchType::SCC;
    bank.patches[0].ext.target_voice_patch_type = fpe::VoicePatchType::SCC;
    bank.patches[0].hw = fpe::FmHwVoice{};
    bank.patches[0].ext.HWEP = 0;
    const nlohmann::json sccJson = bank;
    const nlohmann::json& sp = sccJson.at("patches").at(0);
    CHECK(!sp.contains("ALG") && !sp.contains("NFQ") && !sp.contains("FB"));
    CHECK(sp.at("ops").at(0).contains("WS"));
    CHECK(!sp.at("ops").at(0).contains("EGT")); // no HW envelope generator

    // PSG shared bank: pruning must follow the patch's own target chip, not
    // the bank tag, or an EPSG patch parked in an SSG-tagged bank (which is
    // how ../FITOM_staging really ships them) loses the two fields only EPSG
    // uses - FB as the extended noise frequency's high bits, WS as the duty
    // ratio.
    bank.voicePatchType = fpe::VoicePatchType::SSG;
    bank.patches[0].ext.target_voice_patch_type = fpe::VoicePatchType::EPSG;
    bank.patches[0].ext.HWEP = 0;
    bank.patches[0].hw.FB = 3;
    bank.patches[0].ops[0].WS = 4;
    const nlohmann::json epsgJson = bank;
    const nlohmann::json& ep = epsgJson.at("patches").at(0);
    CHECK(ep.contains("FB"));
    CHECK(ep.at("FB").get<int>() == 3);
    CHECK(ep.at("ops").at(0).contains("WS"));
    CHECK(ep.at("ops").at(0).at("WS").get<int>() == 4);
    // EPSG's HW envelope comes from SL/RR/DR/SR, not HWEP - and this patch
    // left HWEP at 0, so nothing is lost by dropping it.
    CHECK(!ep.at("ext").contains("HWEP"));
    CHECK(fpe::effectiveVoicePatchType(fpe::VoicePatchType::SSG, bank.patches[0]) ==
          fpe::VoicePatchType::EPSG);
    // Outside the PSG family the bank tag always wins, even if a stray
    // target_voice_patch_type is set.
    CHECK(fpe::effectiveVoicePatchType(fpe::VoicePatchType::OPM, bank.patches[0]) ==
          fpe::VoicePatchType::OPM);

    // A driver-pending field is a real parameter of the chip, so it is kept
    // whatever its value - including at its default, unlike a field the chip
    // has no notion of.
    fpe::HwBank pend;
    pend.voicePatchType = fpe::VoicePatchType::OPN2;
    fpe::HwPatch pp;
    pp.prog = 0;
    pp.ops.resize(4);
    pp.hw.PMS = 6;
    pend.patches.push_back(pp);
    const nlohmann::json pendJson = pend;
    CHECK(pendJson.at("patches").at(0).at("PMS").get<int>() == 6);
    CHECK(pendJson.at("patches").at(0).contains("AMS")); // kept at its default too

    // Pruning never destroys information: a field the chip has no notion of
    // is dropped only while it still holds its default. A non-default value
    // survives regardless, so an incomplete spec entry can never turn into
    // silent data loss on save.
    fpe::HwBank keep;
    keep.voicePatchType = fpe::VoicePatchType::OPN2;
    fpe::HwPatch kp;
    kp.prog = 0;
    kp.ops.resize(4);
    kp.hw.FB2 = 5;    // not in OPN2's params[], but set
    kp.ops[0].WS = 3; // OPN2 reads no waveform select, but this one is set
    keep.patches.push_back(kp);
    const nlohmann::json keepJson = keep;
    const nlohmann::json& kj = keepJson.at("patches").at(0);
    CHECK(kj.contains("FB2") && kj.at("FB2").get<int>() == 5);
    CHECK(kj.at("ops").at(0).contains("WS"));
    CHECK(!kj.at("ops").at(1).contains("WS")); // still default on the others
    CHECK(!kj.at("ops").at(1).contains("KSL")); // OPN2 has no key-scale level

    // A builtin-reference entry has no hw/ops/ext half to prune at all.
    fpe::HwBank meta;
    meta.voicePatchType = fpe::VoicePatchType::OPLL;
    fpe::HwPatch ref;
    ref.prog = 1;
    ref.builtin = fpe::BuiltinRef{"OPLL", 3};
    meta.patches.push_back(ref);
    const nlohmann::json metaJson = meta;
    CHECK(metaJson.at("patches").at(0).contains("builtin"));
    CHECK(!metaJson.at("patches").at(0).contains("ops"));
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
    testChipCapabilities();
    testCapabilityBranchInheritance();
    testHwBankPruning();
    testBuiltinVoices();
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
