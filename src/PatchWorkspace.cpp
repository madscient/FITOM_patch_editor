#include "fpe/PatchWorkspace.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "fpe/JsonUtil.h"

namespace fpe {

namespace {

template <typename T>
T loadJsonFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw JsonError("cannot open file: " + path.string());
    }
    nlohmann::json j;
    try {
        in >> j;
    } catch (const nlohmann::json::parse_error& e) {
        throw JsonError("JSON parse error in " + path.string() + ": " + e.what());
    }
    return j.get<T>();
}

template <typename T>
void saveJsonFile(const std::filesystem::path& path, const T& value) {
    std::filesystem::create_directories(path.parent_path());
    nlohmann::json j = value;
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw JsonError("cannot write file: " + path.string());
    }
    out << j.dump(2) << '\n';
}

// Loads a *.pcmbank.json. If it has no entries[] of its own but does name
// an adpcm_json (the usual case - see PcmBank.h), follows that reference,
// resolved relative to `path`'s own parent directory (matches FITOM_X's
// PatchManager::loadPcmBankJson(), core/src/PatchManager.cpp: baseDir =
// path.parent_path()). A missing/unparseable adpcm_json does NOT fail the
// whole bank (the pcmbank.json itself is the thing that must exist and
// parse - D-003 "read loosely"), but unlike other soft-fail cases in this
// file it's surfaced as a warning via `warnings` rather than silently
// leaving entries empty, since an unreachable adpcm_json means the bank's
// entire patch list is missing (see docs/DESIGN.md D-013 - this is exactly
// the condition hit by real FITOM_staging data at the time of writing).
PcmBank loadPcmBank(const std::filesystem::path& path, const std::string& contextLabel,
                     std::vector<std::string>& warnings) {
    PcmBank bank = loadJsonFile<PcmBank>(path);
    if (bank.entries.empty() && !bank.adpcm_json.empty()) {
        std::filesystem::path ajPath(bank.adpcm_json);
        if (ajPath.is_relative()) ajPath = path.parent_path() / ajPath;
        std::ifstream aj(ajPath, std::ios::binary);
        if (!aj) {
            warnings.push_back(contextLabel + ": adpcm_json not found: " + ajPath.string());
        } else {
            try {
                nlohmann::json ajJson;
                aj >> ajJson;
                if (ajJson.contains("entries") && ajJson["entries"].is_array()) {
                    for (const auto& e : ajJson["entries"]) bank.entries.push_back(e.get<PcmBankEntry>());
                } else {
                    warnings.push_back(contextLabel + ": adpcm_json has no entries[]: " + ajPath.string());
                }
            } catch (const nlohmann::json::parse_error& e) {
                warnings.push_back(contextLabel + ": adpcm_json parse error: " + ajPath.string() + ": " + e.what());
            }
        }
    }
    return bank;
}

// Copies a PcmBank's sidecar reference (adpcm_json or bin_file, both
// relative-path fields naming a file alongside the pcmbank.json) from its
// old location to its new one, when the pcmbank.json itself is being
// rebased to a new directory (PatchWorkspace::rebaseSourceFiles()). A
// missing source file is a soft-fail (already reflected as a load warning
// if it mattered - see loadPcmBank()); an absolute or otherwise-unresolved
// reference is left untouched (points at a shared external resource that
// isn't part of the profile tree being relocated).
void copyPcmBankSidecar(const std::string& relRef, const std::filesystem::path& oldParent,
                         const std::filesystem::path& newParent) {
    if (relRef.empty()) return;
    std::filesystem::path relPath(relRef);
    std::filesystem::path oldPath = relPath.is_relative() ? oldParent / relPath : relPath;
    std::filesystem::path newPath = relPath.is_relative() ? newParent / relPath : relPath;
    if (oldPath == newPath) return;
    std::error_code ec;
    std::filesystem::create_directories(newPath.parent_path(), ec);
    std::filesystem::copy_file(oldPath, newPath, std::filesystem::copy_options::overwrite_existing, ec);
}

} // namespace

std::filesystem::path PatchWorkspace::resolve(const std::string& relativeFile) const {
    std::filesystem::path p(relativeFile);
    if (p.is_absolute()) return p;
    return rootDir_ / p;
}

void PatchWorkspace::load(const std::filesystem::path& profileJsonPath) {
    profilePath_ = profileJsonPath;
    rootDir_ = profileJsonPath.parent_path();
    warnings_.clear();

    profile_ = loadJsonFile<Profile>(profileJsonPath);
    loadBanks();
}

