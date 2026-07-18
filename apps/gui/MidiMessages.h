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

} // namespace midimsg
