#include "fpe/ChipCapabilities.h"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

#include "fpe/HwPatch.h"
#include "fpe/JsonUtil.h"

#ifdef _WIN32
// windows.h defines min/max as macros, which breaks std::max below.
#define NOMINMAX
#include <windows.h>
#endif

namespace fpe {
namespace {

using nlohmann::json;

ChipPatchKind parsePatchKind(const std::string& s) {
    if (s == "fm") return ChipPatchKind::Fm;
    if (s == "builtin_rom") return ChipPatchKind::BuiltinRom;
    if (s == "sample_zone") return ChipPatchKind::SampleZone;
    if (s == "mode_selector") return ChipPatchKind::ModeSelector;
    if (s == "unimplemented") return ChipPatchKind::Unimplemented;
    return ChipPatchKind::Unknown;
}

ParamScope parseScope(const std::string& s) {
    if (s == "operator") return ParamScope::Operator;
    if (s == "ext") return ParamScope::Ext;
    return ParamScope::Channel;
}

void readRange(const json& j, const char* key, int& lo, int& hi, bool& present) {
    present = false;
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 2) return;
    lo = (*it)[0].get<int>();
    hi = (*it)[1].get<int>();
    present = true;
}

ParamCondition parseCondition(const json& j) {
    ParamCondition c;
    if (!j.is_object()) return c;
    c.param = json_util::getOr<std::string>(j, "param", "");
    if (j.contains("mask")) c.mask = j.at("mask").get<uint32_t>();
    if (j.contains("equals")) c.equals = j.at("equals").get<int>();
    if (j.contains("not_equals")) c.notEquals = j.at("not_equals").get<int>();
    if (j.contains("in") && j.at("in").is_array()) c.in = j.at("in").get<std::vector<int>>();
    if (j.contains("channels") && j.at("channels").is_array()) {
        c.channels = j.at("channels").get<std::vector<int>>();
    }
    return c;
}

// Fills the value-shaped fields of `p` from `j`, leaving anything `j` doesn't
// mention at whatever `p` already held. That inheritance is what makes the
// per_op / per_condition entries work as partial overrides of their parent.
void applyParamBody(const json& j, ParamCaps& p) {
    int lo = 0, hi = 0;
    bool present = false;
    readRange(j, "range", lo, hi, present);
    if (present) {
        p.minV = lo;
        p.maxV = hi;
        // Only an inferred effective_range follows the new range; one that
        // was stated explicitly (here or by a parent) survives, per the
        // spec's "keys the branch omits come from the parent" rule.
        if (!p.effExplicit) {
            p.effMin = lo;
            p.effMax = hi;
        }
    }
    readRange(j, "effective_range", lo, hi, present);
    if (present) {
        p.effMin = lo;
        p.effMax = hi;
        p.effExplicit = true;
    }
    if (j.contains("quantum")) p.quantum = std::max(1, j.at("quantum").get<int>());
    if (j.contains("mapping")) p.mapping = j.at("mapping").get<std::string>();
    if (j.contains("note")) p.note = j.at("note").get<std::string>();
    if (j.contains("values") && j.at("values").is_array()) {
        p.values.clear();
        for (const auto& v : j.at("values")) {
            ParamEnumValue e;
            e.value = json_util::getOr<int>(v, "value", 0);
            e.label = json_util::getOr<std::string>(v, "label", "");
            p.values.push_back(e);
        }
    }
    if (j.contains("bits") && j.at("bits").is_array()) {
        p.bits.clear();
        for (const auto& v : j.at("bits")) {
            ParamBit b;
            b.mask = json_util::getOr<uint32_t>(v, "mask", 0u);
            b.label = json_util::getOr<std::string>(v, "label", "");
            p.bits.push_back(b);
        }
    }
    if (j.contains("condition")) p.condition = parseCondition(j.at("condition"));
}

bool conditionHolds(const ParamCondition& c, const HwPatch& patch, int opIndex) {
    if (!c.hasFieldTest()) return true; // `channels`-only conditions never gate - see the header
    const std::optional<int> raw = hwPatchParamValue(patch, c.param, opIndex);
    if (!raw) return true; // unknown field: fail open rather than hiding an editable control
    const int v = static_cast<int>(static_cast<uint32_t>(*raw) & c.mask);
    if (c.equals && v != *c.equals) return false;
    if (c.notEquals && v == *c.notEquals) return false;
    if (!c.in.empty() && std::find(c.in.begin(), c.in.end(), v) == c.in.end()) return false;
    return true;
}

std::optional<std::filesystem::path> searchUpward(std::filesystem::path dir) {
    std::error_code ec;
    for (int depth = 0; depth < 12 && !dir.empty(); ++depth) {
        const std::filesystem::path candidate = dir / "spec" / "chip-capabilities.json";
        if (std::filesystem::exists(candidate, ec)) return candidate;
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return std::nullopt;
}

std::filesystem::path executableDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) return std::filesystem::path(buf).parent_path();
#endif
    return std::filesystem::path();
}

} // namespace