void PatchWorkspace::loadBanks() {
    patchBanks_.clear();
    swBanks_.clear();
    hwBanks_.clear();
    sampleZoneBanks_.clear();
    pcmBanks_.clear();
    drumKits_.clear();

    for (const auto& ref : profile_.patch_banks) {
        try {
            PatchBank bank = loadJsonFile<PatchBank>(resolve(ref.file));
            bank.bankIndex = ref.bank;
            bank.sourceFile = resolve(ref.file);
            patchBanks_.push_back(std::move(bank));
        } catch (const std::exception& e) {
            warnings_.push_back(std::string("patch_banks[bank=") + std::to_string(ref.bank) + "]: " + e.what());
        }
    }

    for (const auto& ref : profile_.sw_banks) {
        try {
            SwBank bank = loadJsonFile<SwBank>(resolve(ref.file));
            bank.bankIndex = ref.bank;
            bank.sourceFile = resolve(ref.file);
            swBanks_.push_back(std::move(bank));
        } catch (const std::exception& e) {
            warnings_.push_back(std::string("sw_banks[bank=") + std::to_string(ref.bank) + "]: " + e.what());
        }
    }

    for (const auto& ref : profile_.hw_banks) {
        auto typeOpt = stringToVoicePatchType(ref.group);
        if (!typeOpt) {
            warnings_.push_back("hw_banks[group=\"" + ref.group + "\"]: unrecognized group name");
            continue;
        }
        try {
            if (isSampleBasedVoicePatchType(*typeOpt)) {
                SampleZoneBank bank = loadJsonFile<SampleZoneBank>(resolve(ref.file));
                bank.voicePatchType = *typeOpt;
                bank.bankIndex = ref.bank;
                bank.sourceFile = resolve(ref.file);
                sampleZoneBanks_.push_back(std::move(bank));
            } else if (isPcmWaveformVoicePatchType(*typeOpt)) {
                const std::string label = "hw_banks[group=\"" + ref.group + "\", bank=" +
                                           std::to_string(ref.bank) + "]";
                PcmBank bank = loadPcmBank(resolve(ref.file), label, warnings_);
                bank.voicePatchType = *typeOpt;
                bank.bankIndex = ref.bank;
                bank.sourceFile = resolve(ref.file);
                pcmBanks_.push_back(std::move(bank));
            } else {
                HwBank bank = loadJsonFile<HwBank>(resolve(ref.file));
                bank.voicePatchType = *typeOpt;
                bank.bankIndex = ref.bank;
                bank.role = ref.role;
                bank.sourceFile = resolve(ref.file);
                hwBanks_.push_back(std::move(bank));
            }
        } catch (const std::exception& e) {
            warnings_.push_back(std::string("hw_banks[group=\"") + ref.group + "\", bank=" +
                                 std::to_string(ref.bank) + "]: " + e.what());
        }
    }

    // banks.pcm_banks[]: an alternate registration path for the same
    // *.pcmbank.json shape (no `group` tag, so voicePatchType stays None -
    // see fpe::PcmBankRef). Not currently used by any real profile we've
    // seen (hw_banks[group=ADPCM*] is used instead - D-013), but the ref
    // itself has been round-tripped since D-008, so load it the same way.
    for (const auto& ref : profile_.pcm_banks) {
        try {
            const std::string label = "pcm_banks[bank=" + std::to_string(ref.bank) + "]";
            PcmBank bank = loadPcmBank(resolve(ref.file), label, warnings_);
            bank.bankIndex = ref.bank;
            bank.sourceFile = resolve(ref.file);
            pcmBanks_.push_back(std::move(bank));
        } catch (const std::exception& e) {
            warnings_.push_back(std::string("pcm_banks[bank=") + std::to_string(ref.bank) + "]: " + e.what());
        }
    }

    for (const auto& ref : profile_.drum_banks) {
        try {
            DrumKit kit = loadJsonFile<DrumKit>(resolve(ref.file));
            kit.prog = ref.prog;
            if (kit.name.empty()) kit.name = ref.name;
            kit.sourceFile = resolve(ref.file);
            drumKits_.push_back(std::move(kit));
        } catch (const std::exception& e) {
            warnings_.push_back(std::string("drum_banks[prog=") + std::to_string(ref.prog) + "]: " + e.what());
        }
    }
}

