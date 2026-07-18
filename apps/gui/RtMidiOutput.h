#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class RtMidiOut;

// Thin wrapper around RtMidi's RtMidiOut - the "regular MIDI output"
// fallback used when FITOM_X's internal pipe isn't available (see
// MidiPipeClient, PreviewOutput, docs/DESIGN.md D-018). Real hardware/
// software synths won't understand FITOM_X's own SysEx parameter-override
// protocol, but note on/off, bank-select+program-change, and CC still
// work normally against whatever's listening on the chosen port.
//
// Construction can throw (RtMidi throws RtMidiError if no compiled MIDI
// API is available on this platform at all - not "no ports", an actual
// build/environment problem) - callers should be prepared for that to
// fail gracefully rather than crash the whole app.
class RtMidiOutput {
public:
    RtMidiOutput();
    ~RtMidiOutput();
    RtMidiOutput(const RtMidiOutput&) = delete;
    RtMidiOutput& operator=(const RtMidiOutput&) = delete;

    // True if construction succeeded and at least one MIDI API backend is
    // available on this platform (independent of whether a port is open).
    bool isAvailable() const;

    // Re-queries available output ports (real hardware + virtual/software
    // ones, e.g. loopMIDI on Windows) every call - cheap enough to call
    // each time the preferences dialog opens.
    std::vector<std::string> listPorts() const;

    // Opens the given port index (from listPorts()). Returns false on
    // failure (invalid index, device busy/removed, etc), leaving no port
    // open. Closes any previously open port first either way.
    bool openPort(unsigned int index);
    void close();
    bool isOpen() const;

    bool sendRaw(const std::vector<uint8_t>& bytes);

private:
    std::unique_ptr<RtMidiOut> out_;
    bool portOpen_ = false;
};