VoicePatchType effectiveVoicePatchType(VoicePatchType bankType, const HwPatch& patch) {
    if (isPsgFamilyVoicePatchType(bankType) &&
        isPsgFamilyVoicePatchType(patch.ext.target_voice_patch_type)) {
        return patch.ext.target_voice_patch_type;
    }
    return bankType;
}

std::optional<int> hwPatchParamValue(const HwPatch& p, const std::string& name, int opIndex) {
    // Channel scope.
    if (name == "FB") return p.hw.FB;
    if (name == "ALG") return p.hw.ALG;
    if (name == "AMS") return p.hw.AMS;
    if (name == "PMS") return p.hw.PMS;
    if (name == "NFQ") return p.hw.NFQ;
    if (name == "FB2") return p.hw.FB2;
    // Ext scope.
    if (name == "FIX") return p.ext.FIX;
    if (name == "ALG_EXT") return p.ext.ALG_EXT;
    if (name == "HWEP") return p.ext.HWEP;
    if (name == "rhythm_ch") return p.ext.rhythm_ch;
    if (name == "target_voice_patch_type") return static_cast<int>(p.ext.target_voice_patch_type);
    // Operator scope - conditions inside an operator refer to that same
    // operator's field (SSG's AR depends on its own EGT), so an unspecified
    // index falls back to operator 0, which is the only one every
    // condition-using chip in the spec actually has.
    const size_t idx = (opIndex >= 0) ? static_cast<size_t>(opIndex) : 0u;
    if (idx >= p.ops.size()) return std::nullopt;
    const FmHwOp& o = p.ops[idx];
    if (name == "AR") return o.AR;
    if (name == "DR") return o.DR;
    if (name == "SL") return o.SL;
    if (name == "SR") return o.SR;
    if (name == "RR") return o.RR;
    if (name == "TL") return o.TL;
    if (name == "KSR") return o.KSR;
    if (name == "KSL") return o.KSL;
    if (name == "MUL") return o.MUL;
    if (name == "DT1") return o.DT1;
    if (name == "DT2") return o.DT2;
    if (name == "PDT") return o.PDT;
    if (name == "AM") return o.AM;
    if (name == "VIB") return o.VIB;
    if (name == "EGT") return o.EGT;
    if (name == "WS") return o.WS;
    if (name == "REV") return o.REV;
    if (name == "EGS") return o.EGS;
    if (name == "DT3") return o.DT3;
    return std::nullopt;
}

ChipCapabilities& ChipCapabilities::instance() {
    static ChipCapabilities inst;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (const auto file = findSpecFile()) inst.loadFromFile(*file);
        else inst.loadError_ = "spec/chip-capabilities.json が見つかりません";
    }
    return inst;
}