void PatchWorkspace::save() {
    if (profilePath_.empty()) {
        throw JsonError("PatchWorkspace::save() called with no path set - use saveAs() first");
    }
    saveJsonFile(profilePath_, profile_);
    for (auto& b : patchBanks_) saveJsonFile(b.sourceFile, b);
    for (auto& b : swBanks_) saveJsonFile(b.sourceFile, b);
    for (auto& b : hwBanks_) saveJsonFile(b.sourceFile, b);
    for (auto& b : sampleZoneBanks_) saveJsonFile(b.sourceFile, b);
    // Re-serializes each pcmbank.json's own top-level fields; entries[]
    // read from a separate adpcm_json are NOT duplicated in here (PcmBank's
    // to_json omits them whenever adpcm_json is set) - copying that sidecar
    // file itself alongside is rebaseSourceFiles()'s job (saveAs() calls it
    // before save()), since it needs both the old and new sourceFile.
    for (auto& b : pcmBanks_) saveJsonFile(b.sourceFile, b);
    for (auto& k : drumKits_) saveJsonFile(k.sourceFile, k);
}

void PatchWorkspace::rebaseSourceFiles(const std::filesystem::path& newRoot) {
    auto rebase = [&](std::filesystem::path& sourceFile) {
        if (rootDir_.empty()) return;
        std::error_code ec;
        auto rel = std::filesystem::relative(sourceFile, rootDir_, ec);
        if (ec) return; // leave as-is (e.g. an absolute path outside rootDir_)
        sourceFile = newRoot / rel;
    };
    for (auto& b : patchBanks_) rebase(b.sourceFile);
    for (auto& b : swBanks_) rebase(b.sourceFile);
    for (auto& b : hwBanks_) rebase(b.sourceFile);
    for (auto& b : sampleZoneBanks_) rebase(b.sourceFile);
    for (auto& b : pcmBanks_) {
        const std::filesystem::path oldSourceFile = b.sourceFile;
        rebase(b.sourceFile);
        if (oldSourceFile.empty() || oldSourceFile == b.sourceFile) continue;
        copyPcmBankSidecar(b.adpcm_json, oldSourceFile.parent_path(), b.sourceFile.parent_path());
        copyPcmBankSidecar(b.bin_file, oldSourceFile.parent_path(), b.sourceFile.parent_path());
    }
    for (auto& k : drumKits_) rebase(k.sourceFile);
}

void PatchWorkspace::saveAs(const std::filesystem::path& profileJsonPath) {
    const std::filesystem::path newRoot = profileJsonPath.parent_path();
    rebaseSourceFiles(newRoot);
    profilePath_ = profileJsonPath;
    rootDir_ = newRoot;
    save();
}

void PatchWorkspace::createNew(const std::filesystem::path& dir, const std::string& profileName) {
    rootDir_ = dir;
    profilePath_.clear();
    profile_ = Profile{};
    profile_.profile_name = profileName;
    patchBanks_.clear();
    swBanks_.clear();
    hwBanks_.clear();
    sampleZoneBanks_.clear();
    pcmBanks_.clear();
    drumKits_.clear();
    warnings_.clear();
}

PatchBank* PatchWorkspace::findNativePatchBank(int bank) {
    for (auto& b : patchBanks_) if (b.bankIndex == bank) return &b;
    return nullptr;
}
SwBank* PatchWorkspace::findPerformanceBank(int bank) {
    for (auto& b : swBanks_) if (b.bankIndex == bank) return &b;
    return nullptr;
}
HwBank* PatchWorkspace::findDeviceBank(VoicePatchType type, int bank) {
    for (auto& b : hwBanks_) if (b.voicePatchType == type && b.bankIndex == bank) return &b;
    return nullptr;
}
SampleZoneBank* PatchWorkspace::findSampleZoneBank(VoicePatchType type, int bank) {
    for (auto& b : sampleZoneBanks_) if (b.voicePatchType == type && b.bankIndex == bank) return &b;
    return nullptr;
}
PcmBank* PatchWorkspace::findPcmBank(VoicePatchType type, int bank) {
    for (auto& b : pcmBanks_) if (b.voicePatchType == type && b.bankIndex == bank) return &b;
    return nullptr;
}
DrumKit* PatchWorkspace::findDrumKit(int prog) {
    for (auto& k : drumKits_) if (k.prog == prog) return &k;
    return nullptr;
}

SwPatch* PatchWorkspace::resolvePerformancePatch(int swBank, int swProg) {
    if (swBank < 0 || swProg < 0) return nullptr;
    auto* bank = findPerformanceBank(swBank);
    if (!bank) return nullptr;
    return bank->findByProg(swProg);
}

// --- native patch banks / patches -----------------------------------------

PatchBank& PatchWorkspace::createNativePatchBank(int bankIndex, const std::string& name,
                                                  const std::string& relativeFile) {
    PatchBank bank;
    bank.name = name;
    bank.bankIndex = bankIndex;
    bank.sourceFile = resolve(relativeFile);
    patchBanks_.push_back(std::move(bank));

    PatchBankRef ref;
    ref.bank = bankIndex;
    ref.file = relativeFile;
    ref.name = name;
    profile_.patch_banks.push_back(ref);

    return patchBanks_.back();
}

