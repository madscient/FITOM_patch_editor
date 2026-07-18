#include "RtMidiOutput.h"

#include <rtmidi/RtMidi.h>

RtMidiOutput::RtMidiOutput() {
    try {
        out_ = std::make_unique<RtMidiOut>();
    } catch (const RtMidiError&) {
        out_.reset(); // no MIDI API compiled/available on this platform - isAvailable() reports this
    }
}

RtMidiOutput::~RtMidiOutput() { close(); }

bool RtMidiOutput::isAvailable() const { return out_ != nullptr; }

std::vector<std::string> RtMidiOutput::listPorts() const {
    std::vector<std::string> names;
    if (!out_) return names;
    try {
        const unsigned int count = out_->getPortCount();
        names.reserve(count);
        for (unsigned int i = 0; i < count; ++i) names.push_back(out_->getPortName(i));
    } catch (const RtMidiError&) {
        // leave `names` as whatever was collected so far
    }
    return names;
}

bool RtMidiOutput::openPort(unsigned int index) {
    close();
    if (!out_) return false;
    try {
        out_->openPort(index);
        portOpen_ = true;
        return true;
    } catch (const RtMidiError&) {
        portOpen_ = false;
        return false;
    }
}

void RtMidiOutput::close() {
    if (out_ && portOpen_) {
        try {
            out_->closePort();
        } catch (const RtMidiError&) {
            // nothing more we can do
        }
    }
    portOpen_ = false;
}

bool RtMidiOutput::isOpen() const { return portOpen_; }

bool RtMidiOutput::sendRaw(const std::vector<uint8_t>& bytes) {
    if (!out_ || !portOpen_ || bytes.empty()) return false;
    try {
        // RtMidiOut::sendMessage wants std::vector<unsigned char> - same
        // width as uint8_t, but not guaranteed to be the same type, so
        // copy rather than reinterpret_cast the vector itself.
        std::vector<unsigned char> msg(bytes.begin(), bytes.end());
        out_->sendMessage(&msg);
        return true;
    } catch (const RtMidiError&) {
        return false;
    }
}
