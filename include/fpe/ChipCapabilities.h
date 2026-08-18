#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "fpe/VoicePatchType.h"

// ChipCapabilities: FITOM_X's spec/chip-capabilities.json, parsed.
//
// That file declares, per VoicePatchType, exactly which HwPatch fields the
// chip's driver actually reads and what their value ranges / effective
// resolutions / activation conditions are. It is the machine-readable
// counterpart of docs/manuals/hwpatch-reference.md and supersedes the
// per-chip range tables this editor used to hand-maintain (docs/DESIGN.md
// D-057). FITOM_X's own core never reads it - core/ is the authority and
// this file is transcribed from it for external tools like this one.
//
// A local copy lives at spec/chip-capabilities.json and is located at
// runtime by an upward search (same approach as assets/ and fixtures/), so
// the editor still starts if it is missing - callers just get "no data"
// and should fall back to permissive behavior rather than blocking edits.

namespace fpe {

struct HwPatch;

// chips[].patch_kind - what shape of voice data the chip uses at all.
enum class ChipPatchKind {
    Unknown,
    Fm,          // ordinary HwPatch with ops[] (*.hwbank.json)
    BuiltinRom,  // voices fixed in chip ROM, no user bank possible (DSG)
    SampleZone,  // *.samplezonebank.json / pcmbank entries, ops[] unused
    ModeSelector,// bank-select value that names no chip (0x70 / 0x7f)
    Unimplemented,
};

// parameters[].scope - where the field lives inside a patch's JSON.
enum class ParamScope { Channel, Operator, Ext };

// chips[].params[].condition - when the parameter is read at all.
struct ParamCondition {
    std::string param;            // other field this depends on; empty = none
    uint32_t mask = 0xFFFFFFFFu;  // applied to that field's value first
    std::optional<int> equals;
    std::optional<int> notEquals;
    std::vector<int> in;
    // Sounding-channel restriction (e.g. OPN's FIX is ch2-only). Deliberately
    // NOT part of isActive(): the editor has no channel to test against, and
    // for every chip that uses it the patch's own value is what *causes*
    // FITOM_X to route it to that channel. Surfaced as text instead.
    std::vector<int> channels;

    bool hasFieldTest() const { return !param.empty(); }
    bool empty() const { return param.empty() && channels.empty(); }
};

struct ParamEnumValue {
    int value = 0;
    std::string label;
};
struct ParamBit {
    uint32_t mask = 0;
    std::string label;
};

struct ParamCaps {
    std::string name;
    ParamScope scope = ParamScope::Channel;
    std::string label; // parameters[].label (English, chip-independent)

    int minV = 0;
    int maxV = 0;
    // effective_range: the sub-range that actually reaches the hardware.
    // Equal to minV/maxV when the spec declares none.
    int effMin = 0;
    int effMax = 0;
    // quantum: patch-value step corresponding to one hardware register step.
    int quantum = 1;
    std::string mapping; // direct / attenuation_db / bitfield / enum / index / raw
    std::string note;
    std::vector<ParamEnumValue> values;
    std::vector<ParamBit> bits;
    ParamCondition condition;

    // per_op: when non-empty, the parameter is meaningful ONLY on these
    // operator indices, each optionally overriding the fields above.
    // (The spec calls this an "override" table; treating an absent index as
    // "not applicable" is what matches both of its real uses - OPL3's PDT,
    // read only for ops 0/2, and OPLL's TL, which lists every operator it
    // has. See docs/DESIGN.md D-057.)
    std::map<int, ParamCaps> perOp;

    // per_condition: variants whose meaning changes with another field, each
    // carrying its own condition. The first whose condition holds wins.
    std::vector<ParamCaps> perCondition;
};

struct ChipCaps {
    std::string id;
    VoicePatchType type = VoicePatchType::None;
    std::string displayName;
    std::string voiceGroup;
    ChipPatchKind kind = ChipPatchKind::Unknown;

    std::optional<int> operatorCount;          // fixed ops[] size
    std::vector<int> operatorCountByRhythmCh;  // OPL_RHY: size per ext.rhythm_ch

