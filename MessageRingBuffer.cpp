#include "MessageRingBuffer.h"
#include <cstring>

MessageRingBuffer::MessageRingBuffer() : _start(0), _count(0) {
    std::memset(_buffer, 0, sizeof(_buffer));
}

void MessageRingBuffer::add(const String& msg) {
    size_t idx = (_start + _count) % Config::MAX_MESSAGES;
    std::strncpy(_buffer[idx], msg.c_str(), Config::MAX_MSG_LENGTH);
    _buffer[idx][Config::MAX_MSG_LENGTH] = '\0'; // Nullterminierung sichern

    if (_count < Config::MAX_MESSAGES) {
        _count++;
    } else {
        _start = (_start + 1) % Config::MAX_MESSAGES;
    }
}

size_t MessageRingBuffer::size() const {
    return _count;
}

const char* MessageRingBuffer::get(size_t index) const {
    if (index >= _count) return "";
    size_t idx = (_start + index) % Config::MAX_MESSAGES;
    return _buffer[idx];
}
