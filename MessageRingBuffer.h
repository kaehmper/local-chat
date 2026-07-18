#pragma once

#include <Arduino.h>
#include "config.h"

/**
 * @class MessageRingBuffer
 * @brief Eine hocheffiziente Ringpuffer-Implementierung für Chat-Nachrichten.
 *
 * Vermeidet teure dynamische Speicherverschiebungen durch rotierende Indizes.
 */
class MessageRingBuffer {
public:
    MessageRingBuffer();

    /**
     * @brief Fügt eine Nachricht dem Ringpuffer hinzu.
     * @param msg Die Nachricht.
     */
    void add(const String& msg);

    /**
     * @brief Gibt die Anzahl der aktuell gespeicherten Nachrichten zurück.
     */
    size_t size() const;

    /**
     * @brief Ruft eine Nachricht an einer bestimmten relativen Position ab (0 = älteste).
     */
    const char* get(size_t index) const;

private:
    char _buffer[Config::MAX_MESSAGES][Config::MAX_MSG_LENGTH + 1];
    size_t _start;
    size_t _count;
};