std::optional<std::filesystem::path> ChipCapabilities::findSpecFile() {
    std::error_code ec;
    if (auto p = searchUpward(std::filesystem::current_path(ec))) return p;
    const std::filesystem::path exe = executableDir();
    if (!exe.empty()) {
        if (auto p = searchUpward(exe)) return p;
    }
    return std::nullopt;
}

bool ChipCapabilities::loadFromFile(const std::filesystem::path& file) {
    chips_.clear();
    loaded_ = false;
    loadError_.clear();
    sourceFile_.clear();

    std::ifstream in(file);
    if (!in) {
        loadError_ = "開けません: " + file.string();
        return false;
    }
    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        loadError_ = std::string("JSONを解釈できません: ") + e.what();
        return false;
    }
    if (json_util::getOr<std::string>(root, "format", "") != "fitom-chip-capabilities") {
        loadError_ = "format が fitom-chip-capabilities ではありません: " + file.string();
        return false;
    }

    // parameters[] carries the chip-independent half of each definition
    // (scope, label, schema-wide type limits); chips[].params[] then narrows
    // it. Keyed lookups below fall back gracefully when a chip names a
    // parameter this table doesn't list.
    struct GlobalParam {
        ParamScope scope = ParamScope::Channel;
        std::string label;
        int schemaMin = 0;
        int schemaMax = 0;
    };
    std::map<std::string, GlobalParam> globals;
    if (root.contains("parameters")) {
        for (auto it = root.at("parameters").begin(); it != root.at("parameters").end(); ++it) {
            GlobalParam g;
            g.scope = parseScope(json_util::getOr<std::string>(it.value(), "scope", "channel"));
            g.label = json_util::getOr<std::string>(it.value(), "label", "");
            bool present = false;
            readRange(it.value(), "schema_range", g.schemaMin, g.schemaMax, present);
            globals[it.key()] = g;
        }
    }

    // First pass: everything except same_params_as, which needs the target
    // chip to already exist.
    std::map<std::string, VoicePatchType> byId;
    std::map<VoicePatchType, std::string> inherits;
    for (const auto& cj : root.value("chips", json::array())) {
        ChipCaps c;
        c.id = json_util::getOr<std::string>(cj, "id", "");
        c.type = static_cast<VoicePatchType>(json_util::getOr<int>(cj, "voice_patch_type", 0));
        c.displayName = json_util::getOr<std::string>(cj, "display_name", c.id);
        c.voiceGroup = json_util::getOr<std::string>(cj, "voice_group", "");
        c.kind = parsePatchKind(json_util::getOr<std::string>(cj, "patch_kind", ""));

        const auto oc = cj.find("operator_count");
        if (oc != cj.end() && oc->is_number_integer()) {
            c.operatorCount = oc->get<int>();
        } else if (oc != cj.end() && oc->is_object()) {
            if (oc->contains("by_rhythm_ch")) {
                c.operatorCountByRhythmCh = oc->at("by_rhythm_ch").get<std::vector<int>>();
            }
        }

        if (cj.contains("params")) {
            for (auto it = cj.at("params").begin(); it != cj.at("params").end(); ++it) {
                ParamCaps p;
                p.name = it.key();
                const auto g = globals.find(p.name);
                if (g != globals.end()) {
                    p.scope = g->second.scope;
                    p.label = g->second.label;
                    p.minV = g->second.schemaMin;
                    p.maxV = g->second.schemaMax;
                    p.effMin = p.minV;
                    p.effMax = p.maxV;
                }
                applyParamBody(it.value(), p);

                if (it.value().contains("per_op")) {
                    for (auto oit = it.value().at("per_op").begin();
                         oit != it.value().at("per_op").end(); ++oit) {
                        ParamCaps variant = p; // inherit, then override
                        variant.perOp.clear();
                        variant.perCondition.clear();
                        applyParamBody(oit.value(), variant);
                        p.perOp[std::stoi(oit.key())] = variant;
                    }
                }
                if (it.value().contains("per_condition")) {
                    for (const auto& vj : it.value().at("per_condition")) {
                        ParamCaps variant = p;
                        variant.perOp.clear();
                        variant.perCondition.clear();
                        applyParamBody(vj, variant);
                        p.perCondition.push_back(variant);
                    }
                }
                c.params[p.name] = std::move(p);
            }
        }

        byId[c.id] = c.type;
        const std::string same = json_util::getOr<std::string>(cj, "same_params_as", "");
        if (!same.empty()) inherits[c.type] = same;
        chips_[c.type] = std::move(c);
    }

    // Second pass: same_params_as (OPZ2->OPZ, OPLLP/OPLLX/VRC7->OPLL).
    for (const auto& [type, sourceId] : inherits) {
        const auto srcType = byId.find(sourceId);
        if (srcType == byId.end()) continue;
        const auto src = chips_.find(srcType->second);
        const auto dst = chips_.find(type);
        if (src == chips_.end() || dst == chips_.end()) continue;
        dst->second.params = src->second.params;
        if (!dst->second.operatorCount) dst->second.operatorCount = src->second.operatorCount;
        if (dst->second.operatorCountByRhythmCh.empty()) {
            dst->second.operatorCountByRhythmCh = src->second.operatorCountByRhythmCh;
        }
    }

    loaded_ = !chips_.empty();
    if (!loaded_) loadError_ = "chips[] が空です: " + file.string();
    sourceFile_ = file;
    return loaded_;
}

