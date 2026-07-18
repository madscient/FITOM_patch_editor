#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "MidiPipeClient.h"
#include "RtMidiOutput.h"

// Unifies the two preview-audio transports behind one semantic API, so
// callers (renderPatchEditor()'s preview keyboard) don't need to branch:
// FITOM_X's internal MIDI pipe (primary - lets the user hear live
// parameter edits via its private SysEx, see docs/plugin-midi-pipe.md) if
// an instance is running, otherwise a regular MIDI output port via RtMidi
// (configured through Preferences) so there's still audible feedback with
// FITOM_X closed, understanding that a generic MIDI receiver won't act on
// the parameter-override SysEx (see docs/DESIGN.md D-018).
class PreviewOutput {
public:
    enum class ActiveBackend { None, FitomXPipe, RtMidi };

    // Selects which RtMidi port to use as fallback (-1 = disabled/none).
    // Safe to call every frame - only reopens when the index actually
    // changes. Call with -1 to release the port (e.g. user picks "none"
    // in Preferences).
    void configureRtMidiPort(int portIndex);
    bool rtMidiAvailable() const { return rtMidi_.isAvailable(); }
    std::vector<std::string> listRtMidiPorts() const { return rtMidi_.listPorts(); }

    // Tries the pipe first (cheap to call every frame - see
    // MidiPipeClient::ensureConnected()); returns whichever backend would
    // actually be used right now, for the preview status line.
    ActiveBackend ensureReady();

    bool selectDevice(uint8_t channel, uint8_t voicePatchTypeCc0, uint8_t hwBank, uint8_t hwProg);
    bool sendHwPatchOverride(uint8_t channel, const std::string& json);
    bool sendSwPatchOverride(uint8_t channel, const std::string& json);
    bool noteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    bool noteOff(uint8_t channel, uint8_t note, uint8_t velocity = 0);
    bool sendControlChange(uint8_t channel, uint8_t ccNumber, uint8_t value);
    bool allSoundOff(uint8_t channel);
    bool resetAllControllers(uint8_t channel);

private:
    bool send(const std::vector<uint8_t>& bytes);

    MidiPipeClient pipe_;
    RtMidiOutput rtMidi_;
    int configuredPortIndex_ = -1;
};
