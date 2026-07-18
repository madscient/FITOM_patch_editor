#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Thin client for FITOM_X's internal MIDI pipe (FITOM_X repo's
// docs/plugin-midi-pipe.md): a named pipe on Windows
// (\\.\pipe\FITOM_X_MIDI) or a Unix domain socket on Linux/macOS
// (/tmp/fitom_x_midi.sock). Send-only, single client, silently
// unavailable when FITOM_X isn't running (or wasn't built with the
// fitom_midi_pipe backend) - that is an expected, non-error condition for
// this project's offline-first design (see README.md), not a failure to
// report to the user as an error popup.
//
// NOTE ON CONFIDENCE: the POSIX (Unix domain socket) path is implemented
// to the same spec as the Windows named-pipe path but has not been
// exercised on this development machine (Windows-only so far).
//
// There is no fallback to real MIDI hardware/virtual ports when FITOM_X
// isn't running - see docs/DESIGN.md D-015 for why that's out of scope
// for now.
class MidiPipeClient {
public:
    MidiPipeClient() = default;
    ~MidiPipeClient();
    MidiPipeClient(const MidiPipeClient&) = delete;
    MidiPipeClient& operator=(const MidiPipeClient&) = delete;

    // Connects if not already connected. Cheap to call every frame - a
    // failed attempt just means "still offline", not an error.
    bool ensureConnected();
    bool isConnected() const;
    void disconnect();

    // Raw byte send. Returns false (and disconnects, so the next
    // ensureConnected() retries) on write failure.
    bool sendRaw(const std::vector<uint8_t>& bytes);

    // docs/plugin-midi-pipe.md section 5.1: direct device select + program change.
    bool selectDevice(uint8_t channel, uint8_t voicePatchTypeCc0, uint8_t hwBank, uint8_t hwProg);
    // section 5.2: HwPatch/SwPatch parameter override (private SysEx). `json` is the
    // ASCII JSON payload only - this wraps it in the F0 00 48 01 ... F7 envelope.
    bool sendHwPatchOverride(uint8_t channel, const std::string& json);
    bool sendSwPatchOverride(uint8_t channel, const std::string& json);
    // section 5.3: note on/off.
    bool noteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    bool noteOff(uint8_t channel, uint8_t note, uint8_t velocity = 0);
    // Generic Control Change - used for the preview keyboard's CC#1
    // (modulation) / CC#7 (volume) levers.
    bool sendControlChange(uint8_t channel, uint8_t ccNumber, uint8_t value);
    // section 5.5: cleanup (also clears HwPatch/SwPatch overrides).
    bool allSoundOff(uint8_t channel);
    bool resetAllControllers(uint8_t channel);

private:
    bool sendParamOverride(uint8_t subCmd, uint8_t channel, const std::string& json);

#ifdef _WIN32
    void* handle_ = nullptr; // HANDLE, kept opaque to avoid pulling <windows.h> into this header
#else
    int fd_ = -1;
#endif
};