bool PatchWorkspace::deleteNativePatchBank(int bankIndex) {
    auto it = std::remove_if(patchBanks_.begin(), patchBanks_.end(),
                              [&](const PatchBank& b) { return b.bankIndex == bankIndex; });
    bool removed = it != patchBanks_.end();
    patchBanks_.erase(it, patchBanks_.end());

    auto rit = std::remove_if(profile_.patch_banks.begin(), profile_.patch_banks.end(),
                               [&](const PatchBankRef& r) { return r.bank == bankIndex; });
    profile_.patch_banks.erase(rit, profile_.patch_banks.end());
    return removed;
}

PatchBank* PatchWorkspace::duplicateNativePatchBank(int fromBank, int toBank,
                                                     const std::string& newRelativeFile) {
    auto* src = findNativePatchBank(fromBank);
    if (!src) return nullptr;
    PatchBank copy = *src;
    copy.bankIndex = toBank;
    copy.sourceFile = resolve(newRelativeFile);
    patchBanks_.push_back(std::move(copy));

    PatchBankRef ref;
    ref.bank = toBank;
    ref.file = newRelativeFile;
    ref.name = src->name;
    profile_.patch_banks.push_back(ref);

    return &patchBanks_.back();
}

Patch& PatchWorkspace::createPatch(PatchBank& bank, int prog, const std::string& name) {
    Patch p;
    p.prog = prog;
    p.name = name;
    bank.patches.push_back(std::move(p));
    return bank.patches.back();
}

bool PatchWorkspace::deletePatch(PatchBank& bank, int prog) {
    auto it = std::remove_if(bank.patches.begin(), bank.patches.end(),
                              [&](const Patch& p) { return p.prog == prog; });
    bool removed = it != bank.patches.end();
    bank.patches.erase(it, bank.patches.end());
    return removed;
}

Patch* PatchWorkspace::duplicatePatch(PatchBank& bank, int fromProg, int toProg) {
    auto* src = bank.findByProg(fromProg);
    if (!src) return nullptr;
    Patch copy = *src;
    copy.prog = toProg;
    bank.patches.push_back(std::move(copy));
    return &bank.patches.back();
}

// --- performance banks / patches -------------------------------------------

SwBank& PatchWorkspace::createPerformanceBank(int bankIndex, const std::string& name,
                                               const std::string& relativeFile) {
    SwBank bank;
    bank.name = name;
    bank.bankIndex = bankIndex;
    bank.sourceFile = resolve(relativeFile);
    swBanks_.push_back(std::move(bank));

    SwBankRef ref;
    ref.bank = bankIndex;
    ref.file = relativeFile;
    ref.name = name;
    profile_.sw_banks.push_back(ref);

    return swBanks_.back();
}

bool PatchWorkspace::deletePerformanceBank(int bankIndex) {
    auto it = std::remove_if(swBanks_.begin(), swBanks_.end(),
                              [&](const SwBank& b) { return b.bankIndex == bankIndex; });
    bool removed = it != swBanks_.end();
    swBanks_.erase(it, swBanks_.end());

    auto rit = std::remove_if(profile_.sw_banks.begin(), profile_.sw_banks.end(),
                               [&](const SwBankRef& r) { return r.bank == bankIndex; });
    profile_.sw_banks.erase(rit, profile_.sw_banks.end());
    return removed;
}

SwPatch& PatchWorkspace::createPerformancePatch(SwBank& bank, int prog, const std::string& name) {
    SwPatch p;
    p.prog = prog;
    p.name = name;
    bank.patches.push_back(std::move(p));
    return bank.patches.back();
}

bool PatchWorkspace::deletePerformancePatch(SwBank& bank, int prog) {
    auto it = std::remove_if(bank.patches.begin(), bank.patches.end(),
                              [&](const SwPatch& p) { return p.prog == prog; });
    bool removed = it != bank.patches.end();
    bank.patches.erase(it, bank.patches.end());
    return removed;
}

SwPatch* PatchWorkspace::duplicatePerformancePatch(SwBank& bank, int fromProg, int toProg) {
    auto* src = bank.findByProg(fromProg);
    if (!src) return nullptr;
    SwPatch copy = *src;
    copy.prog = toProg;
    bank.patches.push_back(std::move(copy));
    return &bank.patches.back();
}

// --- device patch banks / voice patches -------------------------------------

