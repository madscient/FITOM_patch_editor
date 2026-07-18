#include "PreviewOutput.h"

#include "MidiMessages.h"

void PreviewOutput::configureRtMidiPort(int portIndex) {
    if (portIndex == configuredPortIndex_) return;
    configuredPortIndex_ = portIndex;
    if (portIndex < 0) {
        rtMidi_.close();
        return;
    }
    rtMidi_.openPort(static_cast<unsigned int>(portIndex));
}

PreviewOutput::ActiveBackend PreviewOutput::ensureReady() {
    if (pipe_.ensureConnected()) return ActiveBackend::FitomXPipe;
    if (rtMidi_.isOpen()) return ActiveBackend::RtMidi;
    return ActiveBackend::None;
}

bool PreviewOutput::send(const std::vector<uint8_t>& bytes) {
    if (pipe_.ensureConnected()) return pipe_.sendRaw(bytes);
    if (rtMidi_.isOpen()) return rtMidi_.sendRaw(bytes);
    return false;
}

bool PreviewOutput::selectDevice(uint8_t channel, uint8_t voicePatchTypeCc0, uint8_t hwBank, uint8_t hwProg) {
    return send(midimsg::selectDevice(channel, voicePatchTypeCc0, hwBank, hwProg));
}
bool PreviewOutput::sendHwPatchOverride(uint8_t channel, const std::string& json) {
    return send(midimsg::paramOverrideSysEx(0x01, channel, json));
}
bool PreviewOutput::sendSwPatchOverride(uint8_t channel, const std::string& json) {
    return send(midimsg::paramOverrideSysEx(0x02, channel, json));
}
bool PreviewOutput::noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    return send(midimsg::noteOn(channel, note, velocity));
}
bool PreviewOutput::noteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    return send(midimsg::noteOff(channel, note, velocity));
}
bool PreviewOutput::sendControlChange(uint8_t channel, uint8_t ccNumber, uint8_t value) {
    return send(midimsg::controlChange(channel, ccNumber, value));
}
bool PreviewOutput::allSoundOff(uint8_t channel) { return send(midimsg::allSoundOff(channel)); }
bool PreviewOutput::resetAllControllers(uint8_t channel) { return send(midimsg::resetAllControllers(channel)); }