const ChipCaps* ChipCapabilities::chip(VoicePatchType t) const {
    const auto it = chips_.find(t);
    return (it == chips_.end()) ? nullptr : &it->second;
}

const ParamCaps* ChipCapabilities::param(VoicePatchType t, const std::string& name) const {
    const ChipCaps* c = chip(t);
    if (!c) return nullptr;
    const auto it = c->params.find(name);
    return (it == c->params.end()) ? nullptr : &it->second;
}

std::optional<ResolvedParam> ChipCapabilities::resolve(VoicePatchType t, const std::string& name,
                                                        const HwPatch& patch, int opIndex) const {
    const ParamCaps* base = param(t, name);
    if (!base) return std::nullopt;

    const ParamCaps* eff = base;
    if (!base->perOp.empty()) {
        // Operator-scope parameter restricted to specific operators.
        const auto it = base->perOp.find(opIndex);
        if (it == base->perOp.end()) return std::nullopt;
        eff = &it->second;
    }
    if (!eff->perCondition.empty()) {
        const ParamCaps* chosen = nullptr;
        for (const auto& v : eff->perCondition) {
            if (conditionHolds(v.condition, patch, opIndex)) {
                chosen = &v;
                break;
            }
        }
        // No variant matches: keep the parent definition but report it
        // inactive, so the control greys out instead of vanishing.
        if (chosen) eff = chosen;
    }

    ResolvedParam r;
    r.base = base;
    r.minV = eff->minV;
    r.maxV = eff->maxV;
    r.effMin = eff->effMin;
    r.effMax = eff->effMax;
    r.quantum = eff->quantum;
    r.mapping = eff->mapping;
    r.note = eff->note;
    r.values = eff->values.empty() ? nullptr : &eff->values;
    r.bits = eff->bits.empty() ? nullptr : &eff->bits;
    r.channels = eff->condition.channels.empty() ? nullptr : &eff->condition.channels;
    r.active = conditionHolds(eff->condition, patch, opIndex);
    return r;
}

int ChipCapabilities::operatorCount(VoicePatchType t, const HwPatch& patch) const {
    const ChipCaps* c = chip(t);
    if (!c) return 0;
    if (!c->operatorCountByRhythmCh.empty()) {
        const int ch = patch.ext.rhythm_ch;
        if (ch >= 0 && ch < static_cast<int>(c->operatorCountByRhythmCh.size())) {
            return c->operatorCountByRhythmCh[static_cast<size_t>(ch)];
        }
        return 0; // rhythm_ch unset (255) - caller decides, see D-033
    }
    return c->operatorCount.value_or(0);
}