HwBank& PatchWorkspace::createDeviceBank(VoicePatchType type, int bankIndex, const std::string& name,
                                          const std::string& relativeFile) {
    HwBank bank;
    bank.name = name;
    bank.voicePatchType = type;
    bank.bankIndex = bankIndex;
    bank.sourceFile = resolve(relativeFile);
    hwBanks_.push_back(std::move(bank));

    HwBankRef ref;
    ref.group = voicePatchTypeToString(type);
    ref.bank = bankIndex;
    ref.file = relativeFile;
    profile_.hw_banks.push_back(ref);

    return hwBanks_.back();
}

bool PatchWorkspace::deleteDeviceBank(VoicePatchType type, int bankIndex) {
    auto it = std::remove_if(hwBanks_.begin(), hwBanks_.end(), [&](const HwBank& b) {
        return b.voicePatchType == type && b.bankIndex == bankIndex;
    });
    bool removed = it != hwBanks_.end();
    hwBanks_.erase(it, hwBanks_.end());

    const std::string groupStr = voicePatchTypeToString(type);
    auto rit = std::remove_if(profile_.hw_banks.begin(), profile_.hw_banks.end(), [&](const HwBankRef& r) {
        return r.group == groupStr && r.bank == bankIndex;
    });
    profile_.hw_banks.erase(rit, profile_.hw_banks.end());
    return removed;
}

HwPatch& PatchWorkspace::createDeviceVoicePatch(HwBank& bank, int prog, const std::string& name) {
    HwPatch p;
    p.prog = prog;
    p.name = name;
    bank.patches.push_back(std::move(p));
    return bank.patches.back();
}

bool PatchWorkspace::deleteDeviceVoicePatch(HwBank& bank, int prog) {
    auto it = std::remove_if(bank.patches.begin(), bank.patches.end(),
                              [&](const HwPatch& p) { return p.prog == prog; });
    bool removed = it != bank.patches.end();
    bank.patches.erase(it, bank.patches.end());
    return removed;
}

HwPatch* PatchWorkspace::duplicateDeviceVoicePatch(HwBank& bank, int fromProg, int toProg) {
    auto* src = bank.findByProg(fromProg);
    if (!src) return nullptr;
    HwPatch copy = *src;
    copy.prog = toProg;
    bank.patches.push_back(std::move(copy));
    return &bank.patches.back();
}

// --- drum kit map / kits / notes --------------------------------------------

DrumKit& PatchWorkspace::createDrumKit(int prog, const std::string& name, const std::string& relativeFile,
                                        DrumKitType type) {
    DrumKit kit;
    kit.type = type;
    kit.name = name;
    kit.prog = prog;
    kit.sourceFile = resolve(relativeFile);
    drumKits_.push_back(std::move(kit));

    DrumBankRef ref;
    ref.prog = prog;
    ref.name = name;
    ref.file = relativeFile;
    profile_.drum_banks.push_back(ref);

    return drumKits_.back();
}

bool PatchWorkspace::deleteDrumKit(int prog) {
    auto it = std::remove_if(drumKits_.begin(), drumKits_.end(),
                              [&](const DrumKit& k) { return k.prog == prog; });
    bool removed = it != drumKits_.end();
    drumKits_.erase(it, drumKits_.end());

    auto rit = std::remove_if(profile_.drum_banks.begin(), profile_.drum_banks.end(),
                               [&](const DrumBankRef& r) { return r.prog == prog; });
    profile_.drum_banks.erase(rit, profile_.drum_banks.end());
    return removed;
}

DrumKit* PatchWorkspace::duplicateDrumKit(int fromProg, int toProg, const std::string& newRelativeFile) {
    auto* src = findDrumKit(fromProg);
    if (!src) return nullptr;
    DrumKit copy = *src;
    copy.prog = toProg;
    copy.sourceFile = resolve(newRelativeFile);
    drumKits_.push_back(std::move(copy));

    DrumBankRef ref;
    ref.prog = toProg;
    ref.name = src->name;
    ref.file = newRelativeFile;
    profile_.drum_banks.push_back(ref);

    return &drumKits_.back();
}

void PatchWorkspace::upsertDrumNote(DrumKit& kit, const DrumNote& note) {
    if (auto* existing = kit.findNote(note.note)) {
        *existing = note;
    } else {
        kit.notes.push_back(note);
    }
}

bool PatchWorkspace::deleteDrumNote(DrumKit& kit, uint8_t note) {
    auto it = std::remove_if(kit.notes.begin(), kit.notes.end(),
                              [&](const DrumNote& n) { return n.note == note; });
    bool removed = it != kit.notes.end();
    kit.notes.erase(it, kit.notes.end());
    return removed;
}
} // namespace fpe
