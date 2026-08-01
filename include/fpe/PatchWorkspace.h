#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fpe/DrumKit.h"
#include "fpe/HwPatch.h"
#include "fpe/LayeredPatch.h"
#include "fpe/PcmBank.h"
#include "fpe/Profile.h"
#include "fpe/SampleZone.h"
#include "fpe/SwPatch.h"

// PatchWorkspace: loads a *.profile.json plus every bank/kit file it
// references, and exposes the whole thing as the browse/edit hierarchy
// requested for the patch editor:
//
//   パッチプロファイル (Profile)
//     + レイヤードパッチバンク (PatchBank)         -> layeredPatchBanks()
//     |   + レイヤードパッチ (Patch)
//     |       + トーンレイヤー0..3 (ToneLayer)
//     + パフォーマンスバンク (SwBank)                -> performanceBanks()
//     |   + パフォーマンスパッチ (SwPatch)
//     + デバイスパッチバンク (HwBank)                -> deviceBanks()
//     |   + デバイスボイスパッチ (HwPatch)
//     |       + [sw_bank/sw_prog reference into performanceBanks()]
//     + サンプルゾーンバンク (SampleZoneBank, AWMのみ) -> sampleZoneBanks()
//     |   + サンプルゾーンパッチ (SampleZonePatch)
//     + PCM波形バンク (PcmBank, ADPCM-B/A・PCM-D8)     -> pcmBanks()
//     |   + PCMエントリ (PcmBankEntry, indexがpatch_prog)
//     + ドラムキットマップ (Profile::drum_banks)      -> drumKits()
//         + ドラムキット (DrumKit)
//             + ドラムノート (DrumNote)
//
// This class is offline-only (pure JSON load/edit/save); it does not talk
// to a running FITOM_X instance. See README.md for scope.

namespace fpe {

class PatchWorkspace {
public:
    // Loads profile.json and every bank/kit file it references. Relative
    // `file` paths in the profile are resolved relative to the profile
    // file's own parent directory. Malformed top-level profile JSON
    // throws JsonError; a broken/missing individual bank file is recorded
    // in warnings() and skipped, so the rest of the profile stays usable.
    void load(const std::filesystem::path& profileJsonPath);

    // Writes profile.json and every loaded bank/kit back to their
    // sourceFile path - but only files whose in-memory content actually
    // differs from what was last loaded from (or written to) disk, so
    // pressing "save" doesn't rewrite the whole reference tree (which, for
    // a profile built on a shared "banks" bankset, can be dozens of files
    // belonging to other profiles too) just because one patch changed. See
    // originalContent_'s comment.
    void save();
    void saveAs(const std::filesystem::path& profileJsonPath);

    // Starts a brand-new, empty profile rooted at `dir` (not yet written
    // to disk - call saveAs() or save() after).
    void createNew(const std::filesystem::path& dir, const std::string& profileName);

    Profile& profile() { return profile_; }
    const Profile& profile() const { return profile_; }
    const std::filesystem::path& rootDir() const { return rootDir_; }
    const std::vector<std::string>& warnings() const { return warnings_; }

    // --- Browse tree ---
    std::vector<PatchBank>& layeredPatchBanks() { return patchBanks_; }
    std::vector<SwBank>& performanceBanks() { return swBanks_; }
    std::vector<HwBank>& deviceBanks() { return hwBanks_; }
    std::vector<SampleZoneBank>& sampleZoneBanks() { return sampleZoneBanks_; }
    std::vector<PcmBank>& pcmBanks() { return pcmBanks_; }
    std::vector<DrumKit>& drumKits() { return drumKits_; }

    PatchBank* findLayeredPatchBank(int bank);
    SwBank* findPerformanceBank(int bank);
    HwBank* findDeviceBank(VoicePatchType type, int bank);
    SampleZoneBank* findSampleZoneBank(VoicePatchType type, int bank);
    PcmBank* findPcmBank(VoicePatchType type, int bank);
    DrumKit* findDrumKit(int prog);

    // Reference-following helper for the "device voice patch -> performance
    // patch" and "layered patch -> performance patch" tree edges.
    SwPatch* resolvePerformancePatch(int swBank, int swProg);

    // --- CRUD: layered patch banks / patches ---
    PatchBank& createLayeredPatchBank(int bankIndex, const std::string& name, const std::string& relativeFile);
    bool deleteLayeredPatchBank(int bankIndex);
    PatchBank* duplicateLayeredPatchBank(int fromBank, int toBank, const std::string& newRelativeFile);

