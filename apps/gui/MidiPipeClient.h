#pragma once
#include <cstdint>
#include <vector>

// Thin transport for FITOM_X's internal MIDI pipe (FITOM_X repo's
// docs/plugin-midi-pipe.md): a named pipe on Windows
// (\\.\pipe\FITOM_X_MIDI) or a Unix domain socket on Linux/macOS
// (/tmp/fitom_x_midi.sock). Single client, silently unavailable when
// FITOM_X isn't running (or wasn't built with the fitom_midi_pipe
// backend) - that is an expected, non-error condition for this project's
// offline-first design (see README.md), not a failure to report to the
// user as an error popup.
//
// Since 2026-07 (FITOM_X docs/plugin-midi-pipe.md 4.1), the MIDI channel
// this connection uses is NOT chosen by the caller - immediately after
// connecting, FITOM_X writes a 7-byte private SysEx
// (F0 00 48 01 03 <ch> F7) assigning the channel for this connection (so
// multiple simultaneously-running patch editor instances - one process
// per connection - don't collide on the same channel). ensureConnected()
// performs this handshake read before returning, and assignedChannel()
// exposes the result. If FITOM_X is already at its 16-connection cap, it
// sends nothing and drops the connection instead - see
// wasRejectedForCapacity(), which is a genuine error (unlike "FITOM_X
// isn't running") the caller should surface to the user rather than
// silently fall back to offline mode.
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

    // Connects (including the channel-assignment handshake) if not already
    // connected. Cheap to call every frame - a failed attempt just means
    // "still offline", not an error, UNLESS wasRejectedForCapacity() is
    // now true (see below).
    bool ensureConnected();
    bool isConnected() const;
    void disconnect();

    // Raw byte send. Returns false (and disconnects, so the next
    // ensureConnected() retries) on write failure.
    bool sendRaw(const std::vector<uint8_t>& bytes);

    // Valid only while isConnected() - the MIDI channel (0-15) FITOM_X
    // assigned this connection during the post-connect handshake.
    uint8_t assignedChannel() const { return assignedChannel_; }

    // True after the most recent ensureConnected() attempt found FITOM_X
    // running but already serving 16 other connections (its documented
    // hard cap) - it accepted the raw connection but sent no handshake
    // and hung up. Stays true until the next connection attempt (success
    // or plain "not running") starts, so the caller can check it once per
    // frame and act (see docs/DESIGN.md D-030).
    bool wasRejectedForCapacity() const { return rejectedForCapacity_; }

private:
#ifdef _WIN32
    void* handle_ = nullptr; // HANDLE, kept opaque to avoid pulling <windows.h> into this header
#else
    int fd_ = -1;
#endif
    uint8_t assignedChannel_ = 0;
    bool rejectedForCapacity_ = false;
};