    std::map<std::string, ParamCaps> params; // exhaustive: absent = never read
};

// A ParamCaps flattened for one specific operator and one specific patch
// state (per_op + per_condition already applied).
struct ResolvedParam {
    const ParamCaps* base = nullptr; // the chip-level entry this came from
    int minV = 0;
    int maxV = 0;
    int effMin = 0;
    int effMax = 0;
    int quantum = 1;
    std::string mapping;
    std::string note;
    const std::vector<ParamEnumValue>* values = nullptr;
    const std::vector<ParamBit>* bits = nullptr;
    const std::vector<int>* channels = nullptr; // informational, may be null/empty
    // False when the parameter exists for this chip but its condition is not
    // currently satisfied (e.g. SSG's AR while the HW envelope flag is set).
    // Callers should show it disabled rather than hidden - unlike a parameter
    // missing from `params` entirely, this one becomes live again if the
    // controlling field changes.
    bool active = true;
};

class ChipCapabilities {
public:
    // Process-wide registry. Loads spec/chip-capabilities.json on first use
    // via findSpecFile(); stays empty (loaded() == false) if it isn't found,
    // which every caller must treat as "no restrictions known".
    static ChipCapabilities& instance();

    bool loaded() const { return loaded_; }
    const std::string& loadError() const { return loadError_; }
    const std::filesystem::path& sourceFile() const { return sourceFile_; }

    bool loadFromFile(const std::filesystem::path& file);

    // Upward search for spec/chip-capabilities.json from `start` (default:
    // the current working directory), then from the running executable's own
    // directory. Same convention as the assets/ and fixtures/ lookups.
    static std::optional<std::filesystem::path> findSpecFile();

    const ChipCaps* chip(VoicePatchType t) const;
    // Raw chip-level entry, before per_op/per_condition are applied.
    const ParamCaps* param(VoicePatchType t, const std::string& name) const;

    // Flattens `name` for `opIndex` (-1 for channel/ext scope) against the
    // patch's current field values. Returns nullopt when the chip never reads
    // the parameter at all (including "not on this operator index").
    std::optional<ResolvedParam> resolve(VoicePatchType t, const std::string& name,
                                          const HwPatch& patch, int opIndex = -1) const;

    // ops[] size this chip wants for this patch. 0 = unknown/not applicable.
    int operatorCount(VoicePatchType t, const HwPatch& patch) const;

    const std::map<VoicePatchType, ChipCaps>& chips() const { return chips_; }

private:
    std::map<VoicePatchType, ChipCaps> chips_;
    bool loaded_ = false;
    std::string loadError_;
    std::filesystem::path sourceFile_;
};

// The chip a patch is really for, given the tag on the bank holding it.
// Identity except for the PSG family, whose five chips share one bank
// namespace registered under SSG (docs/patch-structure-design.md
// "PSG系共有バンク"); there, each patch names its own target in
// ext.target_voice_patch_type. Every per-chip decision - which fields to
// show, which ranges to apply, which keys to keep when saving - has to go
// through this, or an EPSG patch sitting in an SSG-tagged bank gets SSG's
// answers and loses the fields only EPSG uses.
VoicePatchType effectiveVoicePatchType(VoicePatchType bankType, const HwPatch& patch);

// Current value of a named capability parameter inside a patch, or nullopt
// if the name isn't one this editor's HwPatch model carries. `opIndex` is
// ignored for channel/ext-scope names, and required for operator-scope ones.
std::optional<int> hwPatchParamValue(const HwPatch& p, const std::string& name, int opIndex);

// Strips every key the chip's driver never reads from an already-serialized
// HwPatch object, so saved banks match the field set FITOM_X's own writer
// produces instead of always re-emitting all 19 operator fields (D-057).
// A no-op when the capabilities data isn't loaded or the chip is unknown,
// which keeps the editor's output unchanged in that case rather than
// guessing.
void pruneHwPatchJson(nlohmann::json& j, VoicePatchType t, const HwPatch& p);

} // namespace fpe
