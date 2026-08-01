#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Pure MIDI 1.0 byte-sequence builders, shared by both preview-output
// transports (MidiPipeClient's internal FITOM_X pipe and RtMidiOutput's
// regular MIDI port fallback - see docs/DESIGN.md D-018) so the wire
// format can't drift between two independently-maintained copies. No I/O
// here - PreviewOutput sends whatever these return via whichever
// transport is currently active.
namespace midimsg {

inline std::vector<uint8_t> noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    return {static_cast<uint8_t>(0x90 | (channel & 0x0F)), note, velocity};
}
inline std::vector<uint8_t> noteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    return {static_cast<uint8_t>(0x80 | (channel & 0x0F)), note, velocity};
}
inline std::vector<uint8_t> controlChange(uint8_t channel, uint8_t ccNumber, uint8_t value) {
    return {static_cast<uint8_t>(0xB0 | (channel & 0x0F)), ccNumber, value};
}
inline std::vector<uint8_t> allSoundOff(uint8_t channel) { return controlChange(channel, 0x78, 0x00); }
inline std::vector<uint8_t> resetAllControllers(uint8_t channel) { return controlChange(channel, 0x79, 0x00); }

// docs/plugin-midi-pipe.md section 5.1: direct device select + program
// change, combined into one message sequence (CC#0 bank MSB / CC#32 bank
// LSB / program change).
inline std::vector<uint8_t> selectDevice(uint8_t channel, uint8_t voicePatchTypeCc0, uint8_t hwBank,
                                          uint8_t hwProg) {
    const uint8_t ch = channel & 0x0F;
    return {
        static_cast<uint8_t>(0xB0 | ch), 0x00, voicePatchTypeCc0,
        static_cast<uint8_t>(0xB0 | ch), 0x20, hwBank,
        static_cast<uint8_t>(0xC0 | ch), hwProg,
    };
}

// FITOM_X's private SysEx (docs/plugin-midi-pipe.md section 5.2:
// F0 00 48 01 <sub-cmd> 00 <ch> 00 <JSON> F7). Harmless to send to a
// generic MIDI receiver too - unrecognized manufacturer-ID SysEx is
// simply dropped per the MIDI spec - so this is reused as-is for the
// RtMidi fallback rather than special-cased away for it.
inline std::vector<uint8_t> paramOverrideSysEx(uint8_t subCmd, uint8_t channel, const std::string& json) {
    std::vector<uint8_t> msg;
    msg.push_back(0xF0);
    msg.push_back(0x00);
    msg.push_back(0x48);
    msg.push_back(0x01);
    msg.push_back(subCmd);
    msg.push_back(0x00);           // target-type = 0x00 (channel)
    msg.push_back(channel & 0x0F); // target-addr for channel target: 1 byte, MIDI channel
    msg.push_back(0x00);           // layer = 0 (single-layer preview)
    msg.insert(msg.end(), json.begin(), json.end());
    msg.push_back(0xF7);
    return msg;
}

// docs/plugin-midi-pipe.md section 5.6 / docs/manuals/midi-message-
// reference.md section 8.1's target-type=0x01 ("プリセットバンク") variant
// of the same private SysEx (D-049) - unlike paramOverrideSysEx()'s
// target-type=0x00 (channel-scoped, expires on the next Program Change),
// this rewrites the bank/program's own data directly in FITOM_X's memory,
// so it persists across Program Changes and applies to every channel that
// references that bank+prog. Meant to be sent right after this editor's own
// save-to-disk, so a running FITOM_X's in-memory copy stays in sync without
// a restart. Still memory-only on FITOM_X's side - writing the file is this
// editor's own separate, already-existing responsibility. `layer` is fixed
// at 0x00 (ignored for target-type=0x01 per the doc - a bank/program is
// singular, layer selection doesn't apply).
inline std::vector<uint8_t> hwPatchBankOverrideSysEx(uint8_t voicePatchTypeCc0, uint8_t hwBank, uint8_t hwProg,
                                                      const std::string& json) {
    std::vector<uint8_t> msg = {0xF0, 0x00, 0x48, 0x01, 0x01, 0x01, voicePatchTypeCc0, hwBank, hwProg, 0x00};
    msg.insert(msg.end(), json.begin(), json.end());
    msg.push_back(0xF7);
    return msg;
}
inline std::vector<uint8_t> swPatchBankOverrideSysEx(uint8_t swBank, uint8_t swProg, const std::string& json) {
    std::vector<uint8_t> msg = {0xF0, 0x00, 0x48, 0x01, 0x02, 0x01, swBank, swProg, 0x00};
    msg.insert(msg.end(), json.begin(), json.end());
    msg.push_back(0xF7);
    return msg;
}

// docs/manuals/midi-message-reference.md section 8.1.1 (2026-08 FITOM_X
// addition, D-049) - sub-cmd 0x06 (Patch/レイヤードパッチ) and 0x07
// (DrumPatch/ドラムキット) persistence, a NEW message shape rather than a
// target-type variant of the existing one: no target-type/layer framing at
// all, just `<sub-cmd> <bank> <prog> <JSON>` (the doc explains why -
// channel-scoped temporary overrides for these are already covered by the
// existing NRPN 97 ToneLayer override and NRPN 24/26/28 drum-instrument
// NRPNs, so this bank-direct-edit path doesn't need its own target-type/
// layer variant the way HwPatch/SwPatch's already-established mechanism
// does). Drum banks are always bank 0 (profile.schema.json: "ドラムバンクは
// 常に固定バンク番号0を使う").
inline std::vector<uint8_t> layeredPatchBankOverrideSysEx(uint8_t patchBank, uint8_t prog, const std::string& json) {
    std::vector<uint8_t> msg = {0xF0, 0x00, 0x48, 0x01, 0x06, patchBank, prog};
    msg.insert(msg.end(), json.begin(), json.end());
    msg.push_back(0xF7);
    return msg;
}
inline std::vector<uint8_t> drumKitBankOverrideSysEx(uint8_t drumBank, uint8_t prog, const std::string& json) {
    std::vector<uint8_t> msg = {0xF0, 0x00, 0x48, 0x01, 0x07, drumBank, prog};
    msg.insert(msg.end(), json.begin(), json.end());
    msg.push_back(0xF7);
    return msg;
}

} // namespace midimsg
