#include "fpe/PatchWorkspace.h"

#include <algorithm>
#include <fstream>
#include <set>
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

// Like saveJsonFile(), but skips the write (and the baseline update) when
// `value`'s current JSON is structurally identical to `baseline[path]` -
// see PatchWorkspace::originalContent_'s comment for why this matters
// (D-042). `original` (the real originalContent_, updated in place so the
// *next* save() call compares against what's now actually on disk) and
// `baseline` (a frozen copy taken once at the top of THIS save() call,
// see below) are deliberately two separate maps, not one - and `written`
// records which paths this call has already committed to disk.
//
// Both exist to fix a real data-loss bug (reported live: a DrumNote edit
// reached FITOM_X's in-memory copy correctly via the D-049 bank-override
// SysEx, which reads the in-memory object directly, but the *file* on disk
// still had the pre-edit value afterward). Root cause: multiple in-memory
// objects can legitimately share the same sourceFile - confirmed happening
// for real DrumKit data via D-041's shared "banks" bankset + a profile's
// own bank_overrides both registering the same physical *.drumkit.json
// under two different progs, so ws.drumKits() ends up with two separate
// fpe::DrumKit objects, both loaded from the one file. If `original` were
// mutated (and compared against) live during a single save() pass the way
// it used to be, the sequence "edited sibling writes first, updating the
// live baseline to its edited content" followed by "unedited sibling
// compares its own (still pre-edit) content against that now-edited
// baseline, sees a mismatch, and 'helpfully' rewrites the file back to its
// own stale content" would silently discard the first sibling's just-saved
// edit. Comparing against a `baseline` that's frozen for the whole pass,
// plus letting only the first dirty writer for a given path actually write
// (via `written`), closes that: the unedited sibling now correctly matches
// the frozen baseline and is left alone.
template <typename T>
void saveIfDirty(const std::map<std::filesystem::path, nlohmann::json>& baseline,
                  std::map<std::filesystem::path, nlohmann::json>& original,
                  std::set<std::filesystem::path>& written, const std::filesystem::path& path, const T& value) {
    nlohmann::json cur = value;
    auto it = baseline.find(path);
    if (it != baseline.end() && it->second == cur) return; // matches the true pre-save content - nothing to write
    if (!written.insert(path).second) return; // an earlier sibling for this path already wrote this round - first dirty writer wins (rare same-file-diverged-edits case; better than the alternative of letting a later writer silently clobber the first)
    saveJsonFile(path, value);
    original[path] = std::move(cur);
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

// --- "banks" / "bank_overrides" merge (D-041) --------------------------
//
// profile.schema.json's bank_overrides description: entries are matched
// against the base by an array-specific identity key; a match replaces,
// no match appends, and deletion cannot be expressed at all.
bool sameKey(const HwBankRef& a, const HwBankRef& b) { return a.group == b.group && a.bank == b.bank; }
bool sameKey(const PatchBankRef& a, const PatchBankRef& b) { return a.bank == b.bank; }
bool sameKey(const SwBankRef& a, const SwBankRef& b) { return a.bank == b.bank; }
bool sameKey(const DrumBankRef& a, const DrumBankRef& b) { return a.prog == b.prog; }
bool sameKey(const SccWaveBankRef& a, const SccWaveBankRef& b) { return a.bank == b.bank; }
bool sameKey(const PcmBankRef& a, const PcmBankRef& b) { return a.bank == b.bank && a.chip == b.chip; }

template <typename T>
std::vector<T> mergeByKey(const std::vector<T>& base, const std::vector<T>& overrides) {
    std::vector<T> result = base;
    for (const auto& ov : overrides) {
        auto it = std::find_if(result.begin(), result.end(), [&](const T& b) { return sameKey(b, ov); });
        if (it != result.end()) {
            *it = ov;
        } else {
            result.push_back(ov);
        }
    }
    return result;
}

BanksObject mergeBanksObjects(const BanksObject& base, const BanksObject& overrides) {
    BanksObject out;
    out.hw_banks = mergeByKey(base.hw_banks, overrides.hw_banks);
    out.patch_banks = mergeByKey(base.patch_banks, overrides.patch_banks);
    out.sw_banks = mergeByKey(base.sw_banks, overrides.sw_banks);
    out.drum_banks = mergeByKey(base.drum_banks, overrides.drum_banks);
    out.scc_wave_banks = mergeByKey(base.scc_wave_banks, overrides.scc_wave_banks);
    out.pcm_banks = mergeByKey(base.pcm_banks, overrides.pcm_banks);
    out.sf2_banks = base.sf2_banks; // never mutated by this editor - see BanksObject's comment
    return out;
}

// The save-time inverse of mergeByKey(): everything in `effective` that
// isn't identical to what `base` already says for the same key becomes an
// override entry (new key -> append, changed value -> replace, matches
// mergeByKey()'s own semantics so a round-trip through save+load is
// idempotent). A base entry that vanished from `effective` (deleted this
// session) can't be expressed here at all - flagged via `warnings` since it
// will silently reappear on the next load.
template <typename T>
std::vector<T> diffAgainstBase(const std::vector<T>& base, const std::vector<T>& effective, const char* label,
                                std::vector<std::string>& warnings) {
    std::vector<T> result;
    for (const auto& cur : effective) {
        auto it = std::find_if(base.begin(), base.end(), [&](const T& b) { return sameKey(b, cur); });
        if (it == base.end() || !(nlohmann::json(*it) == nlohmann::json(cur))) {
            result.push_back(cur);
        }
    }
    for (const auto& b : base) {
        bool stillPresent = std::any_of(effective.begin(), effective.end(),
                                         [&](const T& cur) { return sameKey(cur, b); });
        if (!stillPresent) {
            warnings.push_back(std::string(label) +
                                ": a shared-bankset (banks) entry was removed locally, but bank_overrides "
                                "cannot express a deletion - it will reappear after the next reload");
        }
    }
    return result;
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

    // D-041: resolve "banks"/"bank_overrides" (either may be an external
    // file reference) and merge them into the effective registry that the
    // rest of this class (loadBanks(), the CRUD methods, findXxx()) reads
    // and mutates exactly as it did before D-041 existed.
    resolveBanksSource(profile_.banks);
    resolveBanksSource(profile_.bank_overrides);
    BanksObject merged = mergeBanksObjects(profile_.banks.data, profile_.bank_overrides.data);
    profile_.hw_banks = std::move(merged.hw_banks);
    profile_.patch_banks = std::move(merged.patch_banks);
    profile_.sw_banks = std::move(merged.sw_banks);
    profile_.drum_banks = std::move(merged.drum_banks);
    profile_.scc_wave_banks = std::move(merged.scc_wave_banks);
    profile_.pcm_banks = std::move(merged.pcm_banks);

    loadBanks();
    captureOriginalContent();
}

// Establishes a fresh originalContent_ baseline from what's currently
// loaded (D-042, see originalContent_'s comment). Called once by load();
// save() itself keeps the baseline current incrementally via
// saveIfDirty()'s own `original[path] = ...` as each file is written, so
// this doesn't need to run again after every save().
void PatchWorkspace::captureOriginalContent() {
    originalContent_.clear();
    originalContent_[profilePath_] = profile_;
    if (!profile_.bank_overrides.externalFile.empty()) {
        originalContent_[resolve(profile_.bank_overrides.externalFile)] = profile_.bank_overrides.data;
    }
    for (auto& b : patchBanks_) originalContent_[b.sourceFile] = b;
    for (auto& b : swBanks_) originalContent_[b.sourceFile] = b;
    for (auto& b : hwBanks_) originalContent_[b.sourceFile] = b;
    for (auto& b : sampleZoneBanks_) originalContent_[b.sourceFile] = b;
    for (auto& b : pcmBanks_) originalContent_[b.sourceFile] = b;
    for (auto& k : drumKits_) originalContent_[k.sourceFile] = k;
}

void PatchWorkspace::resolveBanksSource(BanksSource& src) {
    if (!src.present || src.externalFile.empty()) return;
    try {
        src.data = loadJsonFile<BanksObject>(resolve(src.externalFile));
    } catch (const std::exception& e) {
        warnings_.push_back("banks(\"" + src.externalFile + "\"): " + e.what());
    }
}

void PatchWorkspace::syncBanksSourceForSave() {
    if (profile_.banks.externalFile.empty()) {
        // "banks" was never an external reference (absent, or an inline
        // object already - pre-D-041 behavior): just mirror the current
        // effective registry straight back into it. Any (real-world
        // unseen) inline "bank_overrides" is left exactly as loaded.
        profile_.banks.present = true;
        profile_.banks.data.hw_banks = profile_.hw_banks;
        profile_.banks.data.patch_banks = profile_.patch_banks;
        profile_.banks.data.sw_banks = profile_.sw_banks;
        profile_.banks.data.drum_banks = profile_.drum_banks;
        profile_.banks.data.scc_wave_banks = profile_.scc_wave_banks;
        profile_.banks.data.pcm_banks = profile_.pcm_banks;
        return;
    }

    // "banks" is an external reference (e.g. a shared unified.bankset.json)
    // - it is NEVER rewritten, so other profiles pointing at the same file
    // aren't affected by edits made through this one. Diff the effective
    // registry against the base as loaded; anything new or changed becomes
    // a bank_overrides entry instead.
    BanksObject ov;
    ov.hw_banks = diffAgainstBase(profile_.banks.data.hw_banks, profile_.hw_banks, "hw_banks", warnings_);
    ov.patch_banks = diffAgainstBase(profile_.banks.data.patch_banks, profile_.patch_banks, "patch_banks", warnings_);
    ov.sw_banks = diffAgainstBase(profile_.banks.data.sw_banks, profile_.sw_banks, "sw_banks", warnings_);
    ov.drum_banks = diffAgainstBase(profile_.banks.data.drum_banks, profile_.drum_banks, "drum_banks", warnings_);
    ov.scc_wave_banks =
        diffAgainstBase(profile_.banks.data.scc_wave_banks, profile_.scc_wave_banks, "scc_wave_banks", warnings_);
    ov.pcm_banks = diffAgainstBase(profile_.banks.data.pcm_banks, profile_.pcm_banks, "pcm_banks", warnings_);

    profile_.bank_overrides.data = std::move(ov);
    // Keep a "bank_overrides": "<file>" reference even if the diff came out
    // empty (don't silently drop a reference the user/FITOM_X set up); for
    // a freshly-created inline bank_overrides, only write it if there's
    // actually something to say.
    profile_.bank_overrides.present = !profile_.bank_overrides.externalFile.empty() || !profile_.bank_overrides.data.empty();
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
    // *.pcmbank.json shape as hw_banks[group=ADPCM*] (D-013) - and, per
    // D-038's investigation, actually the one every real profile we've
    // checked (FITOM_staging's emu_opn.profile.json etc) uses for these,
    // not hw_banks[]. `group` is optional per profile.schema.json (empty =
    // legacy "every PCM device shares bank 0" behavior), but resolve it via
    // stringToVoicePatchType() exactly like hw_banks[] does whenever it's
    // set - a fpe::PcmBank left at voicePatchType::None can't be found by
    // any real DrumNote/HwPatch reference into it (PatchWorkspace::
    // findPcmBank() matches on {voicePatchType, bankIndex}), which is
    // exactly the bug this fixes (D-038 "追記2": `group` used to be dropped
    // entirely here, silently, since PcmBankRef didn't even parse it).
    for (const auto& ref : profile_.pcm_banks) {
        try {
            const std::string label = "pcm_banks[bank=" + std::to_string(ref.bank) + "]";
            PcmBank bank = loadPcmBank(resolve(ref.file), label, warnings_);
            if (!ref.group.empty()) {
                auto typeOpt = stringToVoicePatchType(ref.group);
                if (typeOpt) {
                    bank.voicePatchType = *typeOpt;
                } else {
                    warnings_.push_back(label + ": unrecognized group name \"" + ref.group + "\"");
                }
            }
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
    syncBanksSourceForSave();
    // Every write below goes through saveIfDirty() rather than
    // saveJsonFile() directly (D-042): only files whose content actually
    // changed since load()/the last save() get rewritten. Without this,
    // every save() re-serialized the *entire* loaded reference tree
    // (this project's "write explicitly" JSON philosophy re-emits the full
    // canonical field set unconditionally), which for a profile built on a
    // shared "banks" bankset (D-041) meant a single patch edit could touch
    // dozens of files - including ones belonging to other profiles - that
    // were never actually edited this session. A path with no
    // originalContent_ entry (freshly created this session, or rebased by
    // saveAs() to a location that's never been written to) is always
    // treated as dirty, so this doesn't change saveAs()'s "always produces
    // a complete, self-contained copy" behavior.
    //
    // `baseline`/`written` are local to this one save() call (see
    // saveIfDirty()'s comment) - fixes a real data-loss bug where an
    // unedited object sharing a sourceFile with an edited one (D-041: the
    // same physical file registered under two different progs) would
    // silently overwrite the edited one's just-written content back to its
    // own stale value.
    const auto baseline = originalContent_;
    std::set<std::filesystem::path> written;
    saveIfDirty(baseline, originalContent_, written, profilePath_, profile_);
    // "banks" is deliberately never written back here (see
    // syncBanksSourceForSave()) even when it's an external reference; only
    // "bank_overrides" gets a physical file of its own, and only if it was
    // itself an external reference to begin with.
    if (!profile_.bank_overrides.externalFile.empty()) {
        saveIfDirty(baseline, originalContent_, written, resolve(profile_.bank_overrides.externalFile),
                    profile_.bank_overrides.data);
    }
    for (auto& b : patchBanks_) saveIfDirty(baseline, originalContent_, written, b.sourceFile, b);
    for (auto& b : swBanks_) saveIfDirty(baseline, originalContent_, written, b.sourceFile, b);
    for (auto& b : hwBanks_) saveIfDirty(baseline, originalContent_, written, b.sourceFile, b);
    for (auto& b : sampleZoneBanks_) saveIfDirty(baseline, originalContent_, written, b.sourceFile, b);
    // Re-serializes each pcmbank.json's own top-level fields; entries[]
    // read from a separate adpcm_json are NOT duplicated in here (PcmBank's
    // to_json omits them whenever adpcm_json is set) - copying that sidecar
    // file itself alongside is rebaseSourceFiles()'s job (saveAs() calls it
    // before save()), since it needs both the old and new sourceFile.
    for (auto& b : pcmBanks_) saveIfDirty(baseline, originalContent_, written, b.sourceFile, b);
    for (auto& k : drumKits_) saveIfDirty(baseline, originalContent_, written, k.sourceFile, k);
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
    originalContent_.clear();
}

PatchBank* PatchWorkspace::findLayeredPatchBank(int bank) {
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

// --- layered patch banks / patches -----------------------------------------

PatchBank& PatchWorkspace::createLayeredPatchBank(int bankIndex, const std::string& name,
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

bool PatchWorkspace::deleteLayeredPatchBank(int bankIndex) {
    auto it = std::remove_if(patchBanks_.begin(), patchBanks_.end(),
                              [&](const PatchBank& b) { return b.bankIndex == bankIndex; });
    bool removed = it != patchBanks_.end();
    patchBanks_.erase(it, patchBanks_.end());

    auto rit = std::remove_if(profile_.patch_banks.begin(), profile_.patch_banks.end(),
                               [&](const PatchBankRef& r) { return r.bank == bankIndex; });
    profile_.patch_banks.erase(rit, profile_.patch_banks.end());
    return removed;
}

PatchBank* PatchWorkspace::duplicateLayeredPatchBank(int fromBank, int toBank,
                                                     const std::string& newRelativeFile) {
    auto* src = findLayeredPatchBank(fromBank);
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