    Patch& createPatch(PatchBank& bank, int prog, const std::string& name);
    bool deletePatch(PatchBank& bank, int prog);
    Patch* duplicatePatch(PatchBank& bank, int fromProg, int toProg);

    // --- CRUD: performance banks / patches ---
    SwBank& createPerformanceBank(int bankIndex, const std::string& name, const std::string& relativeFile);
    bool deletePerformanceBank(int bankIndex);
    SwPatch& createPerformancePatch(SwBank& bank, int prog, const std::string& name);
    bool deletePerformancePatch(SwBank& bank, int prog);
    SwPatch* duplicatePerformancePatch(SwBank& bank, int fromProg, int toProg);

    // --- CRUD: device patch banks / voice patches ---
    HwBank& createDeviceBank(VoicePatchType type, int bankIndex, const std::string& name, const std::string& relativeFile);
    bool deleteDeviceBank(VoicePatchType type, int bankIndex);
    HwPatch& createDeviceVoicePatch(HwBank& bank, int prog, const std::string& name);
    bool deleteDeviceVoicePatch(HwBank& bank, int prog);
    HwPatch* duplicateDeviceVoicePatch(HwBank& bank, int fromProg, int toProg);

    // --- CRUD: drum kit map / kits / notes ---
    DrumKit& createDrumKit(int prog, const std::string& name, const std::string& relativeFile,
                            DrumKitType type = DrumKitType::Routed);
    bool deleteDrumKit(int prog);
    DrumKit* duplicateDrumKit(int fromProg, int toProg, const std::string& newRelativeFile);
    void upsertDrumNote(DrumKit& kit, const DrumNote& note); // insert or replace by note number
    bool deleteDrumNote(DrumKit& kit, uint8_t note);

private:
    std::filesystem::path rootDir_;
    std::filesystem::path profilePath_;
    Profile profile_;

    std::vector<PatchBank> patchBanks_;
    std::vector<SwBank> swBanks_;
    std::vector<HwBank> hwBanks_;
    std::vector<SampleZoneBank> sampleZoneBanks_;
    std::vector<PcmBank> pcmBanks_;
    std::vector<DrumKit> drumKits_;

    std::vector<std::string> warnings_;

    // Snapshot of each loaded file's content (profile.json, every bank/kit,
    // and bank_overrides' own file if it's external), taken right after
    // load()/save() last touched it - keyed by resolved sourceFile path.
    // save() compares each candidate write against this before actually
    // writing, so an unedited file (e.g. one only reached transitively
    // through a shared "banks" bankset) is left untouched rather than
    // rewritten byte-for-byte-different (this project's "write explicitly"
    // JSON philosophy re-emits the full canonical field set every time,
    // which would otherwise turn every save() into a mass rewrite of the
    // entire reference tree - reported as a bug, see docs/DESIGN.md D-042).
    // A path with no entry here (freshly created this session, or a stale
    // pre-rebase path after saveAs()) is always considered dirty.
    std::map<std::filesystem::path, nlohmann::json> originalContent_;
    void captureOriginalContent();

    void loadBanks();
    // Loads `src.externalFile` (if non-empty) into `src.data`, resolved
    // relative to rootDir_; records a warning and leaves `src.data` empty
    // on failure rather than throwing (matches this library's soft-fail
    // philosophy for referenced-but-broken files - see load()'s doc
    // comment). No-op if `src` wasn't an external reference. D-041.
    void resolveBanksSource(BanksSource& src);
    // Rebuilds profile_.banks/profile_.bank_overrides from the current
    // (possibly-edited) effective registry (profile_.hw_banks etc) ahead of
    // writing profile.json. If "banks" was never an external reference,
    // this just mirrors the effective registry into it (pre-D-041
    // behavior, unchanged). If it was, "banks" itself is left untouched
    // (never rewritten - it may be shared by other profiles) and the diff
    // against the loaded base is written into "bank_overrides" instead.
    // D-041.
    void syncBanksSourceForSave();
    std::filesystem::path resolve(const std::string& relativeFile) const;
    // Rebases every loaded bank's sourceFile onto newRoot, preserving each
    // file's path relative to the current rootDir_. Used by saveAs() so
    // "save as" copies the whole profile tree (all banks/kits), not just
    // the top-level profile.json.
    void rebaseSourceFiles(const std::filesystem::path& newRoot);
};

} // namespace fpe
