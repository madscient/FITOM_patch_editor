#pragma once
#include <cstdint>
#include <vector>

// Thin transport for FITOM_X's internal MIDI pipe (FITOM_X repo's
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
// Pure transport only (raw byte send + connection state) - message
// building lives in MidiMessages.h (shared with the RtMidiOutput
// fallback), and picking which transport to use for a given call lives in
// PreviewOutput (see docs/DESIGN.md D-018).
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

private:
#ifdef _WIN32
    void* handle_ = nullptr; // HANDLE, kept opaque to avoid pulling <windows.h> into this header
#else
    int fd_ = -1;
#endif
};