void pruneHwPatchJson(nlohmann::json& j, VoicePatchType bankType, const HwPatch& p) {
    const ChipCapabilities& caps = ChipCapabilities::instance();
    const VoicePatchType t = effectiveVoicePatchType(bankType, p);
    const ChipCaps* c = caps.chip(t);
    if (!c || c->params.empty() || !j.is_object()) return;
    // builtin-reference entries have no hw/ops/ext half at all.
    if (j.contains("builtin")) return;

    // Pruning must never destroy information. A key the chip doesn't read is
    // dropped only while it still holds its default - which is the case for
    // the overwhelming majority of them, and is the whole point (matching
    // the terse field set FITOM_X's own writer emits). A non-default value
    // is always kept, whatever the spec says about the chip.
    //
    // This is a guard against the spec being incomplete, not a theoretical
    // one: ../FITOM_staging has 109 OPN2-tagged patches carrying a non-zero
    // PMS even though chip-capabilities.json lists no AMS/PMS for OPN2.
    // Whether those patches are mis-tagged or the spec has a gap is
    // FITOM_X's question to answer; either way this editor must not be the
    // thing that silently deletes them.
    auto isDefault = [&](const std::string& name, int opIndex) {
        const std::optional<int> v = hwPatchParamValue(p, name, opIndex);
        if (!v) return true;
        // rhythm_ch's "unset" marker is 255, every other field's default is 0.
        return (name == "rhythm_ch") ? (*v == 255) : (*v == 0);
    };
    auto keeps = [&](const std::string& name, ParamScope scope, int opIndex) {
        const auto it = c->params.find(name);
        if (it == c->params.end() || it->second.scope != scope) {
            return !isDefault(name, opIndex);
        }
        // Restricted to specific operators (OPL3's PDT) - drop it from the
        // others. `condition` is NOT consulted: a field that is merely
        // inactive right now still holds a value the user set, and dropping
        // it would silently reset it on the next save.
        if (scope == ParamScope::Operator && !it->second.perOp.empty()) {
            return it->second.perOp.count(opIndex) > 0 || !isDefault(name, opIndex);
        }
        return true;
    };

    static const char* kChannelKeys[] = {"FB", "ALG", "AMS", "PMS", "NFQ", "FB2"};
    for (const char* k : kChannelKeys) {
        if (!keeps(k, ParamScope::Channel, -1)) j.erase(k);
    }

    if (j.contains("ops") && j.at("ops").is_array()) {
        static const char* kOpKeys[] = {"AR", "DR",  "SL", "SR",  "RR",  "TL", "KSR",
                                        "KSL", "MUL", "DT1", "DT2", "PDT", "AM", "VIB",
                                        "EGT", "WS",  "REV", "EGS", "DT3"};
        auto& ops = j.at("ops");
        for (size_t i = 0; i < ops.size(); ++i) {
            for (const char* k : kOpKeys) {
                if (!keeps(k, ParamScope::Operator, static_cast<int>(i))) ops[i].erase(k);
            }
        }
    }

    if (j.contains("ext") && j.at("ext").is_object()) {
        static const char* kExtKeys[] = {"FIX", "ALG_EXT", "HWEP", "rhythm_ch",
                                         "target_voice_patch_type"};
        auto& ext = j.at("ext");
        for (const char* k : kExtKeys) {
            if (!keeps(k, ParamScope::Ext, -1)) ext.erase(k);
        }
        // An "ext" that ended up empty carries no information; FITOM_X's own
        // writer omits the key entirely in that case.
        if (ext.empty()) j.erase("ext");
    }
}

} // namespace fpe
